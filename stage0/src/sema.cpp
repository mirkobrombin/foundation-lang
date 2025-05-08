#include "foundation/sema.hpp"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace foundation {

namespace {

struct FunctionSignature {
    Type returnType{invalidType};
    std::vector<Type> parameters;
};

struct GenericCall {
    FirFunctionId from{};
    FirFunctionId to{};
    std::vector<Type> arguments;
    SourceSpan span;
};

enum class MoveState {
    Available,
    Moved,
    MaybeMoved,
};

enum class ExpressionUse {
    Consume,
    Inspect,
};

enum class LoanState {
    None,
    View,
    Edit,
};

std::string semanticTypeKey(const Type &type) {
    std::string result = std::to_string(static_cast<unsigned int>(type.kind)) + ':' +
                         std::to_string(type.declaration);
    if (!type.arguments.empty()) {
        result += '<';
        for (const auto &argument : type.arguments) {
            result += semanticTypeKey(argument) + ';';
        }
        result += '>';
    }
    return result;
}

bool containsProperType(const Type &outer, const Type &candidate) {
    for (const auto &argument : outer.arguments) {
        if (argument == candidate || containsProperType(argument, candidate)) {
            return true;
        }
    }
    return false;
}

bool isCIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto first = value.front();
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
          first == '_')) {
        return false;
    }
    if (!std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    })) {
        return false;
    }

    static const std::unordered_set<std::string_view> invalid{
        "alignas",          "alignof",          "and",              "and_eq",
        "asm",              "auto",             "bitand",           "bitor",
        "bool",             "break",            "case",             "catch",
        "char",             "char8_t",          "char16_t",         "char32_t",
        "class",            "compl",            "concept",          "const",
        "consteval",        "constexpr",        "constinit",        "const_cast",
        "continue",         "co_await",         "co_return",        "co_yield",
        "decltype",         "default",          "delete",           "do",
        "double",           "dynamic_cast",     "else",             "enum",
        "explicit",         "export",           "extern",           "false",
        "float",            "for",              "friend",           "goto",
        "if",               "import",           "inline",           "int",
        "long",             "module",           "mutable",          "namespace",
        "new",              "noexcept",         "not",              "not_eq",
        "nullptr",          "operator",         "or",               "or_eq",
        "private",          "protected",        "public",           "register",
        "reinterpret_cast", "requires",         "restrict",         "return",
        "short",            "signed",           "sizeof",           "static",
        "static_assert",    "static_cast",      "struct",           "switch",
        "template",         "this",             "thread_local",     "throw",
        "true",             "try",              "typedef",          "typeid",
        "typename",         "union",            "unsigned",         "using",
        "virtual",          "void",             "volatile",         "wchar_t",
        "while",            "xor",              "xor_eq",           "_Alignas",
        "_Alignof",         "_Atomic",          "_Bool",            "_Complex",
        "_Generic",         "_Imaginary",       "_Noreturn",        "_Static_assert",
        "_Thread_local",
    };
    return !invalid.contains(value);
}

bool isReservedCSymbol(std::string_view value) {
    return value == "main" || value.starts_with("fdn_") || value.starts_with('_');
}

bool isCParameterType(const Type &type) {
    return type == i32Type || type == boolType ||
           (type.kind == TypeKind::View && type.arguments.size() == 1 &&
            type.arguments.front() == stringType);
}

bool isCReturnType(const Type &type) {
    return type == voidType || type == i32Type || type == boolType;
}

class Analyzer {
  public:
    Analyzer(const Program &program, Diagnostics &diagnostics)
        : program_(program), diagnostics_(diagnostics) {
        model_.expressionTypes.resize(program.expressions.size(), invalidType);
        model_.expressionLocals.resize(program.expressions.size());
        model_.callTargets.resize(program.expressions.size());
        model_.structTargets.resize(program.expressions.size());
        model_.expressionFields.resize(program.expressions.size());
        model_.enumTargets.resize(program.expressions.size());
        model_.matchTargets.resize(program.expressions.size());
        model_.ownershipTargets.resize(program.expressions.size());
        model_.functionValueTargets.resize(program.expressions.size());
        model_.closureTargets.resize(program.expressions.size());
        model_.expressionBorrowedClosures.resize(program.expressions.size());
        model_.expressionMoves.resize(program.expressions.size());
        model_.statementLocals.resize(program.statements.size());
        model_.statementElseLocals.resize(program.statements.size());
        model_.statementStructTargets.resize(program.statements.size());
        model_.statementDrops.resize(program.statements.size());
        model_.blockDrops.resize(program.blocks.size());
        model_.structs.resize(program.structs.size());
        model_.enums.resize(program.enums.size());
        model_.contracts.resize(program.contracts.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
        declareStructs();
        declareEnums();
        declareContracts();
        resolveContracts();
        resolveStructs();
        resolveEnums();
        rejectTypeCycles();
        declareFunctions();
        verifyImplementations();
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            if (!program_.functions[index].closure) {
                analyzeFunction(index);
            }
        }
        rejectPolymorphicRecursion();
        if (!diagnostics_.hasErrors()) {
            rejectVoidApplications();
        }
        if (diagnostics_.hasErrors()) {
            return std::nullopt;
        }
        return std::move(model_);
    }

  private:
    void declareStructs() {
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &declaration = program_.structs[index];
            if (isBuiltinType(declaration.name) ||
                !structs_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2020", "duplicate type " + declaration.name,
                                   declaration.span);
            }
        }
    }

    void declareEnums() {
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            const auto &declaration = program_.enums[index];
            if ((declaration.builtin == BuiltinEnumKind::None &&
                 isBuiltinType(declaration.name)) ||
                structs_.contains(declaration.name) ||
                !enums_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2020", "duplicate type " + declaration.name,
                                   declaration.span);
            }
            if (declaration.variants.empty()) {
                diagnostics_.error("FDN2031", "enum must declare at least one variant",
                                   declaration.span);
            }
        }
    }

    void declareContracts() {
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            const auto &declaration = program_.contracts[index];
            if (isBuiltinType(declaration.name) || structs_.contains(declaration.name) ||
                enums_.contains(declaration.name) ||
                !contracts_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2020", "duplicate type " + declaration.name,
                                   declaration.span);
            }
            if (declaration.methods.empty()) {
                diagnostics_.error("FDN2090", "contract must declare at least one method",
                                   declaration.span);
            }
        }
    }

    void resolveContracts() {
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            const auto &declaration = program_.contracts[index];
            auto &semantic = model_.contracts[index];
            setTypeParameters(declaration.typeParameters, declaration.span);
            semantic.typeParameterCount = declaration.typeParameters.size();
            std::unordered_set<std::string> methods;
            for (const auto &method : declaration.methods) {
                if (!methods.emplace(method.name).second) {
                    diagnostics_.error("FDN2091", "duplicate contract method " + method.name,
                                       method.span);
                }
                if (method.name == "drop") {
                    diagnostics_.error("FDN2138",
                                       "drop is reserved for deterministic cleanup",
                                       method.span);
                }
                SemanticContractMethod target;
                target.receiver = method.receiver;
                target.returnType = resolveType(method.returnType);
                if (containsBorrow(target.returnType) || containsBareSlice(target.returnType) ||
                    containsBareContract(target.returnType)) {
                    diagnostics_.error("FDN2063", "borrow cannot be returned from a method",
                                       method.returnType.span);
                }
                for (const auto &parameter : method.parameters) {
                    const auto type = resolveType(parameter.type);
                    if (type == voidType) {
                        diagnostics_.error("FDN2016", "parameter cannot have type void",
                                           parameter.span);
                    }
                    if (containsBareSlice(type)) {
                        diagnostics_.error("FDN2080", "slice parameter requires view or edit",
                                           parameter.span);
                    }
                    if (containsNestedBorrow(type, true)) {
                        diagnostics_.error("FDN2064", "parameter contains a nested borrow",
                                           parameter.span);
                    }
                    if (containsBareContract(type)) {
                        diagnostics_.error("FDN2099", "contract value requires view or edit",
                                           parameter.span);
                    }
                    target.parameterTypes.push_back(type);
                }
                semantic.methods.push_back(std::move(target));
            }
        }
    }

    bool containsBorrow(const Type &type) const {
        if (type.kind == TypeKind::Function) {
            return false;
        }
        if (type.kind == TypeKind::View || type.kind == TypeKind::Edit) {
            return true;
        }
        for (const auto &argument : type.arguments) {
            if (containsBorrow(argument)) {
                return true;
            }
        }
        return false;
    }

    bool containsNestedBorrow(const Type &type, bool allowRoot) const {
        if (type.kind == TypeKind::Function) {
            return false;
        }
        if (type.kind == TypeKind::View || type.kind == TypeKind::Edit) {
            if (!allowRoot) {
                return true;
            }
            for (const auto &argument : type.arguments) {
                if (containsNestedBorrow(argument, false)) {
                    return true;
                }
            }
            return false;
        }
        for (const auto &argument : type.arguments) {
            if (containsNestedBorrow(argument, false)) {
                return true;
            }
        }
        return false;
    }

    bool containsBareSlice(const Type &type) const {
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            const auto &slice = type.arguments.front();
            for (const auto &element : slice.arguments) {
                if (containsBareSlice(element)) {
                    return true;
                }
            }
            return false;
        }
        if (type.kind == TypeKind::Slice) {
            return true;
        }
        for (const auto &argument : type.arguments) {
            if (containsBareSlice(argument)) {
                return true;
            }
        }
        return false;
    }

    bool containsBareContract(const Type &type, bool allowBorrowTarget = true) const {
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Contract &&
            allowBorrowTarget) {
            return false;
        }
        if (type.kind == TypeKind::Contract) {
            return true;
        }
        for (const auto &argument : type.arguments) {
            if (containsBareContract(argument, false)) {
                return true;
            }
        }
        return false;
    }

    void resolveStructs() {
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &declaration = program_.structs[index];
            auto &semantic = model_.structs[index];
            setTypeParameters(declaration.typeParameters, declaration.span);
            semantic.typeParameterCount = declaration.typeParameters.size();
            std::unordered_map<std::string, std::size_t> fields;
            semantic.fieldTypes.reserve(declaration.fields.size());
            for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                const auto &source = declaration.fields[field];
                if (!fields.emplace(source.name, field).second) {
                    diagnostics_.error("FDN2021", "duplicate field " + source.name,
                                       source.span);
                }
                const auto type = resolveType(source.type);
                if (type == voidType) {
                    diagnostics_.error("FDN2022", "struct field cannot have type void",
                                       source.span);
                }
                if (containsBorrow(type)) {
                    diagnostics_.error("FDN2062", "borrow cannot be stored in a struct field",
                                       source.span);
                }
                if (containsBareSlice(type)) {
                    diagnostics_.error("FDN2080", "slice type requires view or edit", source.span);
                }
                if (containsBareContract(type)) {
                    diagnostics_.error("FDN2099", "contract value requires view or edit",
                                       source.span);
                }
                semantic.fieldTypes.push_back(type);
            }
            std::unordered_set<std::string> implementations;
            for (const auto &implementation : declaration.implementations) {
                const auto type = resolveType(implementation);
                semantic.implementations.push_back(type);
                if (type.kind != TypeKind::Contract) {
                    diagnostics_.error("FDN2092", "implements requires a contract",
                                       implementation.span);
                    continue;
                }
                const auto key = std::to_string(type.declaration);
                if (!implementations.emplace(key).second) {
                    diagnostics_.error("FDN2093", "duplicate contract implementation",
                                       implementation.span);
                }
            }
        }
    }

    void resolveEnums() {
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            const auto &declaration = program_.enums[index];
            auto &semantic = model_.enums[index];
            setTypeParameters(declaration.typeParameters, declaration.span);
            semantic.typeParameterCount = declaration.typeParameters.size();
            std::unordered_map<std::string, std::size_t> variants;
            semantic.payloadTypes.reserve(declaration.variants.size());
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                const auto &source = declaration.variants[variant];
                if (!variants.emplace(source.name, variant).second) {
                    diagnostics_.error("FDN2032", "duplicate variant " + source.name,
                                       source.span);
                }
                if (!source.payloadType.has_value()) {
                    semantic.payloadTypes.push_back(std::nullopt);
                    continue;
                }
                const auto type = resolveType(*source.payloadType);
                if (type == voidType) {
                    diagnostics_.error("FDN2033", "enum payload cannot have type void",
                                       source.span);
                }
                if (containsBorrow(type)) {
                    diagnostics_.error("FDN2062", "borrow cannot be stored in an enum payload",
                                       source.span);
                }
                if (containsBareSlice(type)) {
                    diagnostics_.error("FDN2080", "slice type requires view or edit", source.span);
                }
                if (containsBareContract(type)) {
                    diagnostics_.error("FDN2099", "contract value requires view or edit",
                                       source.span);
                }
                semantic.payloadTypes.push_back(type);
            }
        }
    }

    void rejectTypeCycles() {
        const auto structCount = program_.structs.size();
        const auto typeCount = structCount + program_.enums.size();
        struct Frame {
            Type type;
            std::size_t node{};
            std::string key;
            std::vector<std::pair<Type, SourceSpan>> children;
            std::size_t next{};
        };

        std::vector<std::pair<Type, SourceSpan>> roots;
        roots.reserve(typeCount);
        for (std::size_t id = 0; id < program_.structs.size(); ++id) {
            std::vector<Type> arguments;
            for (std::size_t parameter = 0;
                 parameter < program_.structs[id].typeParameters.size(); ++parameter) {
                arguments.emplace_back(TypeKind::Parameter, parameter);
            }
            roots.push_back({Type{TypeKind::Struct, id, std::move(arguments)},
                             program_.structs[id].span});
        }
        for (std::size_t id = 0; id < program_.enums.size(); ++id) {
            std::vector<Type> arguments;
            for (std::size_t parameter = 0;
                 parameter < program_.enums[id].typeParameters.size(); ++parameter) {
                arguments.emplace_back(TypeKind::Parameter, parameter);
            }
            roots.push_back({Type{TypeKind::Enum, id, std::move(arguments)},
                             program_.enums[id].span});
        }

        std::unordered_set<std::string> validated;
        for (const auto &rootEntry : roots) {
            const auto &root = rootEntry.first;
            const auto rootKey = semanticTypeKey(root);
            if (validated.contains(rootKey)) {
                continue;
            }
            std::vector<std::vector<Type>> active(typeCount);
            std::vector<Frame> stack;
            const auto rootNode = root.kind == TypeKind::Struct
                                      ? root.declaration
                                      : structCount + root.declaration;
            active[rootNode].push_back(root);
            stack.push_back({root, rootNode, rootKey, layoutChildren(root), 0});
            while (!stack.empty()) {
                auto &frame = stack.back();
                if (frame.next == frame.children.size()) {
                    active[frame.node].pop_back();
                    validated.insert(frame.key);
                    stack.pop_back();
                    continue;
                }
                const auto [child, span] = frame.children[frame.next++];
                if (child.kind != TypeKind::Struct && child.kind != TypeKind::Enum) {
                    continue;
                }
                const auto node = child.kind == TypeKind::Struct
                                      ? child.declaration
                                      : structCount + child.declaration;
                if (node >= active.size()) {
                    continue;
                }
                if (!active[node].empty() &&
                    !containsProperType(active[node].back(), child)) {
                    diagnostics_.error("FDN2023", "recursive value type is not allowed", span);
                    return;
                }
                const auto key = semanticTypeKey(child);
                if (validated.contains(key)) {
                    continue;
                }
                active[node].push_back(child);
                stack.push_back({child, node, key, layoutChildren(child), 0});
            }
        }
    }

    std::vector<std::pair<Type, SourceSpan>> layoutChildren(const Type &type) const {
        std::vector<std::pair<Type, SourceSpan>> children;
        if (type.kind == TypeKind::Struct && type.declaration < model_.structs.size()) {
            const auto &fields = model_.structs[type.declaration].fieldTypes;
            children.reserve(fields.size());
            for (std::size_t index = 0; index < fields.size(); ++index) {
                auto child = substitute(fields[index], type.arguments);
                while (child.kind == TypeKind::Array && child.arguments.size() == 1) {
                    const auto element = child.arguments.front();
                    child = element;
                }
                children.push_back(
                    {child, program_.structs[type.declaration].fields[index].span});
            }
        } else if (type.kind == TypeKind::Enum && type.declaration < model_.enums.size()) {
            const auto &variants = model_.enums[type.declaration].payloadTypes;
            for (std::size_t index = 0; index < variants.size(); ++index) {
                if (variants[index].has_value()) {
                    auto child = substitute(*variants[index], type.arguments);
                    while (child.kind == TypeKind::Array && child.arguments.size() == 1) {
                        const auto element = child.arguments.front();
                        child = element;
                    }
                    children.push_back(
                        {child, program_.enums[type.declaration].variants[index].span});
                }
            }
        }
        return children;
    }

    void declareFunctions() {
        bool foundMain = false;
        std::unordered_set<std::string> cSymbols;
        methods_.resize(program_.structs.size());
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            setTypeParameters(function.typeParameters, function.span);
            semantic.typeParameterCount = function.typeParameters.size();
            semantic.returnType = resolveType(function.returnType);
            if (containsBorrow(semantic.returnType) ||
                containsBareSlice(semantic.returnType) ||
                containsBareContract(semantic.returnType)) {
                diagnostics_.error("FDN2063", "borrow cannot be returned from a function",
                                   function.returnType.span);
            }
            for (const auto &parameter : function.parameters) {
                const auto type = resolveType(parameter.type);
                if (type == voidType) {
                    diagnostics_.error("FDN2016", "parameter cannot have type void",
                                       parameter.span);
                }
                if (containsBareSlice(type)) {
                    diagnostics_.error("FDN2080", "slice parameter requires view or edit",
                                       parameter.span);
                }
                if (containsNestedBorrow(type, true)) {
                    diagnostics_.error("FDN2064", "parameter contains a nested borrow",
                                       parameter.span);
                }
                if (containsBareContract(type)) {
                    diagnostics_.error("FDN2099", "contract value requires view or edit",
                                       parameter.span);
                }
                semantic.parameterTypes.push_back(type);
            }

            if (function.cSymbol.has_value()) {
                if (!isCIdentifier(*function.cSymbol)) {
                    diagnostics_.error("FDN2110", "invalid C or C++ symbol " +
                                                       *function.cSymbol,
                                       function.span);
                } else if (!cSymbols.emplace(*function.cSymbol).second) {
                    diagnostics_.error("FDN2111", "duplicate C symbol " + *function.cSymbol,
                                       function.span);
                }
                if (!function.typeParameters.empty()) {
                    diagnostics_.error("FDN2112", "C ABI function cannot be generic",
                                       function.span);
                }
                if (!isCReturnType(semantic.returnType)) {
                    diagnostics_.error("FDN2113", "return type is not C ABI safe",
                                       function.returnType.span);
                }
                for (std::size_t parameter = 0;
                     parameter < semantic.parameterTypes.size(); ++parameter) {
                    if (!isCParameterType(semantic.parameterTypes[parameter])) {
                        diagnostics_.error("FDN2114", "parameter type is not C ABI safe",
                                           function.parameters[parameter].span);
                    }
                }
                if (isReservedCSymbol(*function.cSymbol)) {
                    diagnostics_.error("FDN2115", "C ABI symbol is reserved " +
                                                       *function.cSymbol,
                                       function.span);
                }
                if (function.name == "main") {
                    diagnostics_.error("FDN2116", "main cannot use the C ABI declaration form",
                                       function.span);
                }
            }

            if (function.closure) {
                signatures_.push_back({semantic.returnType, semantic.parameterTypes});
                continue;
            }
            if (function.receiver.has_value()) {
                const auto owner = structs_.find(function.ownerType);
                if (owner == structs_.end()) {
                    diagnostics_.error("FDN2094", "method owner is not a struct", function.span);
                } else {
                    const auto prefix = function.ownerType + '.';
                    const auto methodName = function.name.starts_with(prefix)
                                                ? function.name.substr(prefix.size())
                                                : function.name;
                    if (!methods_[owner->second].emplace(methodName, index).second) {
                        diagnostics_.error("FDN2095", "duplicate method " + methodName,
                                           function.span);
                    }
                    if (methodName == "drop" &&
                        (function.receiver != ReceiverKind::Edit ||
                         semantic.parameterTypes.size() != 1 ||
                         semantic.returnType != voidType)) {
                        diagnostics_.error(
                            "FDN2137",
                            "drop must have signature fn drop(edit) void", function.span);
                    }
                }
            } else if (!functions_.emplace(function.name, index).second) {
                diagnostics_.error("FDN2001", "duplicate function " + function.name,
                                   function.span);
            }
            if (!function.receiver.has_value() && function.name == "print") {
                diagnostics_.error("FDN2018", "print is a reserved builtin", function.span);
            }
            if (!function.receiver.has_value() && function.name == "panic") {
                diagnostics_.error("FDN2018", "panic is a reserved builtin", function.span);
            }
            signatures_.push_back({semantic.returnType, semantic.parameterTypes});

            if (function.receiver.has_value() || function.name != "main" ||
                function.cSymbol.has_value()) {
                continue;
            }
            if (!foundMain) {
                model_.main = index;
            }
            foundMain = true;
            if (!function.typeParameters.empty() || !function.parameters.empty() ||
                semantic.returnType != i32Type) {
                diagnostics_.error("FDN2007", "main must have signature fn main() i32",
                                   function.span);
            }
        }
        if (!foundMain) {
            diagnostics_.error("FDN2006", "program must declare main", {0, 0, 1, 1});
        }
    }

    void verifyImplementations() {
        for (std::size_t structId = 0; structId < program_.structs.size(); ++structId) {
            const auto &declaration = program_.structs[structId];
            const auto &semantic = model_.structs[structId];
            for (std::size_t implementationIndex = 0;
                 implementationIndex < semantic.implementations.size(); ++implementationIndex) {
                const auto &implementation = semantic.implementations[implementationIndex];
                if (implementation.kind != TypeKind::Contract ||
                    implementation.declaration >= program_.contracts.size()) {
                    continue;
                }
                const auto &contract = program_.contracts[implementation.declaration];
                const auto &contractSemantic = model_.contracts[implementation.declaration];
                for (std::size_t methodIndex = 0; methodIndex < contract.methods.size();
                     ++methodIndex) {
                    const auto &requiredMethod = contract.methods[methodIndex];
                    const auto found = methods_[structId].find(requiredMethod.name);
                    if (found == methods_[structId].end()) {
                        diagnostics_.error(
                            "FDN2096",
                            declaration.name + " does not implement " + contract.name + '.' +
                                requiredMethod.name,
                            declaration.implementations[implementationIndex].span);
                        continue;
                    }
                    const auto function = found->second;
                    const auto &provided = program_.functions[function];
                    const auto &signature = signatures_[function];
                    const auto &required = contractSemantic.methods[methodIndex];
                    if (provided.receiver != requiredMethod.receiver) {
                        diagnostics_.error("FDN2097",
                                           "receiver mismatch for method " +
                                               requiredMethod.name,
                                           provided.span);
                    }
                    if (signature.parameters.size() != required.parameterTypes.size() + 1) {
                        diagnostics_.error("FDN2098",
                                           "parameter count mismatch for method " +
                                               requiredMethod.name,
                                           provided.span);
                        continue;
                    }
                    for (std::size_t parameter = 0;
                         parameter < required.parameterTypes.size(); ++parameter) {
                        requireSame(
                            substitute(required.parameterTypes[parameter],
                                       implementation.arguments),
                            signature.parameters[parameter + 1], provided.span,
                            "contract method parameter");
                    }
                    requireSame(substitute(required.returnType, implementation.arguments),
                                signature.returnType, provided.span,
                                "contract method return");
                    if (declaration.packageName != contract.packageName &&
                        !provided.exported) {
                        diagnostics_.error("FDN3008",
                                           "method " + requiredMethod.name +
                                               " is not exported",
                                           provided.span);
                    }
                }
            }
        }
    }

    void analyzeFunction(std::size_t index) {
        currentFunction_ = index;
        scopes_.clear();
        scopes_.emplace_back();
        resultOutstanding_.clear();
        resultExitReported_.clear();
        localSpans_.clear();
        moveStates_.clear();
        loanStates_.clear();

        const auto &function = program_.functions[index];
        setTypeParameters(function.typeParameters, function.span);
        auto &semantic = model_.functions[index];
        for (std::size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
            const auto &parameter = function.parameters[parameterIndex];
            const auto local = addLocal(parameter.name, semantic.parameterTypes[parameterIndex],
                                        false, parameter.span);
            semantic.parameters.push_back(local);
        }

        if (!function.hasBody) {
            return;
        }

        const auto returns = analyzeBlock(function.body, false);
        reportScope(scopes_.front());
        if (semantic.returnType != voidType && !returns) {
            diagnostics_.error("FDN2008", "function does not return on every path", function.span);
        }
    }

    bool analyzeBlock(AstBlockId id, bool nested) {
        if (nested) {
            scopes_.emplace_back();
        }

        bool returns = false;
        for (const auto statement : program_.blocks[id].statements) {
            if (analyzeStatement(statement)) {
                returns = true;
            }
        }

        model_.blockDrops[id] = scopeDrops(scopes_.back());

        if (nested) {
            reportScope(scopes_.back());
            scopes_.pop_back();
        }
        return returns;
    }

    bool analyzeStatement(AstStatementId id) {
        const auto &statement = program_.statements[id];
        if (const auto *variable = std::get_if<VariableStatement>(&statement.value)) {
            auto declared = invalidType;
            if (variable->type.has_value()) {
                declared = resolveType(*variable->type);
            }
            const auto hasElse = variable->elseBlock.has_value();
            const auto initializer = analyzeExpression(
                variable->initializer,
                !hasElse && declared.kind != TypeKind::Invalid ? std::optional<Type>{declared}
                                                               : std::nullopt);
            if (hasElse) {
                if (!isResult(initializer) || initializer.arguments.size() != 2) {
                    diagnostics_.error("FDN2053", "let else requires a Result initializer",
                                       statement.span);
                    declared = invalidType;
                } else {
                    const auto payload = initializer.arguments[0];
                    if (variable->type.has_value()) {
                        requireSame(declared, payload, statement.span, "let else payload");
                    } else {
                        declared = payload;
                    }
                    const auto successState = resultOutstanding_;
                    scopes_.emplace_back();
                    const auto errorLocal = addLocal(*variable->elseBinding,
                                                     initializer.arguments[1], false,
                                                     statement.span);
                    model_.statementElseLocals[id] = errorLocal;
                    const auto exits = analyzeBlock(*variable->elseBlock, false);
                    reportScope(scopes_.back());
                    scopes_.pop_back();
                    restoreOutstanding(successState);
                    if (!exits) {
                        diagnostics_.error("FDN2054", "let else block must exit",
                                           statement.span);
                    }
                }
            } else if (!variable->type.has_value()) {
                declared = initializer;
            } else {
                requireSame(declared, initializer, statement.span, "binding initializer");
            }
            if (declared == voidType) {
                diagnostics_.error("FDN2016", "binding initializer cannot be void", statement.span);
                declared = invalidType;
            }
            if (containsBorrow(declared)) {
                diagnostics_.error("FDN2074", "borrow cannot be stored in a local binding",
                                   statement.span);
            }
            if (containsBareSlice(declared)) {
                diagnostics_.error("FDN2080", "slice binding requires view or edit",
                                   statement.span);
            }
            if (containsBareContract(declared)) {
                diagnostics_.error("FDN2099", "contract value requires view or edit",
                                   statement.span);
            }
            const auto local =
                addLocal(variable->name, declared, variable->mutableBinding, statement.span);
            model_.statementLocals[id] = local;
            if (variable->initializer < model_.expressionBorrowedClosures.size()) {
                model_.functions[currentFunction_].locals[local].borrowedClosure =
                    model_.expressionBorrowedClosures[variable->initializer];
            }
            return false;
        }
        if (const auto *destructure =
                std::get_if<StructDestructureStatement>(&statement.value)) {
            const auto initializer = analyzeExpression(destructure->initializer);
            auto source = initializer;
            auto owned = false;
            if (source.kind == TypeKind::Own && source.arguments.size() == 1) {
                source = source.arguments.front();
                owned = true;
            }

            const auto found = structs_.find(destructure->type.name);
            if (found == structs_.end()) {
                diagnostics_.error("FDN2024", "unknown struct " + destructure->type.name,
                                   destructure->type.span);
            }
            if (source.kind != TypeKind::Struct ||
                source.declaration >= program_.structs.size()) {
                diagnostics_.error("FDN2130",
                                   "struct destructuring requires a struct value or owner",
                                   statement.span);
            } else if (found != structs_.end() && source.declaration != found->second) {
                diagnostics_.error("FDN2131", "struct pattern does not match initializer",
                                   statement.span);
            } else if (source.declaration < methods_.size() &&
                       methods_[source.declaration].contains("drop")) {
                diagnostics_.error("FDN2139",
                                   "struct with custom drop cannot be destructured",
                                   statement.span);
            }

            const auto type = source.kind == TypeKind::Struct ? source : invalidType;
            const auto declarationId =
                found != structs_.end()
                    ? found->second
                    : (source.kind == TypeKind::Struct ? source.declaration
                                                       : program_.structs.size());
            std::vector<bool> bound;
            if (declarationId < program_.structs.size()) {
                bound.resize(program_.structs[declarationId].fields.size());
            }
            std::vector<FirFieldId> fields;
            std::vector<FirLocalId> bindings;
            for (const auto &pattern : destructure->fields) {
                if (declarationId >= program_.structs.size()) {
                    bindings.push_back(addLocal(pattern.binding, invalidType, false,
                                                pattern.span));
                    fields.push_back(0);
                    continue;
                }
                const auto field = findField(declarationId, pattern.field);
                if (!field.has_value()) {
                    diagnostics_.error("FDN2025", "unknown field " + pattern.field,
                                       pattern.span);
                    bindings.push_back(addLocal(pattern.binding, invalidType, false,
                                                pattern.span));
                    fields.push_back(0);
                    continue;
                }
                const auto &declaration = program_.structs[declarationId];
                if (declaration.packageName != currentPackage() &&
                    !declaration.fields[*field].exported) {
                    diagnostics_.error("FDN3008", "field " + pattern.field +
                                                        " is not exported",
                                       pattern.span);
                }
                if (bound[*field]) {
                    diagnostics_.error("FDN2132", "duplicate field pattern " + pattern.field,
                                       pattern.span);
                }
                bound[*field] = true;
                auto fieldType = invalidType;
                if (type.kind == TypeKind::Struct && type.declaration == declarationId) {
                    fieldType = substitute(model_.structs[declarationId].fieldTypes[*field],
                                           type.arguments);
                }
                const auto local = addLocal(pattern.binding, fieldType, false, pattern.span);
                fields.push_back(*field);
                bindings.push_back(local);
            }
            if (declarationId < program_.structs.size()) {
                for (std::size_t field = 0; field < bound.size(); ++field) {
                    if (!bound[field]) {
                        diagnostics_.error(
                            "FDN2133",
                            "struct pattern is missing field " +
                                program_.structs[declarationId].fields[field].name,
                            statement.span);
                    }
                }
            }
            if (model_.expressionBorrowedClosures[destructure->initializer]) {
                diagnostics_.error("FDN2127", "borrowed closure cannot be destructured",
                                   statement.span);
            }
            model_.statementStructTargets[id] = StructDestructureTarget{
                type, owned, std::move(fields), std::move(bindings)};
            return false;
        }
        if (const auto *assignment = std::get_if<AssignmentStatement>(&statement.value)) {
            const auto &targetExpression = program_.expressions[assignment->target];
            if (const auto *name = std::get_if<NameExpression>(&targetExpression.value)) {
                const auto local = findLocal(name->name, statement.span);
                const auto expected = local.has_value()
                                          ? std::optional<Type>{model_.functions[currentFunction_]
                                                                    .locals[*local]
                                                                    .type}
                                          : std::nullopt;
                const auto value = analyzeExpression(assignment->value, expected);
                if (!local.has_value()) {
                    return false;
                }
                model_.expressionLocals[assignment->target] = *local;
                model_.expressionTypes[assignment->target] = *expected;
                model_.statementLocals[id] = *local;
                const auto &declaration = model_.functions[currentFunction_].locals[*local];
                if (!declaration.mutableBinding) {
                    diagnostics_.error("FDN2013", "cannot assign to immutable binding " +
                                                        name->name,
                                       statement.span);
                }
                if (loanStates_[*local] != LoanState::None) {
                    diagnostics_.error("FDN2075", "cannot replace borrowed binding " +
                                                        declaration.name,
                                       statement.span);
                }
                requireSame(declaration.type, value, statement.span, "assignment");
                if (assignment->value < model_.expressionBorrowedClosures.size() &&
                    model_.expressionBorrowedClosures[assignment->value]) {
                    diagnostics_.error("FDN2127", "borrowed closure cannot be assigned",
                                       statement.span);
                }
                if (requiresDrop(declaration.type)) {
                    moveStates_[*local] = MoveState::Available;
                }
                if (isResult(declaration.type)) {
                    if (resultOutstanding_[*local]) {
                        diagnostics_.error("FDN2052", "assignment replaces an unhandled Result",
                                           statement.span);
                    }
                    resultOutstanding_[*local] = true;
                }
                return false;
            }

            const auto target = analyzeExpression(assignment->target, std::nullopt,
                                                  ExpressionUse::Inspect);
            const auto value = analyzeExpression(assignment->value, target);
            if (!editablePlace(assignment->target)) {
                diagnostics_.error("FDN2077",
                                   "field assignment requires a mutable binding or edit borrow",
                                   statement.span);
            }
            requireSame(target, value, statement.span, "assignment");
            return false;
        }
        if (const auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            const auto type = analyzeExpression(expression->expression);
            if (isResult(type)) {
                diagnostics_.error("FDN2051", "Result value must be handled or discarded",
                                   statement.span);
            }
            if (requiresDrop(type)) {
                diagnostics_.error("FDN2076", "owned value must be handled or discarded",
                                   statement.span);
            }
            const auto &target = model_.callTargets[expression->expression];
            if (target.has_value() && target->kind == CallTargetKind::Panic) {
                std::fill(resultOutstanding_.begin(), resultOutstanding_.end(), false);
                return true;
            }
            return false;
        }
        if (const auto *returned = std::get_if<ReturnStatement>(&statement.value)) {
            const auto expected = model_.functions[currentFunction_].returnType;
            if (!returned->value.has_value()) {
                if (expected != voidType) {
                    diagnostics_.error("FDN2014", "non-void function must return a value",
                                       statement.span);
                }
                reportOutstanding(statement.span);
                model_.statementDrops[id] = activeDrops();
                return true;
            }
            const auto value = analyzeExpression(*returned->value, expected);
            if (*returned->value < model_.expressionBorrowedClosures.size() &&
                model_.expressionBorrowedClosures[*returned->value]) {
                diagnostics_.error("FDN2127", "borrowed closure cannot be returned",
                                   statement.span);
            }
            const auto &target = model_.callTargets[*returned->value];
            if (target.has_value() && target->kind == CallTargetKind::Panic) {
                std::fill(resultOutstanding_.begin(), resultOutstanding_.end(), false);
                return true;
            }
            if (expected == voidType) {
                diagnostics_.error("FDN2015", "void function must use a bare return",
                                   statement.span);
                return true;
            }
            requireSame(expected, value, statement.span, "return");
            reportOutstanding(statement.span);
            model_.statementDrops[id] = activeDrops();
            return true;
        }
        if (const auto *discarded = std::get_if<DiscardStatement>(&statement.value)) {
            static_cast<void>(analyzeExpression(discarded->value));
            return false;
        }
        if (const auto *branch = std::get_if<IfStatement>(&statement.value)) {
            requireSame(boolType, analyzeExpression(branch->condition), statement.span,
                        "if condition");
            const auto before = resultOutstanding_;
            const auto movesBefore = moveStates_;
            const auto loansBefore = loanStates_;
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto thenState = outstandingPrefix(before.size());
            const auto thenMoves = movePrefix(movesBefore.size());
            const auto thenLoans = loanPrefix(loansBefore.size());
            restoreOutstanding(before);
            restoreMoves(movesBefore);
            restoreLoans(loansBefore);
            auto elseReturns = false;
            if (branch->elseBlock.has_value()) {
                elseReturns = analyzeBlock(*branch->elseBlock, true);
            }
            const auto elseState = outstandingPrefix(before.size());
            const auto elseMoves = movePrefix(movesBefore.size());
            const auto elseLoans = loanPrefix(loansBefore.size());
            std::vector<bool> merged(before.size());
            for (std::size_t local = 0; local < before.size(); ++local) {
                if (thenReturns && elseReturns) {
                    merged[local] = false;
                } else if (thenReturns) {
                    merged[local] = elseState[local];
                } else if (elseReturns) {
                    merged[local] = thenState[local];
                } else {
                    merged[local] = thenState[local] || elseState[local];
                }
            }
            restoreOutstanding(merged);
            restoreMoves(mergeMoves(movesBefore, thenMoves, elseMoves, thenReturns, elseReturns));
            restoreLoans(
                mergeLoans(loansBefore, thenLoans, elseLoans, thenReturns, elseReturns));
            return thenReturns && elseReturns;
        }

        const auto &loop = std::get<WhileStatement>(statement.value);
        const auto movesBeforeCondition = moveStates_;
        requireSame(boolType, analyzeExpression(loop.condition), statement.span,
                    "while condition");
        for (std::size_t local = 0; local < movesBeforeCondition.size(); ++local) {
            if (moveStates_[local] != movesBeforeCondition[local]) {
                diagnostics_.error("FDN2079", "loop condition cannot move binding " +
                                                   model_.functions[currentFunction_]
                                                       .locals[local]
                                                       .name,
                                   statement.span);
            }
        }
        const auto before = resultOutstanding_;
        const auto movesBefore = moveStates_;
        const auto bodyReturns = analyzeBlock(loop.body, true);
        const auto bodyState = outstandingPrefix(before.size());
        const auto bodyMoves = movePrefix(movesBefore.size());
        std::vector<bool> merged(before.size());
        for (std::size_t local = 0; local < before.size(); ++local) {
            merged[local] = before[local] || bodyState[local];
        }
        restoreOutstanding(merged);
        std::vector<MoveState> loopMoves(movesBefore.size());
        for (std::size_t local = 0; local < loopMoves.size(); ++local) {
            if (!bodyReturns && movesBefore[local] != bodyMoves[local]) {
                diagnostics_.error("FDN2079", "loop body cannot leave binding " +
                                                   model_.functions[currentFunction_]
                                                       .locals[local]
                                                       .name +
                                                   " moved",
                                   statement.span);
            }
            loopMoves[local] = bodyReturns || movesBefore[local] == bodyMoves[local]
                                   ? movesBefore[local]
                                   : MoveState::MaybeMoved;
        }
        restoreMoves(loopMoves);
        return false;
    }

    Type analyzeExpression(AstExpressionId id, std::optional<Type> expected = std::nullopt,
                           ExpressionUse use = ExpressionUse::Consume) {
        const auto &expression = program_.expressions[id];
        auto type = invalidType;
        if (const auto *integer = std::get_if<IntegerExpression>(&expression.value)) {
            if (integer->value < INT32_MIN || integer->value > INT32_MAX) {
                diagnostics_.error("FDN2005", "integer literal does not fit i32", expression.span);
            }
            type = i32Type;
        } else if (std::holds_alternative<BooleanExpression>(expression.value)) {
            type = boolType;
        } else if (std::holds_alternative<StringExpression>(expression.value)) {
            type = stringType;
        } else if (const auto *array = std::get_if<ArrayExpression>(&expression.value)) {
            type = analyzeArray(id, *array, expected, expression.span);
        } else if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            const auto local = lookupLocal(name->name);
            if (local.has_value()) {
                model_.expressionLocals[id] = *local;
                type = model_.functions[currentFunction_].locals[*local].type;
                if (loanStates_[*local] == LoanState::Edit) {
                    diagnostics_.error("FDN2073", "cannot inspect edited binding " + name->name,
                                       expression.span);
                }
                if (use == ExpressionUse::Consume &&
                    model_.functions[currentFunction_].locals[*local].borrowedClosure) {
                    diagnostics_.error("FDN2127", "borrowed closure cannot escape",
                                       expression.span);
                }
                if (requiresDrop(type)) {
                    if (model_.functions[currentFunction_].locals[*local].capture &&
                        use == ExpressionUse::Consume) {
                        diagnostics_.error("FDN2126", "closure capture cannot be consumed " +
                                                            name->name,
                                           expression.span);
                    }
                    if (moveStates_[*local] == MoveState::Moved) {
                        diagnostics_.error("FDN2065", "use of moved binding " + name->name,
                                           expression.span);
                    } else if (moveStates_[*local] == MoveState::MaybeMoved) {
                        diagnostics_.error("FDN2066", "binding may have moved " + name->name,
                                           expression.span);
                    } else if (use == ExpressionUse::Consume) {
                        if (loanStates_[*local] != LoanState::None) {
                            diagnostics_.error("FDN2067", "cannot move borrowed binding " +
                                                                name->name,
                                               expression.span);
                        }
                        moveStates_[*local] = MoveState::Moved;
                        model_.expressionMoves[id] = true;
                    }
                }
                if (isResult(type)) {
                    resultOutstanding_[*local] = false;
                }
            } else {
                auto function = functions_.find(name->name);
                if (function == functions_.end() && name->name.find('.') == std::string::npos &&
                    !currentPackage().empty()) {
                    function = functions_.find(std::string(currentPackage()) + '.' + name->name);
                }
                if (function == functions_.end()) {
                    static_cast<void>(findLocal(name->name, expression.span));
                    model_.expressionTypes[id] = type;
                    return type;
                }
                const auto functionId = function->second;
                if (program_.functions[functionId].name == "main") {
                    diagnostics_.error("FDN2019", "main cannot be used as a value",
                                       expression.span);
                }
                const auto &signature = signatures_[functionId];
                std::vector<std::optional<Type>> inferred(
                    program_.functions[functionId].typeParameters.size());
                if (name->typeArguments.size() > inferred.size()) {
                    diagnostics_.error("FDN2043", "wrong type argument count for function " +
                                                       name->name,
                                       expression.span);
                }
                for (std::size_t index = 0;
                     index < name->typeArguments.size() && index < inferred.size(); ++index) {
                    inferred[index] = resolveType(name->typeArguments[index]);
                }
                if (expected.has_value() && expected->kind == TypeKind::Function &&
                    expected->arguments.size() == signature.parameters.size() + 1) {
                    inferType(signature.returnType, expected->arguments.front(), inferred,
                              expression.span, "function value return");
                    for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
                        inferType(signature.parameters[index], expected->arguments[index + 1],
                                  inferred, expression.span, "function value parameter");
                    }
                }
                const auto typeArguments = completeInference(
                    inferred, program_.functions[functionId].typeParameters, expression.span,
                    name->name);
                std::vector<Type> parts;
                parts.push_back(substitute(signature.returnType, typeArguments));
                for (const auto &parameter : signature.parameters) {
                    parts.push_back(substitute(parameter, typeArguments));
                }
                type = Type{TypeKind::Function, 0, std::move(parts)};
                model_.functionValueTargets[id] =
                    FunctionValueTarget{functionId, std::move(typeArguments)};
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            type = analyzeUnary(*unary, expression.span);
        } else if (const auto *ownership = std::get_if<OwnershipExpression>(&expression.value)) {
            type = analyzeOwnership(id, *ownership, expression.span);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
            type = analyzeBinary(*binary, expression.span);
        } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
            type = analyzeCall(id, *call, expression.span);
        } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            type = analyzeStruct(id, *literal, expression.span);
        } else if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            type = analyzeMember(id, *member, expected, use, expression.span);
        } else if (const auto *index = std::get_if<IndexExpression>(&expression.value)) {
            type = analyzeIndex(*index, use, expression.span);
        } else if (const auto *replace = std::get_if<ReplaceExpression>(&expression.value)) {
            type = analyzeReplace(id, *replace, expression.span);
        } else if (const auto *function = std::get_if<FunctionExpression>(&expression.value)) {
            type = analyzeClosure(id, *function, expected, expression.span);
        } else {
            type = analyzeMatch(id, std::get<MatchExpression>(expression.value), expected,
                                expression.span);
        }
        model_.expressionTypes[id] = type;
        return type;
    }

    Type analyzeOwnership(AstExpressionId id, const OwnershipExpression &ownership,
                          SourceSpan span) {
        if (ownership.operation == OwnershipOperator::Own) {
            const auto value = analyzeExpression(ownership.operand);
            if (value == voidType || value.kind == TypeKind::Invalid ||
                value.kind == TypeKind::Own || value.kind == TypeKind::View ||
                value.kind == TypeKind::Edit) {
                diagnostics_.error("FDN2068", "own requires a non-borrowed value", span);
                return invalidType;
            }
            const auto result = Type{TypeKind::Own, 0, {value}};
            model_.ownershipTargets[id] = OwnershipTarget{ownership.operation, std::nullopt};
            return result;
        }

        const auto &operand = program_.expressions[ownership.operand];
        if (!std::holds_alternative<NameExpression>(operand.value)) {
            static_cast<void>(analyzeExpression(ownership.operand, std::nullopt,
                                                ExpressionUse::Inspect));
            diagnostics_.error("FDN2069", "borrow requires a binding", span);
            return invalidType;
        }
        const auto value = analyzeExpression(ownership.operand, std::nullopt,
                                             ExpressionUse::Inspect);
        const auto local = model_.expressionLocals[ownership.operand];
        if (!local.has_value()) {
            if (ownership.operation == OwnershipOperator::View &&
                model_.functionValueTargets[ownership.operand].has_value()) {
                if (!transientBorrowsAllowed_) {
                    diagnostics_.error("FDN2070",
                                       "borrow must be passed directly to a function", span);
                }
                model_.ownershipTargets[id] =
                    OwnershipTarget{ownership.operation, std::nullopt};
                return Type{TypeKind::View, 0, {value}};
            }
            if (ownership.operation == OwnershipOperator::Edit &&
                model_.functionValueTargets[ownership.operand].has_value()) {
                diagnostics_.error("FDN2072", "edit requires a mutable binding", span);
            } else {
                diagnostics_.error("FDN2069", "borrow requires a binding", span);
            }
            return invalidType;
        }
        if (!transientBorrowsAllowed_) {
            diagnostics_.error("FDN2070", "borrow must be passed directly to a function", span);
        }

        Type target = value;
        if ((value.kind == TypeKind::Own || value.kind == TypeKind::View ||
             value.kind == TypeKind::Edit) &&
            value.arguments.size() == 1) {
            target = value.arguments.front();
        }
        if (target.kind == TypeKind::Array && target.arguments.size() == 1) {
            target = Type{TypeKind::Slice, 0, {target.arguments.front()}};
        }
        if (value.kind == TypeKind::View && ownership.operation == OwnershipOperator::Edit) {
            diagnostics_.error("FDN2071", "shared view cannot become an edit", span);
        }
        if (ownership.operation == OwnershipOperator::Edit &&
            !model_.functions[currentFunction_].locals[*local].mutableBinding &&
            value.kind != TypeKind::Edit) {
            diagnostics_.error("FDN2072", "edit requires a mutable binding", span);
        }

        const auto requested = ownership.operation == OwnershipOperator::View ? LoanState::View
                                                                               : LoanState::Edit;
        if ((requested == LoanState::View && loanStates_[*local] == LoanState::Edit) ||
            (requested == LoanState::Edit && loanStates_[*local] != LoanState::None)) {
            diagnostics_.error("FDN2073", "conflicting borrow of binding " +
                                                model_.functions[currentFunction_].locals[*local]
                                                    .name,
                               span);
        } else if (requested == LoanState::Edit || loanStates_[*local] == LoanState::None) {
            loanStates_[*local] = requested;
        }

        model_.ownershipTargets[id] = OwnershipTarget{ownership.operation, local};
        return Type{ownership.operation == OwnershipOperator::View ? TypeKind::View
                                                                    : TypeKind::Edit,
                    0, {target}};
    }

    Type analyzeUnary(const UnaryExpression &unary, SourceSpan span) {
        const auto operand = analyzeExpression(unary.operand);
        if (unary.operation == UnaryOperator::Negate) {
            requireSame(i32Type, operand, span, "unary -");
            return i32Type;
        }
        requireSame(boolType, operand, span, "unary !");
        return boolType;
    }

    Type analyzeReplace(AstExpressionId id, const ReplaceExpression &replace,
                        SourceSpan span) {
        const auto context = placeContextType(replace.target);
        const auto root = placeRootLocal(replace.target);
        const auto before = root.has_value() ? moveStates_[*root] : MoveState::Available;
        const auto value = analyzeExpression(
            replace.value,
            context.kind == TypeKind::Invalid ? std::nullopt : std::optional<Type>{context});
        const auto consumedDestination =
            root.has_value() && before == MoveState::Available &&
            moveStates_[*root] != MoveState::Available;
        const auto target = analyzeExpression(replace.target, std::nullopt,
                                              ExpressionUse::Inspect);
        if (!isPlaceExpression(replace.target)) {
            diagnostics_.error("FDN2134", "replace requires a place", span);
        } else if (!editablePlace(replace.target)) {
            diagnostics_.error("FDN2135", "replace requires a mutable place", span);
        }
        if (containsBorrow(target)) {
            diagnostics_.error("FDN2136", "replace cannot target a borrow", span);
        }
        if (root.has_value() && loanStates_[*root] != LoanState::None) {
            diagnostics_.error("FDN2075", "cannot replace borrowed binding " +
                                                model_.functions[currentFunction_]
                                                    .locals[*root]
                                                    .name,
                               span);
        }
        if (root.has_value() &&
            model_.functions[currentFunction_].locals[*root].borrowedClosure) {
            diagnostics_.error("FDN2127", "borrowed closure cannot be replaced", span);
        }
        requireSame(target, value, span, "replacement");
        if (consumedDestination) {
            diagnostics_.error("FDN2136", "replacement value consumes its destination", span);
        }
        if (model_.expressionBorrowedClosures[replace.value]) {
            diagnostics_.error("FDN2127", "borrowed closure cannot be stored by replace", span);
            model_.expressionBorrowedClosures[id] = true;
        }
        if (root.has_value() &&
            std::holds_alternative<NameExpression>(program_.expressions[replace.target].value) &&
            isResult(target)) {
            resultOutstanding_[*root] = true;
        }
        return target;
    }

    Type analyzeClosure(AstExpressionId id, const FunctionExpression &expression,
                        std::optional<Type> expected, SourceSpan span) {
        if (expression.function >= program_.functions.size() ||
            !program_.functions[expression.function].closure) {
            diagnostics_.error("FDN2129", "invalid closure target", span);
            return invalidType;
        }

        const auto closureId = expression.function;
        const auto &closure = program_.functions[closureId];
        const auto &signature = signatures_[closureId];
        std::vector<Type> parts;
        parts.reserve(signature.parameters.size() + 1);
        parts.push_back(signature.returnType);
        parts.insert(parts.end(), signature.parameters.begin(), signature.parameters.end());
        const Type closureType{TypeKind::Function, 0, std::move(parts)};
        if (expected.has_value()) {
            auto target = *expected;
            if ((target.kind == TypeKind::View || target.kind == TypeKind::Edit) &&
                target.arguments.size() == 1) {
                const auto borrowed = target.arguments.front();
                target = borrowed;
            }
            requireSame(target, closureType, span, "closure signature");
        }

        std::unordered_set<std::string> captureNames;
        std::unordered_set<std::string> parameterNames;
        for (const auto &parameter : closure.parameters) {
            parameterNames.insert(parameter.name);
        }
        std::vector<FirLocalId> captureSources;
        std::vector<CaptureMode> captureModes;
        std::vector<Type> captureTypes;
        bool borrowed = false;
        for (const auto &capture : closure.captures) {
            if (!captureNames.insert(capture.name).second) {
                diagnostics_.error("FDN2121", "duplicate capture " + capture.name,
                                   capture.span);
                continue;
            }
            if (parameterNames.contains(capture.name)) {
                diagnostics_.error("FDN2122", "capture conflicts with parameter " +
                                                    capture.name,
                                   capture.span);
            }
            const auto source = lookupLocal(capture.name);
            if (!source.has_value()) {
                diagnostics_.error("FDN2120", "unknown captured binding " + capture.name,
                                   capture.span);
                continue;
            }
            const auto type = model_.functions[currentFunction_].locals[*source].type;
            if (model_.functions[currentFunction_].locals[*source].borrowedClosure) {
                diagnostics_.error("FDN2127", "borrowed closure cannot be captured",
                                   capture.span);
            }
            if (containsBorrow(type)) {
                diagnostics_.error("FDN2123", "capture cannot contain an existing borrow",
                                   capture.span);
            }
            if (capture.mode == CaptureMode::Copy) {
                if (requiresDrop(type)) {
                    diagnostics_.error("FDN2123", "move-only capture requires own " +
                                                        capture.name,
                                       capture.span);
                }
            } else if (capture.mode == CaptureMode::Own) {
                if (moveStates_[*source] == MoveState::Moved) {
                    diagnostics_.error("FDN2065", "use of moved binding " + capture.name,
                                       capture.span);
                } else if (moveStates_[*source] == MoveState::MaybeMoved) {
                    diagnostics_.error("FDN2066", "binding may have moved " + capture.name,
                                       capture.span);
                } else if (loanStates_[*source] != LoanState::None) {
                    diagnostics_.error("FDN2067", "cannot move borrowed binding " +
                                                        capture.name,
                                       capture.span);
                }
                if (requiresDrop(type)) {
                    moveStates_[*source] = MoveState::Moved;
                }
                if (isResult(type)) {
                    resultOutstanding_[*source] = false;
                }
            } else {
                const auto requested = capture.mode == CaptureMode::View ? LoanState::View
                                                                         : LoanState::Edit;
                if (requested == LoanState::Edit &&
                    !model_.functions[currentFunction_].locals[*source].mutableBinding &&
                    type.kind != TypeKind::Edit) {
                    diagnostics_.error("FDN2124", "edit capture requires a mutable binding",
                                       capture.span);
                }
                if ((requested == LoanState::View && loanStates_[*source] == LoanState::Edit) ||
                    (requested == LoanState::Edit && loanStates_[*source] != LoanState::None)) {
                    diagnostics_.error("FDN2125", "conflicting capture of binding " +
                                                        capture.name,
                                       capture.span);
                } else if (requested == LoanState::Edit ||
                           loanStates_[*source] == LoanState::None) {
                    loanStates_[*source] = requested;
                }
                borrowed = true;
            }
            captureSources.push_back(*source);
            captureModes.push_back(capture.mode);
            captureTypes.push_back(type);
        }

        if (captureTypes.size() != closure.captures.size()) {
            model_.closureTargets[id] = ClosureTarget{closureId, std::move(captureSources),
                                                       std::move(captureModes), borrowed};
            model_.expressionBorrowedClosures[id] = borrowed;
            return closureType;
        }

        auto outerNames = std::unordered_set<std::string>{};
        for (const auto &scope : scopes_) {
            for (const auto &[name, local] : scope) {
                static_cast<void>(local);
                outerNames.insert(name);
            }
        }

        const auto savedFunction = currentFunction_;
        auto savedScopes = std::move(scopes_);
        auto savedOutstanding = std::move(resultOutstanding_);
        auto savedExitReported = std::move(resultExitReported_);
        auto savedSpans = std::move(localSpans_);
        auto savedMoves = std::move(moveStates_);
        auto savedLoans = std::move(loanStates_);
        auto savedTypeParameters = std::move(typeParameters_);
        auto savedTypeParameterNames = std::move(currentTypeParameterNames_);
        auto savedClosureOuterNames = std::move(closureOuterNames_);

        currentFunction_ = closureId;
        scopes_.clear();
        scopes_.emplace_back();
        resultOutstanding_.clear();
        resultExitReported_.clear();
        localSpans_.clear();
        moveStates_.clear();
        loanStates_.clear();
        closureOuterNames_ = std::move(outerNames);
        setTypeParameters(closure.typeParameters, closure.span);
        auto &semantic = model_.functions[closureId];
        for (std::size_t index = 0; index < captureTypes.size(); ++index) {
            const auto local = addLocal(closure.captures[index].name, captureTypes[index],
                                        captureModes[index] == CaptureMode::Edit,
                                        closure.captures[index].span);
            semantic.locals[local].capture = true;
            semantic.locals[local].captureMode = captureModes[index];
        }
        for (std::size_t parameter = 0; parameter < closure.parameters.size(); ++parameter) {
            const auto local = addLocal(closure.parameters[parameter].name,
                                        semantic.parameterTypes[parameter], false,
                                        closure.parameters[parameter].span);
            semantic.parameters.push_back(local);
        }
        const auto returns = analyzeBlock(closure.body, false);
        reportScope(scopes_.front());
        if (semantic.returnType != voidType && !returns) {
            diagnostics_.error("FDN2008", "function does not return on every path",
                               closure.span);
        }

        currentFunction_ = savedFunction;
        scopes_ = std::move(savedScopes);
        resultOutstanding_ = std::move(savedOutstanding);
        resultExitReported_ = std::move(savedExitReported);
        localSpans_ = std::move(savedSpans);
        moveStates_ = std::move(savedMoves);
        loanStates_ = std::move(savedLoans);
        typeParameters_ = std::move(savedTypeParameters);
        currentTypeParameterNames_ = std::move(savedTypeParameterNames);
        closureOuterNames_ = std::move(savedClosureOuterNames);

        model_.closureTargets[id] =
            ClosureTarget{closureId, std::move(captureSources), std::move(captureModes), borrowed};
        model_.expressionBorrowedClosures[id] = borrowed;
        return closureType;
    }

    Type analyzeArray(AstExpressionId id, const ArrayExpression &array,
                      std::optional<Type> expected,
                      SourceSpan span) {
        std::optional<Type> expectedElement;
        if (expected.has_value() && expected->kind == TypeKind::Array &&
            expected->arguments.size() == 1) {
            expectedElement = expected->arguments.front();
            if (expected->declaration != array.elements.size()) {
                diagnostics_.error("FDN2082", "array literal length does not match its type",
                                   span);
            }
        }
        if (array.elements.empty()) {
            if (!expectedElement.has_value()) {
                diagnostics_.error("FDN2081", "empty array literal requires an array type", span);
                return invalidType;
            }
            return *expected;
        }

        auto element = analyzeExpression(array.elements.front(), expectedElement);
        auto borrowedClosure = model_.expressionBorrowedClosures[array.elements.front()];
        for (std::size_t index = 1; index < array.elements.size(); ++index) {
            const auto current = analyzeExpression(array.elements[index], element);
            borrowedClosure =
                borrowedClosure || model_.expressionBorrowedClosures[array.elements[index]];
            requireSame(element, current, program_.expressions[array.elements[index]].span,
                        "array element");
        }
        if (borrowedClosure) {
            diagnostics_.error("FDN2127", "borrowed closure cannot be stored in an array", span);
            model_.expressionBorrowedClosures[id] = true;
        }
        if (expectedElement.has_value()) {
            requireSame(*expectedElement, element, span, "array element");
            element = *expectedElement;
        }
        return Type{TypeKind::Array, array.elements.size(), {element}};
    }

    Type analyzeIndex(const IndexExpression &index, ExpressionUse use, SourceSpan span) {
        auto base = analyzeExpression(index.base, std::nullopt, ExpressionUse::Inspect);
        requireSame(i32Type,
                    analyzeExpression(index.index, std::nullopt, ExpressionUse::Inspect), span,
                    "index");
        if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
             base.kind == TypeKind::Edit) &&
            base.arguments.size() == 1) {
            const auto sequence = base.arguments.front();
            base = sequence;
        }
        if ((base.kind != TypeKind::Array && base.kind != TypeKind::Slice) ||
            base.arguments.size() != 1) {
            diagnostics_.error("FDN2084", "indexing requires an array or slice", span);
            return invalidType;
        }
        const auto element = base.arguments.front();
        if (use == ExpressionUse::Consume && requiresDrop(element)) {
            diagnostics_.error("FDN2083", "owned array element cannot move independently", span);
        }
        return element;
    }

    Type analyzeBinary(const BinaryExpression &binary, SourceSpan span) {
        const auto left = analyzeExpression(binary.left, std::nullopt, ExpressionUse::Inspect);
        const auto movesBeforeRight = moveStates_;
        const auto right = analyzeExpression(binary.right, std::nullopt, ExpressionUse::Inspect);
        if (binary.operation == BinaryOperator::And || binary.operation == BinaryOperator::Or) {
            std::vector<MoveState> merged(movesBeforeRight.size());
            for (std::size_t local = 0; local < merged.size(); ++local) {
                merged[local] = movesBeforeRight[local] == moveStates_[local]
                                    ? movesBeforeRight[local]
                                    : MoveState::MaybeMoved;
            }
            restoreMoves(merged);
        }
        switch (binary.operation) {
        case BinaryOperator::Add:
            if (left == stringType || right == stringType) {
                requireSame(stringType, left, span, "string concatenation operand");
                requireSame(stringType, right, span, "string concatenation operand");
                return stringType;
            }
            requireSame(i32Type, left, span, "arithmetic operand");
            requireSame(i32Type, right, span, "arithmetic operand");
            return i32Type;
        case BinaryOperator::Subtract:
        case BinaryOperator::Multiply:
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:
            requireSame(i32Type, left, span, "arithmetic operand");
            requireSame(i32Type, right, span, "arithmetic operand");
            return i32Type;
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual:
            requireSame(i32Type, left, span, "comparison operand");
            requireSame(i32Type, right, span, "comparison operand");
            return boolType;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            if (left.kind == TypeKind::Parameter || right.kind == TypeKind::Parameter ||
                left.kind == TypeKind::Struct || right.kind == TypeKind::Struct ||
                left.kind == TypeKind::Enum || right.kind == TypeKind::Enum ||
                left.kind == TypeKind::Function || right.kind == TypeKind::Function) {
                diagnostics_.error("FDN2012", "equality is not available for this type", span);
            } else {
                requireSame(left, right, span, "equality operand");
                if (left == voidType) {
                    diagnostics_.error("FDN2012", "void values cannot be compared", span);
                }
            }
            return boolType;
        case BinaryOperator::And:
        case BinaryOperator::Or:
            requireSame(boolType, left, span, "logical operand");
            requireSame(boolType, right, span, "logical operand");
            return boolType;
        }
        return invalidType;
    }

    Type analyzeCall(AstExpressionId id, const CallExpression &call, SourceSpan span) {
        const auto loansBefore = loanStates_;
        const auto borrowsAllowedBefore = transientBorrowsAllowed_;
        transientBorrowsAllowed_ = true;
        std::vector<Type> arguments;
        arguments.reserve(call.arguments.size());
        const auto inspectsArguments = call.callee == "print" || call.callee == "panic";
        for (const auto argument : call.arguments) {
            arguments.push_back(analyzeExpression(
                argument, std::nullopt,
                inspectsArguments ? ExpressionUse::Inspect : ExpressionUse::Consume));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;
        std::vector<Type> explicitTypes;
        explicitTypes.reserve(call.typeArguments.size());
        for (const auto &argument : call.typeArguments) {
            explicitTypes.push_back(resolveType(argument));
        }

        if (const auto local = lookupLocal(call.callee); local.has_value()) {
            auto functionType = model_.functions[currentFunction_].locals[*local].type;
            if ((functionType.kind == TypeKind::View || functionType.kind == TypeKind::Edit) &&
                functionType.arguments.size() == 1) {
                const auto callable = functionType.arguments.front();
                functionType = callable;
            }
            if (functionType.kind != TypeKind::Function || functionType.arguments.empty()) {
                diagnostics_.error("FDN2128", call.callee + " is not callable", span);
                return invalidType;
            }
            if (!explicitTypes.empty()) {
                diagnostics_.error("FDN2043", "function value call does not accept type arguments",
                                   span);
            }
            const auto parameterCount = functionType.arguments.size() - 1;
            if (arguments.size() != parameterCount) {
                diagnostics_.error("FDN2010", "wrong argument count for " + call.callee, span);
            }
            const auto count = std::min(arguments.size(), parameterCount);
            for (std::size_t index = 0; index < count; ++index) {
                requireSame(functionType.arguments[index + 1], arguments[index], span,
                            "function value argument");
                if (model_.expressionBorrowedClosures[call.arguments[index]] &&
                    functionType.arguments[index + 1].kind != TypeKind::View &&
                    functionType.arguments[index + 1].kind != TypeKind::Edit) {
                    diagnostics_.error("FDN2127", "borrowed closure cannot escape", span);
                }
            }
            CallTarget target;
            target.kind = CallTargetKind::FunctionValue;
            target.local = *local;
            model_.callTargets[id] = std::move(target);
            return functionType.arguments.front();
        }

        if (call.callee == "print") {
            if (!explicitTypes.empty()) {
                diagnostics_.error("FDN2043", "print does not accept type arguments", span);
            }
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "print expects one argument", span);
            } else {
                requireSame(stringType, arguments.front(), span, "print argument");
            }
            std::vector<bool> drops;
            drops.reserve(call.arguments.size());
            for (std::size_t index = 0; index < call.arguments.size(); ++index) {
                drops.push_back(index < arguments.size() && requiresDrop(arguments[index]) &&
                                !isPlaceExpression(call.arguments[index]));
            }
            CallTarget target;
            target.kind = CallTargetKind::Print;
            target.argumentDrops = std::move(drops);
            model_.callTargets[id] = std::move(target);
            return voidType;
        }
        if (call.callee == "panic") {
            if (!explicitTypes.empty()) {
                diagnostics_.error("FDN2043", "panic does not accept type arguments", span);
            }
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "panic expects one argument", span);
            } else {
                requireSame(stringType, arguments.front(), span, "panic argument");
            }
            CallTarget target;
            target.kind = CallTargetKind::Panic;
            model_.callTargets[id] = std::move(target);
            return voidType;
        }

        auto found = functions_.find(call.callee);
        if (found == functions_.end() && call.callee.find('.') == std::string::npos &&
            !currentPackage().empty()) {
            found = functions_.find(std::string(currentPackage()) + '.' + call.callee);
        }
        if (found == functions_.end()) {
            diagnostics_.error("FDN2009", "unknown function " + call.callee, span);
            return invalidType;
        }
        const auto function = found->second;
        if (program_.functions[function].name == "main") {
            diagnostics_.error("FDN2019", "main cannot be called", span);
        }
        const auto &signature = signatures_[function];
        if (arguments.size() != signature.parameters.size()) {
            diagnostics_.error("FDN2010", "wrong argument count for " + call.callee, span);
        }
        std::vector<std::optional<Type>> inferred(
            program_.functions[function].typeParameters.size());
        if (!explicitTypes.empty()) {
            if (explicitTypes.size() != inferred.size()) {
                diagnostics_.error("FDN2043",
                                   "wrong type argument count for function " + call.callee, span);
            }
            for (std::size_t index = 0;
                 index < explicitTypes.size() && index < inferred.size(); ++index) {
                inferred[index] = explicitTypes[index];
            }
        }
        const auto count = std::min(arguments.size(), signature.parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            inferType(signature.parameters[index], arguments[index], inferred, span,
                      "function argument");
        }
        const auto typeArguments = completeInference(
            inferred, program_.functions[function].typeParameters, span, call.callee);
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto expected = substitute(signature.parameters[index], typeArguments);
            if (const auto conversion = contractConversion(expected, arguments[index]);
                conversion.has_value()) {
                conversions[index] = *conversion;
            } else {
                requireSame(expected, arguments[index], span, "function argument");
            }
            if (model_.expressionBorrowedClosures[call.arguments[index]] &&
                expected.kind != TypeKind::View && expected.kind != TypeKind::Edit) {
                diagnostics_.error("FDN2127", "borrowed closure cannot escape", span);
            }
        }
        CallTarget target;
        target.kind = CallTargetKind::Function;
        target.function = function;
        target.typeArguments = typeArguments;
        target.argumentConversions = std::move(conversions);
        model_.callTargets[id] = std::move(target);
        if (!program_.functions[currentFunction_].typeParameters.empty() &&
            !program_.functions[function].typeParameters.empty()) {
            genericCalls_.push_back({currentFunction_, function, typeArguments, span});
        }
        return substitute(signature.returnType, typeArguments);
    }

    void rejectPolymorphicRecursion() {
        std::vector<std::vector<FirFunctionId>> edges(program_.functions.size());
        for (const auto &call : genericCalls_) {
            edges[call.from].push_back(call.to);
        }
        for (const auto &call : genericCalls_) {
            std::vector<bool> seen(program_.functions.size());
            std::vector<FirFunctionId> pending{call.to};
            bool cyclic = false;
            while (!pending.empty()) {
                const auto function = pending.back();
                pending.pop_back();
                if (function == call.from) {
                    cyclic = true;
                    break;
                }
                if (seen[function]) {
                    continue;
                }
                seen[function] = true;
                pending.insert(pending.end(), edges[function].begin(), edges[function].end());
            }
            if (!cyclic) {
                continue;
            }

            const auto parameterCount = program_.functions[call.from].typeParameters.size();
            bool identity = call.arguments.size() == parameterCount;
            for (std::size_t index = 0; identity && index < call.arguments.size(); ++index) {
                identity = call.arguments[index].kind == TypeKind::Parameter &&
                           call.arguments[index].declaration == index &&
                           call.arguments[index].arguments.empty();
            }
            if (!identity) {
                diagnostics_.error("FDN2046", "polymorphic recursion is not supported", call.span);
            }
        }
    }

    void rejectVoidApplications() {
        std::vector<std::pair<Type, SourceSpan>> roots;
        for (std::size_t type = 0; type < model_.structs.size(); ++type) {
            for (std::size_t field = 0; field < model_.structs[type].fieldTypes.size(); ++field) {
                roots.push_back({model_.structs[type].fieldTypes[field],
                                 program_.structs[type].fields[field].span});
            }
        }
        for (std::size_t type = 0; type < model_.enums.size(); ++type) {
            for (std::size_t variant = 0; variant < model_.enums[type].payloadTypes.size();
                 ++variant) {
                if (model_.enums[type].payloadTypes[variant].has_value()) {
                    roots.push_back({*model_.enums[type].payloadTypes[variant],
                                     program_.enums[type].variants[variant].span});
                }
            }
        }
        for (std::size_t function = 0; function < model_.functions.size(); ++function) {
            roots.push_back(
                {model_.functions[function].returnType, program_.functions[function].span});
            for (std::size_t parameter = 0;
                 parameter < model_.functions[function].parameterTypes.size(); ++parameter) {
                roots.push_back({model_.functions[function].parameterTypes[parameter],
                                 program_.functions[function].parameters[parameter].span});
            }
            for (const auto &local : model_.functions[function].locals) {
                roots.push_back({local.type, program_.functions[function].span});
            }
        }
        for (std::size_t expression = 0; expression < model_.expressionTypes.size(); ++expression) {
            roots.push_back(
                {model_.expressionTypes[expression], program_.expressions[expression].span});
            if (!model_.callTargets[expression].has_value() ||
                model_.callTargets[expression]->kind != CallTargetKind::Function) {
                continue;
            }
            const auto &target = *model_.callTargets[expression];
            const auto &signature = signatures_[target.function];
            for (const auto &parameter : signature.parameters) {
                const auto concrete = substitute(parameter, target.typeArguments);
                if (concrete == voidType) {
                    diagnostics_.error("FDN2016", "generic parameter cannot become void",
                                       program_.expressions[expression].span);
                } else {
                    roots.push_back({concrete, program_.expressions[expression].span});
                }
            }
        }

        std::unordered_set<std::string> ownershipValidated;
        for (const auto &[root, span] : roots) {
            std::vector<std::pair<Type, SourceSpan>> pending{{root, span}};
            while (!pending.empty()) {
                const auto [type, typeSpan] = pending.back();
                pending.pop_back();
                const auto key = semanticTypeKey(type);
                if (!ownershipValidated.insert(key).second) {
                    continue;
                }
                if (type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                    type.kind == TypeKind::Edit) {
                    if (type.arguments.size() != 1) {
                        continue;
                    }
                    const auto &target = type.arguments.front();
                    if (target == voidType || target.kind == TypeKind::Own ||
                        target.kind == TypeKind::View || target.kind == TypeKind::Edit) {
                        diagnostics_.error("FDN2064",
                                           std::string(typeName(type)) +
                                               " requires a direct non-void value type",
                                           typeSpan);
                    } else if (target.kind != TypeKind::Invalid) {
                        pending.push_back({target, typeSpan});
                    }
                    continue;
                }
                if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
                    for (const auto &[child, childSpan] : layoutChildren(type)) {
                        pending.push_back({child, childSpan});
                    }
                }
            }
        }

        struct Frame {
            Type type;
            std::string key;
            std::vector<std::pair<Type, SourceSpan>> children;
            std::size_t next{};
        };
        std::unordered_set<std::string> validated;
        for (const auto &[root, span] : roots) {
            if (root.kind != TypeKind::Struct && root.kind != TypeKind::Enum) {
                continue;
            }
            const auto rootKey = semanticTypeKey(root);
            if (validated.contains(rootKey)) {
                continue;
            }
            std::vector<Frame> stack;
            stack.push_back({root, rootKey, layoutChildren(root), 0});
            while (!stack.empty()) {
                auto &frame = stack.back();
                if (frame.next == frame.children.size()) {
                    validated.insert(frame.key);
                    stack.pop_back();
                    continue;
                }
                const auto [child, childSpan] = frame.children[frame.next++];
                if (child == voidType && !frame.type.arguments.empty()) {
                    diagnostics_.error("FDN2047",
                                       "type application produces a void field or payload",
                                       childSpan.length == 0 ? span : childSpan);
                    continue;
                }
                if (child.kind != TypeKind::Struct && child.kind != TypeKind::Enum) {
                    continue;
                }
                const auto key = semanticTypeKey(child);
                if (!validated.contains(key)) {
                    stack.push_back({child, key, layoutChildren(child), 0});
                }
            }
        }
    }

    Type analyzeStruct(AstExpressionId id, const StructExpression &literal, SourceSpan span) {
        const auto found = structs_.find(literal.type.name);
        if (found == structs_.end()) {
            for (const auto &field : literal.fields) {
                static_cast<void>(analyzeExpression(field.value));
            }
            diagnostics_.error("FDN2024", "unknown struct " + literal.type.name, span);
            return invalidType;
        }

        const auto type = found->second;
        const auto &declaration = program_.structs[type];
        const auto &semantic = model_.structs[type];
        std::vector<std::optional<Type>> inferred(declaration.typeParameters.size());
        std::vector<Type> explicitArguments;
        if (!literal.type.arguments.empty() || declaration.typeParameters.empty()) {
            const auto resolved = resolveType(literal.type);
            explicitArguments = resolved.arguments;
            for (std::size_t index = 0;
                 index < explicitArguments.size() && index < inferred.size(); ++index) {
                inferred[index] = explicitArguments[index];
            }
        }
        std::vector<bool> initialized(declaration.fields.size());
        std::vector<FirFieldId> fields;
        std::vector<Type> values;
        fields.reserve(literal.fields.size());
        for (const auto &initializer : literal.fields) {
            const auto field = findField(type, initializer.name);
            if (!field.has_value()) {
                static_cast<void>(analyzeExpression(initializer.value));
                diagnostics_.error("FDN2025", "unknown field " + initializer.name,
                                   initializer.span);
                continue;
            }
            if (declaration.packageName != currentPackage() &&
                !declaration.fields[*field].exported) {
                diagnostics_.error("FDN3008", "field " + initializer.name + " is not exported",
                                   initializer.span);
            }
            if (initialized[*field]) {
                static_cast<void>(analyzeExpression(initializer.value));
                diagnostics_.error("FDN2026", "duplicate field initializer " + initializer.name,
                                   initializer.span);
                continue;
            }
            std::vector<Type> knownArguments;
            knownArguments.reserve(inferred.size());
            auto complete = true;
            for (const auto &argument : inferred) {
                complete = complete && argument.has_value();
                knownArguments.push_back(argument.value_or(invalidType));
            }
            const auto expectedField = complete
                                           ? std::optional<Type>{substitute(
                                                 semantic.fieldTypes[*field], knownArguments)}
                                           : std::nullopt;
            const auto value = analyzeExpression(initializer.value, expectedField);
            if (model_.expressionBorrowedClosures[initializer.value]) {
                diagnostics_.error("FDN2127", "borrowed closure cannot be stored in a struct",
                                   initializer.span);
                model_.expressionBorrowedClosures[id] = true;
            }
            initialized[*field] = true;
            fields.push_back(*field);
            values.push_back(value);
            inferType(semantic.fieldTypes[*field], value, inferred, initializer.span,
                      "field initializer");
        }
        for (std::size_t field = 0; field < initialized.size(); ++field) {
            if (!initialized[field]) {
                diagnostics_.error("FDN2027", "missing field " + declaration.fields[field].name,
                                   span);
            }
        }
        const auto arguments = completeInference(inferred, declaration.typeParameters, span,
                                                 declaration.name);
        for (std::size_t index = 0; index < fields.size() && index < values.size(); ++index) {
            requireSame(substitute(semantic.fieldTypes[fields[index]], arguments), values[index],
                        span, "field initializer");
        }
        Type result{TypeKind::Struct, type, arguments};
        model_.structTargets[id] = StructLiteralTarget{result, std::move(fields)};
        return result;
    }

    Type analyzeMember(AstExpressionId id, const MemberExpression &member,
                       std::optional<Type> expected, ExpressionUse use, SourceSpan span) {
        if (!member.base.has_value()) {
            return analyzeEnum(id, member, std::nullopt, expected, span);
        }

        const auto &baseExpression = program_.expressions[*member.base];
        if (const auto *name = std::get_if<NameExpression>(&baseExpression.value);
            name != nullptr && !lookupLocal(name->name).has_value()) {
            auto enumName = name->name;
            if (!enums_.contains(enumName) && enumName.find('.') == std::string::npos &&
                !currentPackage().empty()) {
                enumName = std::string(currentPackage()) + '.' + enumName;
            }
            if (enums_.contains(enumName)) {
                return analyzeEnum(
                    id, member,
                    TypeSyntax{std::move(enumName), name->typeArguments, baseExpression.span},
                    expected, span);
            }
        }

        const auto sourceType =
            analyzeExpression(*member.base, std::nullopt, ExpressionUse::Inspect);
        if (sourceType.kind == TypeKind::Invalid) {
            return invalidType;
        }
        auto base = sourceType;
        if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
             base.kind == TypeKind::Edit) &&
            base.arguments.size() == 1) {
            const auto value = base.arguments.front();
            base = value;
        }
        if (member.invoked) {
            return analyzeMethod(id, member, sourceType, base, span);
        }
        if (base.kind != TypeKind::Struct || base.declaration >= program_.structs.size()) {
            diagnostics_.error("FDN2028", "field access requires a struct value", span);
            return invalidType;
        }
        const auto resolved = findField(base.declaration, member.member);
        if (!resolved.has_value()) {
            diagnostics_.error("FDN2025", "unknown field " + member.member, span);
            return invalidType;
        }
        const auto &declaration = program_.structs[base.declaration];
        if (declaration.packageName != currentPackage() &&
            !declaration.fields[*resolved].exported) {
            diagnostics_.error("FDN3008", "field " + member.member + " is not exported", span);
        }
        model_.expressionFields[id] = *resolved;
        const auto result =
            substitute(model_.structs[base.declaration].fieldTypes[*resolved], base.arguments);
        if (use == ExpressionUse::Consume && requiresDrop(result)) {
            diagnostics_.error("FDN2078", "owned field cannot move independently", span);
        }
        return result;
    }

    Type analyzeMethod(AstExpressionId id, const MemberExpression &member,
                       const Type &sourceType, const Type &base, SourceSpan span) {
        if (!member.typeArguments.empty()) {
            diagnostics_.error("FDN2043", "method does not accept type arguments", span);
        }
        if (base.kind == TypeKind::Contract) {
            return analyzeContractMethod(id, member, sourceType, base, span);
        }
        if (base.kind != TypeKind::Struct || base.declaration >= methods_.size()) {
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2050", "method call requires a struct or contract", span);
            return invalidType;
        }
        const auto found = methods_[base.declaration].find(member.member);
        if (found == methods_[base.declaration].end()) {
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2100", "unknown method " + member.member, span);
            return invalidType;
        }

        const auto function = found->second;
        const auto &declaration = program_.functions[function];
        if (member.member == "drop") {
            diagnostics_.error("FDN2138", "drop is called only by deterministic cleanup",
                               span);
        }
        if (declaration.packageName != currentPackage() && !declaration.exported) {
            diagnostics_.error("FDN3008", "method " + member.member + " is not exported", span);
        }
        const auto access = *declaration.receiver;
        if (access == ReceiverKind::Edit &&
            (sourceType.kind == TypeKind::View || !editablePlace(*member.base))) {
            diagnostics_.error("FDN2101", "edit method requires an editable receiver", span);
        }
        if (access == ReceiverKind::Own) {
            if (sourceType.kind != TypeKind::Own ||
                !std::holds_alternative<NameExpression>(
                    program_.expressions[*member.base].value)) {
                diagnostics_.error("FDN2102", "own method requires an owned binding", span);
            } else if (const auto local = model_.expressionLocals[*member.base];
                       local.has_value()) {
                if (loanStates_[*local] != LoanState::None) {
                    diagnostics_.error("FDN2067", "cannot move borrowed binding " +
                                                        model_.functions[currentFunction_]
                                                            .locals[*local]
                                                            .name,
                                       span);
                }
                moveStates_[*local] = MoveState::Moved;
                model_.expressionMoves[*member.base] = true;
            }
        }

        const auto loansBefore = loanStates_;
        const auto borrowsAllowedBefore = transientBorrowsAllowed_;
        transientBorrowsAllowed_ = true;
        std::vector<Type> arguments;
        arguments.reserve(member.arguments.size());
        for (const auto argument : member.arguments) {
            arguments.push_back(analyzeExpression(argument));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;

        const auto &signature = signatures_[function];
        const auto parameterCount = signature.parameters.empty()
                                        ? std::size_t{}
                                        : signature.parameters.size() - 1;
        if (arguments.size() != parameterCount) {
            diagnostics_.error("FDN2010", "wrong argument count for method " + member.member,
                               span);
        }
        const auto count = std::min(arguments.size(), parameterCount);
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto expected = substitute(signature.parameters[index + 1], base.arguments);
            if (const auto conversion = contractConversion(expected, arguments[index]);
                conversion.has_value()) {
                conversions[index] = *conversion;
            } else {
                requireSame(expected, arguments[index], span, "method argument");
            }
        }

        CallTarget target;
        target.kind = CallTargetKind::Method;
        target.function = function;
        target.typeArguments = base.arguments;
        target.receiver = *member.base;
        target.receiverType = substitute(signature.parameters.front(), base.arguments);
        target.argumentConversions = std::move(conversions);
        model_.callTargets[id] = std::move(target);
        return substitute(signature.returnType, base.arguments);
    }

    Type analyzeContractMethod(AstExpressionId id, const MemberExpression &member,
                               const Type &sourceType, const Type &base, SourceSpan span) {
        if (base.declaration >= program_.contracts.size()) {
            return invalidType;
        }
        const auto &declaration = program_.contracts[base.declaration];
        std::optional<std::size_t> methodIndex;
        for (std::size_t index = 0; index < declaration.methods.size(); ++index) {
            if (declaration.methods[index].name == member.member) {
                methodIndex = index;
                break;
            }
        }
        if (!methodIndex.has_value()) {
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2100", "unknown method " + member.member, span);
            return invalidType;
        }
        const auto &method = declaration.methods[*methodIndex];
        const auto &semantic = model_.contracts[base.declaration].methods[*methodIndex];
        if (declaration.packageName != currentPackage() && !method.exported) {
            diagnostics_.error("FDN3008", "method " + member.member + " is not exported", span);
        }
        if (method.receiver == ReceiverKind::Edit && sourceType.kind != TypeKind::Edit) {
            diagnostics_.error("FDN2101", "edit method requires an editable receiver", span);
        }
        if (method.receiver == ReceiverKind::Own) {
            diagnostics_.error("FDN2103", "own method cannot use a borrowed contract value",
                               span);
        }

        const auto loansBefore = loanStates_;
        const auto borrowsAllowedBefore = transientBorrowsAllowed_;
        transientBorrowsAllowed_ = true;
        std::vector<Type> arguments;
        arguments.reserve(member.arguments.size());
        for (const auto argument : member.arguments) {
            arguments.push_back(analyzeExpression(argument));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;
        if (arguments.size() != semantic.parameterTypes.size()) {
            diagnostics_.error("FDN2010", "wrong argument count for method " + member.member,
                               span);
        }
        const auto count = std::min(arguments.size(), semantic.parameterTypes.size());
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto expected = substitute(semantic.parameterTypes[index], base.arguments);
            if (const auto conversion = contractConversion(expected, arguments[index]);
                conversion.has_value()) {
                conversions[index] = *conversion;
            } else {
                requireSame(expected, arguments[index], span, "method argument");
            }
        }

        CallTarget target;
        target.kind = CallTargetKind::ContractMethod;
        target.receiver = *member.base;
        target.receiverType = sourceType;
        target.contract = base.declaration;
        target.method = *methodIndex;
        target.typeArguments = base.arguments;
        target.argumentConversions = std::move(conversions);
        model_.callTargets[id] = std::move(target);
        return substitute(semantic.returnType, base.arguments);
    }

    bool editablePlace(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (std::holds_alternative<NameExpression>(expression.value)) {
            if (id >= model_.expressionTypes.size()) {
                return false;
            }
            if (model_.expressionTypes[id].kind == TypeKind::Edit) {
                return true;
            }
            const auto local = model_.expressionLocals[id];
            return local.has_value() &&
                   model_.functions[currentFunction_].locals[*local].mutableBinding;
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value);
            member != nullptr && member->base.has_value()) {
            return editablePlace(*member->base);
        }
        if (const auto *index = std::get_if<IndexExpression>(&expression.value)) {
            return editablePlace(index->base);
        }
        return false;
    }

    Type placeContextType(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            const auto local = lookupLocal(name->name);
            return local.has_value()
                       ? model_.functions[currentFunction_].locals[*local].type
                       : invalidType;
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value);
            member != nullptr && member->base.has_value()) {
            auto base = placeContextType(*member->base);
            if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
                 base.kind == TypeKind::Edit) &&
                base.arguments.size() == 1) {
                base = base.arguments.front();
            }
            if (base.kind != TypeKind::Struct || base.declaration >= model_.structs.size()) {
                return invalidType;
            }
            const auto field = findField(base.declaration, member->member);
            return field.has_value()
                       ? substitute(model_.structs[base.declaration].fieldTypes[*field],
                                    base.arguments)
                       : invalidType;
        }
        if (const auto *index = std::get_if<IndexExpression>(&expression.value)) {
            auto base = placeContextType(index->base);
            if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
                 base.kind == TypeKind::Edit) &&
                base.arguments.size() == 1) {
                base = base.arguments.front();
            }
            if ((base.kind == TypeKind::Array || base.kind == TypeKind::Slice) &&
                base.arguments.size() == 1) {
                return base.arguments.front();
            }
        }
        return invalidType;
    }

    std::optional<FirLocalId> placeRootLocal(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            if (id < model_.expressionLocals.size() && model_.expressionLocals[id].has_value()) {
                return model_.expressionLocals[id];
            }
            return lookupLocal(name->name);
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value);
            member != nullptr && member->base.has_value()) {
            return placeRootLocal(*member->base);
        }
        if (const auto *index = std::get_if<IndexExpression>(&expression.value)) {
            return placeRootLocal(index->base);
        }
        return std::nullopt;
    }

    bool isPlaceExpression(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (std::holds_alternative<NameExpression>(expression.value)) {
            return true;
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            return member->base.has_value() && isPlaceExpression(*member->base);
        }
        if (const auto *index = std::get_if<IndexExpression>(&expression.value)) {
            return isPlaceExpression(index->base);
        }
        return false;
    }

    Type analyzeEnum(AstExpressionId id, const MemberExpression &constructor,
                     std::optional<TypeSyntax> explicitType, std::optional<Type> contextualType,
                     SourceSpan span) {
        if (!constructor.typeArguments.empty()) {
            diagnostics_.error("FDN2043", "enum variant does not accept type arguments", span);
        }
        std::optional<FirEnumId> enumType;
        if (explicitType.has_value()) {
            const auto found = enums_.find(explicitType->name);
            if (found != enums_.end()) {
                enumType = found->second;
            }
        } else if (contextualType.has_value() && contextualType->kind == TypeKind::Enum &&
                   contextualType->declaration < program_.enums.size()) {
            enumType = contextualType->declaration;
        }
        if (!enumType.has_value()) {
            for (const auto argument : constructor.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2034", "cannot resolve enum for ." + constructor.member,
                               span);
            return invalidType;
        }
        const auto variant = findVariant(*enumType, constructor.member);
        if (!variant.has_value()) {
            for (const auto argument : constructor.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2035", "unknown variant " + constructor.member, span);
            return invalidType;
        }

        const auto &declaration = program_.enums[*enumType];
        if (declaration.packageName != currentPackage() &&
            !declaration.variants[*variant].exported) {
            diagnostics_.error("FDN3008", "variant " + constructor.member + " is not exported",
                               span);
        }
        std::vector<std::optional<Type>> inferred(declaration.typeParameters.size());
        if (explicitType.has_value() &&
            (!explicitType->arguments.empty() || declaration.typeParameters.empty())) {
            const auto resolved = resolveType(*explicitType);
            for (std::size_t index = 0;
                 index < resolved.arguments.size() && index < inferred.size(); ++index) {
                inferred[index] = resolved.arguments[index];
            }
        } else if (contextualType.has_value() && contextualType->kind == TypeKind::Enum &&
                   contextualType->declaration == *enumType) {
            for (std::size_t index = 0;
                 index < contextualType->arguments.size() && index < inferred.size(); ++index) {
                inferred[index] = contextualType->arguments[index];
            }
        }
        const auto payloadPattern = model_.enums[*enumType].payloadTypes[*variant];
        std::optional<Type> payloadType;
        if (payloadPattern.has_value() && constructor.arguments.empty()) {
            diagnostics_.error("FDN2036", "variant requires a payload", span);
        } else if (!payloadPattern.has_value() && !constructor.arguments.empty()) {
            for (const auto argument : constructor.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2036", "unit variant does not accept a payload", span);
        } else if (payloadPattern.has_value() && constructor.arguments.size() == 1) {
            std::vector<Type> knownArguments;
            knownArguments.reserve(inferred.size());
            auto complete = true;
            for (const auto &argument : inferred) {
                complete = complete && argument.has_value();
                knownArguments.push_back(argument.value_or(invalidType));
            }
            const auto expectedPayload = complete
                                             ? std::optional<Type>{substitute(*payloadPattern,
                                                                              knownArguments)}
                                             : std::nullopt;
            payloadType = analyzeExpression(constructor.arguments.front(), expectedPayload);
            if (model_.expressionBorrowedClosures[constructor.arguments.front()]) {
                diagnostics_.error("FDN2127", "borrowed closure cannot be stored in an enum",
                                   span);
                model_.expressionBorrowedClosures[id] = true;
            }
            inferType(*payloadPattern, *payloadType, inferred, span, "variant payload");
        } else if (payloadPattern.has_value()) {
            for (const auto argument : constructor.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2036", "variant accepts one payload", span);
        }
        const auto arguments = completeInference(inferred, declaration.typeParameters, span,
                                                 declaration.name);
        if (payloadPattern.has_value() && payloadType.has_value()) {
            requireSame(substitute(*payloadPattern, arguments), *payloadType, span,
                        "variant payload");
        }
        Type result{TypeKind::Enum, *enumType, arguments};
        model_.enumTargets[id] = EnumTarget{result, *variant};
        return result;
    }

    Type analyzeMatch(AstExpressionId id, const MatchExpression &match,
                      std::optional<Type> expected, SourceSpan span) {
        const auto value = analyzeExpression(match.value);
        if (value.kind != TypeKind::Enum || value.declaration >= program_.enums.size()) {
            for (const auto &arm : match.arms) {
                scopes_.emplace_back();
                static_cast<void>(analyzeExpression(arm.expression));
                scopes_.pop_back();
            }
            diagnostics_.error("FDN2037", "match requires an enum value", span);
            return invalidType;
        }

        const auto enumType = value.declaration;
        const auto &declaration = program_.enums[enumType];
        const auto beforeArms = resultOutstanding_;
        const auto movesBeforeArms = moveStates_;
        const auto loansBeforeArms = loanStates_;
        std::vector<bool> covered(declaration.variants.size());
        std::vector<FirVariantId> variants;
        std::vector<std::optional<FirLocalId>> bindings;
        std::vector<std::vector<FirLocalId>> drops;
        std::vector<std::vector<bool>> armStates;
        std::vector<std::vector<MoveState>> armMoveStates;
        std::vector<std::vector<LoanState>> armLoanStates;
        auto borrowedClosure = false;
        auto result = invalidType;
        for (const auto &arm : match.arms) {
            restoreOutstanding(beforeArms);
            restoreMoves(movesBeforeArms);
            restoreLoans(loansBeforeArms);
            auto variant = findVariant(enumType, arm.variant);
            if (!variant.has_value()) {
                diagnostics_.error("FDN2035", "unknown variant " + arm.variant, arm.span);
            } else if (covered[*variant]) {
                diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                   arm.span);
            } else {
                covered[*variant] = true;
                if (declaration.packageName != currentPackage() &&
                    !declaration.variants[*variant].exported) {
                    diagnostics_.error("FDN3008", "variant " + arm.variant + " is not exported",
                                       arm.span);
                }
            }

            scopes_.emplace_back();
            std::optional<FirLocalId> binding;
            if (variant.has_value()) {
                auto payload = model_.enums[enumType].payloadTypes[*variant];
                if (payload.has_value()) {
                    payload = substitute(*payload, value.arguments);
                }
                if (payload.has_value() && !arm.binding.has_value()) {
                    diagnostics_.error("FDN2041", "payload pattern requires a binding", arm.span);
                } else if (!payload.has_value() && arm.binding.has_value()) {
                    diagnostics_.error("FDN2041", "unit pattern does not accept a binding",
                                       arm.span);
                } else if (payload.has_value()) {
                    binding = addLocal(*arm.binding, *payload, false, arm.span);
                }
            }
            const auto armExpected = expected.has_value()
                                         ? expected
                                         : (result.kind == TypeKind::Invalid
                                                ? std::nullopt
                                                : std::optional<Type>{result});
            const auto armType = analyzeExpression(arm.expression, armExpected);
            borrowedClosure =
                borrowedClosure || model_.expressionBorrowedClosures[arm.expression];
            drops.push_back(scopeDrops(scopes_.back()));
            reportScope(scopes_.back());
            scopes_.pop_back();
            armStates.push_back(outstandingPrefix(beforeArms.size()));
            armMoveStates.push_back(movePrefix(movesBeforeArms.size()));
            armLoanStates.push_back(loanPrefix(loansBeforeArms.size()));
            if (result.kind == TypeKind::Invalid) {
                result = armType;
            } else {
                requireSame(result, armType, arm.span, "match arm");
            }
            variants.push_back(variant.value_or(0));
            bindings.push_back(binding);
        }
        if (!armStates.empty()) {
            std::vector<bool> merged(beforeArms.size());
            for (const auto &state : armStates) {
                for (std::size_t local = 0; local < merged.size(); ++local) {
                    merged[local] = merged[local] || state[local];
                }
            }
            restoreOutstanding(merged);
        } else {
            restoreOutstanding(beforeArms);
        }
        if (!armMoveStates.empty()) {
            std::vector<MoveState> merged(movesBeforeArms.size());
            for (std::size_t local = 0; local < merged.size(); ++local) {
                merged[local] = armMoveStates.front()[local];
                for (std::size_t arm = 1; arm < armMoveStates.size(); ++arm) {
                    if (merged[local] != armMoveStates[arm][local]) {
                        merged[local] = MoveState::MaybeMoved;
                        break;
                    }
                }
            }
            restoreMoves(merged);
        } else {
            restoreMoves(movesBeforeArms);
        }
        if (!armLoanStates.empty()) {
            auto merged = armLoanStates.front();
            for (std::size_t arm = 1; arm < armLoanStates.size(); ++arm) {
                for (std::size_t local = 0; local < merged.size(); ++local) {
                    merged[local] = mergeLoan(merged[local], armLoanStates[arm][local]);
                }
            }
            restoreLoans(merged);
        } else {
            restoreLoans(loansBeforeArms);
        }
        for (std::size_t variant = 0; variant < covered.size(); ++variant) {
            if (!covered[variant]) {
                diagnostics_.error("FDN2040",
                                   "match does not cover " + declaration.variants[variant].name,
                                   span);
            }
        }
        model_.matchTargets[id] = MatchTarget{value, std::move(variants), std::move(bindings),
                                              std::move(drops)};
        model_.expressionBorrowedClosures[id] = borrowedClosure;
        return result;
    }

    void setTypeParameters(const std::vector<std::string> &parameters, SourceSpan span) {
        typeParameters_.clear();
        currentTypeParameterNames_ = parameters;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto &name = parameters[index];
            if (isBuiltinType(name) || structs_.contains(name) || enums_.contains(name) ||
                contracts_.contains(name) ||
                !typeParameters_.emplace(name, index).second) {
                diagnostics_.error("FDN2042", "duplicate or shadowing type parameter " + name,
                                   span);
            }
        }
    }

    Type resolveType(const TypeSyntax &syntax) {
        std::optional<Type> base;
        if (syntax.name == "void") {
            base = voidType;
        } else if (syntax.name == "i32") {
            base = i32Type;
        } else if (syntax.name == "bool") {
            base = boolType;
        } else if (syntax.name == "String") {
            base = stringType;
        } else if (syntax.name == "[array]") {
            base = Type{TypeKind::Array, syntax.arrayLength};
            if (syntax.arrayLength > static_cast<std::size_t>(INT32_MAX)) {
                diagnostics_.error("FDN2080", "array length exceeds the i32 index range",
                                   syntax.span);
            }
        } else if (syntax.name == "[slice]") {
            base = Type{TypeKind::Slice};
        } else if (syntax.name == "own") {
            base = Type{TypeKind::Own};
        } else if (syntax.name == "view") {
            base = Type{TypeKind::View};
        } else if (syntax.name == "edit") {
            base = Type{TypeKind::Edit};
        } else if (syntax.name == "[function]") {
            base = Type{TypeKind::Function};
        } else if (const auto parameter = typeParameters_.find(syntax.name);
                   parameter != typeParameters_.end()) {
            base = Type{TypeKind::Parameter, parameter->second};
        } else if (const auto structFound = structs_.find(syntax.name);
                   structFound != structs_.end()) {
            base = Type{TypeKind::Struct, structFound->second};
        } else if (const auto enumFound = enums_.find(syntax.name);
                   enumFound != enums_.end()) {
            base = Type{TypeKind::Enum, enumFound->second};
        } else if (const auto contractFound = contracts_.find(syntax.name);
                   contractFound != contracts_.end()) {
            base = Type{TypeKind::Contract, contractFound->second};
        }

        std::vector<Type> arguments;
        arguments.reserve(syntax.arguments.size());
        for (const auto &argument : syntax.arguments) {
            arguments.push_back(resolveType(argument));
        }
        if (!base.has_value()) {
            diagnostics_.error("FDN2002", "unknown type " + syntax.name, syntax.span);
            return invalidType;
        }

        std::size_t expected{};
        if (base->kind == TypeKind::Struct) {
            expected = program_.structs[base->declaration].typeParameters.size();
        } else if (base->kind == TypeKind::Enum) {
            expected = program_.enums[base->declaration].typeParameters.size();
        } else if (base->kind == TypeKind::Contract) {
            expected = program_.contracts[base->declaration].typeParameters.size();
        } else if (base->kind == TypeKind::Function) {
            expected = arguments.size();
            if (arguments.empty()) {
                diagnostics_.error("FDN2126", "function type requires a return type",
                                   syntax.span);
            }
            for (std::size_t index = 1; index < arguments.size(); ++index) {
                if (arguments[index] == voidType) {
                    diagnostics_.error("FDN2016", "function parameter cannot have type void",
                                       syntax.span);
                }
            }
        } else if (base->kind == TypeKind::Own || base->kind == TypeKind::View ||
                   base->kind == TypeKind::Edit || base->kind == TypeKind::Array ||
                   base->kind == TypeKind::Slice) {
            expected = 1;
        }
        if (arguments.size() != expected) {
            diagnostics_.error("FDN2043", "wrong type argument count for " + syntax.name,
                               syntax.span);
        }
        base->arguments = std::move(arguments);
        if ((base->kind == TypeKind::Own || base->kind == TypeKind::View ||
             base->kind == TypeKind::Edit) &&
            !base->arguments.empty()) {
            const auto &target = base->arguments.front();
            if (target == voidType || target.kind == TypeKind::View ||
                target.kind == TypeKind::Edit || target.kind == TypeKind::Own) {
                diagnostics_.error("FDN2064",
                                   syntax.name + " requires a direct non-void value type",
                                   syntax.span);
            }
            if (base->kind == TypeKind::Own && target.kind == TypeKind::Slice) {
                diagnostics_.error("FDN2080", "slice cannot be owned directly", syntax.span);
            }
        } else if ((base->kind == TypeKind::Array || base->kind == TypeKind::Slice) &&
                   !base->arguments.empty() && base->arguments.front() == voidType) {
            diagnostics_.error("FDN2047", "array or slice element cannot be void", syntax.span);
        }
        return *base;
    }

    Type substitute(const Type &type, const std::vector<Type> &arguments) const {
        if (type.kind == TypeKind::Parameter) {
            return type.declaration < arguments.size() ? arguments[type.declaration] : invalidType;
        }
        auto result = type;
        for (auto &argument : result.arguments) {
            argument = substitute(argument, arguments);
        }
        return result;
    }

    void inferType(const Type &pattern, const Type &actual,
                   std::vector<std::optional<Type>> &inferred, SourceSpan span,
                   std::string_view context) {
        if (pattern.kind == TypeKind::Invalid || actual.kind == TypeKind::Invalid) {
            return;
        }
        if (pattern.kind == TypeKind::Parameter) {
            if (pattern.declaration >= inferred.size()) {
                diagnostics_.error("FDN2044", "invalid type parameter in inference", span);
            } else if (!inferred[pattern.declaration].has_value()) {
                inferred[pattern.declaration] = actual;
            } else {
                requireSame(*inferred[pattern.declaration], actual, span, context);
            }
            return;
        }
        if ((pattern.kind == TypeKind::View || pattern.kind == TypeKind::Edit) &&
            pattern.arguments.size() == 1 &&
            pattern.arguments.front().kind == TypeKind::Contract &&
            (actual.kind == TypeKind::View || actual.kind == TypeKind::Edit) &&
            actual.arguments.size() == 1 && actual.arguments.front().kind == TypeKind::Struct &&
            (pattern.kind == TypeKind::View || actual.kind == TypeKind::Edit)) {
            const auto implemented = implementedContract(
                actual.arguments.front(), pattern.arguments.front().declaration);
            if (implemented.has_value()) {
                inferType(pattern.arguments.front(), *implemented, inferred, span, context);
                return;
            }
        }
        if ((pattern.kind == TypeKind::Struct || pattern.kind == TypeKind::Enum ||
             pattern.kind == TypeKind::Contract ||
             pattern.kind == TypeKind::Function ||
             pattern.kind == TypeKind::Array || pattern.kind == TypeKind::Slice ||
             pattern.kind == TypeKind::Own || pattern.kind == TypeKind::View ||
             pattern.kind == TypeKind::Edit) &&
            pattern.kind == actual.kind && pattern.declaration == actual.declaration &&
            pattern.arguments.size() == actual.arguments.size()) {
            for (std::size_t index = 0; index < pattern.arguments.size(); ++index) {
                inferType(pattern.arguments[index], actual.arguments[index], inferred, span,
                          context);
            }
            return;
        }
        requireSame(pattern, actual, span, context);
    }

    std::vector<Type> completeInference(const std::vector<std::optional<Type>> &inferred,
                                        const std::vector<std::string> &parameters,
                                        SourceSpan span, std::string_view owner) {
        std::vector<Type> arguments;
        arguments.reserve(inferred.size());
        for (std::size_t index = 0; index < inferred.size(); ++index) {
            if (!inferred[index].has_value()) {
                diagnostics_.error("FDN2045",
                                   "cannot infer type parameter " + parameters[index] + " for " +
                                       std::string(owner),
                                   span);
                arguments.push_back(invalidType);
            } else {
                arguments.push_back(*inferred[index]);
            }
        }
        return arguments;
    }

    bool isBuiltinType(std::string_view name) const {
        return name == "void" || name == "i32" || name == "bool" || name == "String" ||
               name == "Option" || name == "Result" || name == "own" || name == "view" ||
               name == "edit";
    }

    std::string_view currentPackage() const {
        return currentFunction_ < program_.functions.size()
                   ? std::string_view(program_.functions[currentFunction_].packageName)
                   : std::string_view{};
    }

    std::optional<FirFieldId> findField(FirStructId type, std::string_view name) const {
        const auto &fields = program_.structs[type].fields;
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (fields[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<FirVariantId> findVariant(FirEnumId type, std::string_view name) const {
        const auto &variants = program_.enums[type].variants;
        for (std::size_t index = 0; index < variants.size(); ++index) {
            if (variants[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<Type> implementedContract(const Type &concrete,
                                            std::size_t contract) const {
        if (concrete.kind != TypeKind::Struct || concrete.declaration >= model_.structs.size()) {
            return std::nullopt;
        }
        for (const auto &implementation :
             model_.structs[concrete.declaration].implementations) {
            if (implementation.kind == TypeKind::Contract &&
                implementation.declaration == contract) {
                return substitute(implementation, concrete.arguments);
            }
        }
        return std::nullopt;
    }

    std::optional<CallTarget::ContractConversion>
    contractConversion(const Type &expected, const Type &actual) const {
        if ((expected.kind != TypeKind::View && expected.kind != TypeKind::Edit) ||
            expected.arguments.size() != 1 ||
            expected.arguments.front().kind != TypeKind::Contract ||
            (actual.kind != TypeKind::View && actual.kind != TypeKind::Edit) ||
            actual.arguments.size() != 1 || actual.arguments.front().kind != TypeKind::Struct ||
            (expected.kind == TypeKind::Edit && actual.kind != TypeKind::Edit)) {
            return std::nullopt;
        }
        const auto concrete = actual.arguments.front();
        const auto contract = expected.arguments.front();
        const auto implemented = implementedContract(concrete, contract.declaration);
        if (!implemented.has_value() || *implemented != contract) {
            return std::nullopt;
        }

        CallTarget::ContractConversion conversion;
        conversion.concreteType = concrete;
        conversion.contractType = contract;
        conversion.targetType = expected;
        const auto &methods = program_.contracts[contract.declaration].methods;
        conversion.methods.reserve(methods.size());
        for (const auto &method : methods) {
            const auto found = methods_[concrete.declaration].find(method.name);
            if (found == methods_[concrete.declaration].end()) {
                return std::nullopt;
            }
            conversion.methods.push_back(found->second);
        }
        return conversion;
    }

    FirLocalId addLocal(const std::string &name, Type type, bool mutableBinding, SourceSpan span) {
        auto &scope = scopes_.back();
        if (scope.contains(name)) {
            diagnostics_.error("FDN2003", "duplicate binding " + name, span);
            return scope[name];
        }

        auto &locals = model_.functions[currentFunction_].locals;
        const auto id = locals.size();
        locals.push_back({name, type, mutableBinding});
        resultOutstanding_.push_back(isResult(type));
        resultExitReported_.push_back(false);
        localSpans_.push_back(span);
        moveStates_.push_back(MoveState::Available);
        loanStates_.push_back(LoanState::None);
        scope.emplace(name, id);
        return id;
    }

    bool isResult(const Type &type) const {
        return type.kind == TypeKind::Enum && type.declaration < program_.enums.size() &&
               program_.enums[type.declaration].builtin == BuiltinEnumKind::Result;
    }

    bool requiresDrop(const Type &type) const {
        std::unordered_set<std::string> active;
        return requiresDrop(type, active);
    }

    bool requiresDrop(const Type &type, std::unordered_set<std::string> &active) const {
        if (type.kind == TypeKind::String || type.kind == TypeKind::Own ||
            type.kind == TypeKind::Function ||
            type.kind == TypeKind::Parameter) {
            return true;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return requiresDrop(type.arguments.front(), active);
        }
        if (type.kind != TypeKind::Struct && type.kind != TypeKind::Enum) {
            return false;
        }
        const auto key = semanticTypeKey(type);
        if (!active.insert(key).second) {
            return false;
        }
        if (type.kind == TypeKind::Struct && type.declaration < model_.structs.size()) {
            if (type.declaration < methods_.size() &&
                methods_[type.declaration].contains("drop")) {
                active.erase(key);
                return true;
            }
            for (const auto &field : model_.structs[type.declaration].fieldTypes) {
                if (requiresDrop(substitute(field, type.arguments), active)) {
                    active.erase(key);
                    return true;
                }
            }
        } else if (type.kind == TypeKind::Enum && type.declaration < model_.enums.size()) {
            for (const auto &payload : model_.enums[type.declaration].payloadTypes) {
                if (payload.has_value() &&
                    requiresDrop(substitute(*payload, type.arguments), active)) {
                    active.erase(key);
                    return true;
                }
            }
        }
        active.erase(key);
        return false;
    }

    std::vector<FirLocalId>
    scopeDrops(const std::unordered_map<std::string, FirLocalId> &scope) const {
        std::vector<FirLocalId> drops;
        for (const auto &[name, local] : scope) {
            static_cast<void>(name);
            if (local < model_.functions[currentFunction_].locals.size() &&
                !model_.functions[currentFunction_].locals[local].capture &&
                requiresDrop(model_.functions[currentFunction_].locals[local].type)) {
                drops.push_back(local);
            }
        }
        std::sort(drops.begin(), drops.end(), std::greater<>());
        return drops;
    }

    std::vector<FirLocalId> activeDrops() const {
        std::vector<FirLocalId> drops;
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto current = scopeDrops(*scope);
            drops.insert(drops.end(), current.begin(), current.end());
        }
        return drops;
    }

    void reportScope(const std::unordered_map<std::string, FirLocalId> &scope) {
        std::vector<FirLocalId> locals;
        locals.reserve(scope.size());
        for (const auto &[name, local] : scope) {
            static_cast<void>(name);
            locals.push_back(local);
        }
        std::sort(locals.begin(), locals.end());
        for (const auto local : locals) {
            if (local < resultOutstanding_.size() && resultOutstanding_[local]) {
                if (!resultExitReported_[local]) {
                    diagnostics_.error("FDN2052", "Result binding is not handled",
                                       localSpans_[local]);
                    resultExitReported_[local] = true;
                }
                resultOutstanding_[local] = false;
            }
        }
    }

    void reportOutstanding(SourceSpan span) {
        for (std::size_t local = 0; local < resultOutstanding_.size(); ++local) {
            if (!resultOutstanding_[local]) {
                continue;
            }
            if (!resultExitReported_[local]) {
                diagnostics_.error("FDN2052", "Result binding is not handled",
                                   local < localSpans_.size() ? localSpans_[local] : span);
                resultExitReported_[local] = true;
            }
            resultOutstanding_[local] = false;
        }
    }

    std::vector<bool> outstandingPrefix(std::size_t count) const {
        const auto end = std::min(count, resultOutstanding_.size());
        return {resultOutstanding_.begin(), resultOutstanding_.begin() + end};
    }

    void restoreOutstanding(const std::vector<bool> &state) {
        if (resultOutstanding_.size() < state.size()) {
            resultOutstanding_.resize(state.size());
        }
        for (std::size_t local = 0; local < state.size(); ++local) {
            resultOutstanding_[local] = state[local];
        }
        for (std::size_t local = state.size(); local < resultOutstanding_.size(); ++local) {
            resultOutstanding_[local] = false;
        }
    }

    std::vector<MoveState> movePrefix(std::size_t count) const {
        const auto end = std::min(count, moveStates_.size());
        return {moveStates_.begin(), moveStates_.begin() + end};
    }

    void restoreMoves(const std::vector<MoveState> &state) {
        if (moveStates_.size() < state.size()) {
            moveStates_.resize(state.size(), MoveState::Available);
        }
        for (std::size_t local = 0; local < state.size(); ++local) {
            moveStates_[local] = state[local];
        }
        for (std::size_t local = state.size(); local < moveStates_.size(); ++local) {
            moveStates_[local] = MoveState::Available;
        }
    }

    void restoreLoans(const std::vector<LoanState> &state) {
        if (loanStates_.size() < state.size()) {
            loanStates_.resize(state.size(), LoanState::None);
        }
        for (std::size_t local = 0; local < state.size(); ++local) {
            loanStates_[local] = state[local];
        }
        for (std::size_t local = state.size(); local < loanStates_.size(); ++local) {
            loanStates_[local] = LoanState::None;
        }
    }

    std::vector<LoanState> loanPrefix(std::size_t count) const {
        const auto end = std::min(count, loanStates_.size());
        return {loanStates_.begin(), loanStates_.begin() + end};
    }

    LoanState mergeLoan(LoanState left, LoanState right) const {
        if (left == LoanState::Edit || right == LoanState::Edit) {
            return LoanState::Edit;
        }
        if (left == LoanState::View || right == LoanState::View) {
            return LoanState::View;
        }
        return LoanState::None;
    }

    std::vector<LoanState> mergeLoans(const std::vector<LoanState> &before,
                                      const std::vector<LoanState> &thenState,
                                      const std::vector<LoanState> &elseState, bool thenReturns,
                                      bool elseReturns) const {
        if (thenReturns && elseReturns) {
            return before;
        }
        if (thenReturns) {
            return elseState;
        }
        if (elseReturns) {
            return thenState;
        }
        std::vector<LoanState> merged(before.size());
        for (std::size_t local = 0; local < merged.size(); ++local) {
            merged[local] = mergeLoan(thenState[local], elseState[local]);
        }
        return merged;
    }

    std::vector<MoveState> mergeMoves(const std::vector<MoveState> &before,
                                      const std::vector<MoveState> &thenState,
                                      const std::vector<MoveState> &elseState, bool thenReturns,
                                      bool elseReturns) const {
        if (thenReturns && elseReturns) {
            return before;
        }
        if (thenReturns) {
            return elseState;
        }
        if (elseReturns) {
            return thenState;
        }
        std::vector<MoveState> merged(before.size());
        for (std::size_t local = 0; local < merged.size(); ++local) {
            merged[local] = thenState[local] == elseState[local] ? thenState[local]
                                                                 : MoveState::MaybeMoved;
        }
        return merged;
    }

    std::optional<FirLocalId> lookupLocal(const std::string &name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    std::optional<FirLocalId> findLocal(const std::string &name, SourceSpan span) {
        const auto local = lookupLocal(name);
        if (local.has_value()) {
            return local;
        }
        if (program_.functions[currentFunction_].closure && closureOuterNames_.contains(name)) {
            diagnostics_.error("FDN2120", "binding must be listed after capture " + name,
                               span);
        } else {
            diagnostics_.error("FDN2004", "unknown binding " + name, span);
        }
        return std::nullopt;
    }

    std::string displayType(const Type &type) const {
        if (type.kind == TypeKind::Parameter &&
            type.declaration < currentTypeParameterNames_.size()) {
            return currentTypeParameterNames_[type.declaration];
        }
        if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
             type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1) {
            return std::string(typeName(type)) + ' ' + displayType(type.arguments.front());
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return '[' + std::to_string(type.declaration) + ']' +
                   displayType(type.arguments.front());
        }
        if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
            return '[' + displayType(type.arguments.front()) + ']';
        }
        if (type.kind == TypeKind::Function && !type.arguments.empty()) {
            std::string name = "fn(";
            for (std::size_t index = 1; index < type.arguments.size(); ++index) {
                if (index != 1) {
                    name += ", ";
                }
                name += displayType(type.arguments[index]);
            }
            name += ") ";
            name += displayType(type.arguments.front());
            return name;
        }
        std::string name;
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            name = program_.structs[type.declaration].name;
        } else if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            name = program_.enums[type.declaration].name;
        } else if (type.kind == TypeKind::Contract &&
                   type.declaration < program_.contracts.size()) {
            name = program_.contracts[type.declaration].name;
        } else {
            return typeName(type);
        }
        if (type.arguments.empty()) {
            return name;
        }
        name += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                name += ", ";
            }
            name += displayType(type.arguments[index]);
        }
        name += '>';
        return name;
    }

    void requireSame(Type expected, Type actual, SourceSpan span, std::string_view context) {
        if (expected.kind == TypeKind::Invalid || actual.kind == TypeKind::Invalid ||
            expected == actual) {
            return;
        }
        diagnostics_.error("FDN2011",
                           std::string(context) + " expects " + displayType(expected) + ", got " +
                               displayType(actual),
                           span);
    }

    const Program &program_;
    Diagnostics &diagnostics_;
    SemanticModel model_;
    std::unordered_map<std::string, FirStructId> structs_;
    std::unordered_map<std::string, FirEnumId> enums_;
    std::unordered_map<std::string, std::size_t> contracts_;
    std::unordered_map<std::string, FirFunctionId> functions_;
    std::vector<std::unordered_map<std::string, FirFunctionId>> methods_;
    std::unordered_map<std::string, std::size_t> typeParameters_;
    std::vector<std::string> currentTypeParameterNames_;
    std::vector<FunctionSignature> signatures_;
    std::vector<GenericCall> genericCalls_;
    std::vector<std::unordered_map<std::string, FirLocalId>> scopes_;
    std::vector<bool> resultOutstanding_;
    std::vector<bool> resultExitReported_;
    std::vector<SourceSpan> localSpans_;
    std::vector<MoveState> moveStates_;
    std::vector<LoanState> loanStates_;
    std::unordered_set<std::string> closureOuterNames_;
    bool transientBorrowsAllowed_{};
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

} // namespace foundation
