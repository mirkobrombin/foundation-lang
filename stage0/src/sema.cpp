#include "foundation/sema.hpp"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstdint>
#include <limits>
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

bool isCParameterType(const Type &type, std::string_view symbol,
                      std::string_view packageName) {
    return (isMachineScalar(type) && type != voidType && type != neverType) ||
           ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
            type.arguments.size() == 1) ||
           (type.kind == TypeKind::View && type.arguments.size() == 1 &&
            type.arguments.front() == stringType) ||
           (type.kind == TypeKind::Edit && type.arguments.size() == 1 &&
            ((isMachineScalar(type.arguments.front()) &&
              type.arguments.front() != voidType && type.arguments.front() != neverType) ||
             type.arguments.front() == boolType || type.arguments.front() == stringType)) ||
           (packageName == "foundation.worker" &&
            (symbol == "foundation_runtime_supervisor_adopt" ||
             symbol == "foundation_runtime_pool_submit") &&
            type.kind == TypeKind::Task && type.arguments.size() == 1 &&
            type.arguments.front() == voidType);
}

bool isCReturnType(const Type &type) {
    return (isMachineScalar(type) && type != neverType) || type == stringType ||
           ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
            type.arguments.size() == 1);
}

bool containsRawPointer(const Type &type) {
    if (type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) {
        return true;
    }
    return std::any_of(type.arguments.begin(), type.arguments.end(), containsRawPointer);
}

bool integerLiteralFits(Type type, std::uint64_t magnitude, bool negative) {
    if (isUnsignedInteger(type)) {
        if (negative) {
            return false;
        }
        switch (type.kind) {
        case TypeKind::U8:
            return magnitude <= UINT8_MAX;
        case TypeKind::U16:
            return magnitude <= UINT16_MAX;
        case TypeKind::U32:
            return magnitude <= UINT32_MAX;
        case TypeKind::U64:
            return true;
        case TypeKind::Usize:
            return magnitude <= std::numeric_limits<std::size_t>::max();
        default:
            return false;
        }
    }
    if (!isSignedInteger(type)) {
        return false;
    }
    std::uint64_t maximum{};
    switch (type.kind) {
    case TypeKind::I8:
        maximum = INT8_MAX;
        break;
    case TypeKind::I16:
        maximum = INT16_MAX;
        break;
    case TypeKind::I32:
        maximum = INT32_MAX;
        break;
    case TypeKind::I64:
        maximum = INT64_MAX;
        break;
    case TypeKind::Isize:
        maximum = static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max());
        break;
    default:
        return false;
    }
    return magnitude <= maximum + (negative ? UINT64_C(1) : UINT64_C(0));
}

std::optional<Type> machineScalarType(std::string_view name) {
    if (name == "i8") return i8Type;
    if (name == "i16") return i16Type;
    if (name == "i32") return i32Type;
    if (name == "i64") return i64Type;
    if (name == "u8") return u8Type;
    if (name == "u16") return u16Type;
    if (name == "u32") return u32Type;
    if (name == "u64") return u64Type;
    if (name == "isize") return isizeType;
    if (name == "usize") return usizeType;
    if (name == "f32") return f32Type;
    if (name == "f64") return f64Type;
    return std::nullopt;
}

unsigned int integerValueBits(Type type) {
    switch (type.kind) {
    case TypeKind::I8:
        return std::numeric_limits<std::int8_t>::digits;
    case TypeKind::I16:
        return std::numeric_limits<std::int16_t>::digits;
    case TypeKind::I32:
        return std::numeric_limits<std::int32_t>::digits;
    case TypeKind::I64:
        return std::numeric_limits<std::int64_t>::digits;
    case TypeKind::Isize:
        return std::numeric_limits<std::intptr_t>::digits;
    case TypeKind::U8:
        return std::numeric_limits<std::uint8_t>::digits;
    case TypeKind::U16:
        return std::numeric_limits<std::uint16_t>::digits;
    case TypeKind::U32:
        return std::numeric_limits<std::uint32_t>::digits;
    case TypeKind::U64:
        return std::numeric_limits<std::uint64_t>::digits;
    case TypeKind::Usize:
        return std::numeric_limits<std::size_t>::digits;
    default:
        return 0;
    }
}

bool numericConversionIsInfallible(Type source, Type target) {
    if (source == target) {
        return true;
    }
    if (isInteger(source) && isInteger(target)) {
        if (isSignedInteger(source) && isUnsignedInteger(target)) {
            return false;
        }
        return integerValueBits(target) >= integerValueBits(source);
    }
    if (isInteger(source) && isFloating(target)) {
        const auto precision = static_cast<unsigned int>(
            target == f32Type ? std::numeric_limits<float>::digits
                              : std::numeric_limits<double>::digits);
        return precision >= integerValueBits(source);
    }
    return source == f32Type && target == f64Type;
}

class Analyzer {
  public:
    Analyzer(const Program &program, Diagnostics &diagnostics, AnalyzeOptions options)
        : program_(program), diagnostics_(diagnostics), options_(options) {
        model_.expressionTypes.resize(program.expressions.size(), invalidType);
        model_.expressionReads.resize(program.expressions.size());
        model_.expressionContractConversions.resize(program.expressions.size());
        model_.expressionLocals.resize(program.expressions.size());
        model_.callTargets.resize(program.expressions.size());
        model_.emptyTests.resize(program.expressions.size());
        model_.structTargets.resize(program.expressions.size());
        model_.expressionFields.resize(program.expressions.size());
        model_.enumTargets.resize(program.expressions.size());
        model_.matchTargets.resize(program.expressions.size());
        model_.ownershipTargets.resize(program.expressions.size());
        model_.functionValueTargets.resize(program.expressions.size());
        model_.closureTargets.resize(program.expressions.size());
        model_.taskWaitTargets.resize(program.expressions.size());
        model_.blockingCallTargets.resize(program.expressions.size());
        model_.callbackCallTargets.resize(program.expressions.size());
        model_.channelOperationTargets.resize(program.expressions.size());
        model_.channelSenderClones.resize(program.expressions.size());
        model_.selectTargets.resize(program.statements.size());
        model_.forTargets.resize(program.statements.size());
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
        model_.attributeDeclarations.resize(program.attributeDeclarations.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
        declareStructs();
        declareEnums();
        declareContracts();
        declareAttributes();
        resolveContracts();
        resolveStructs();
        resolveEnums();
        rejectTypeCycles();
        resolveAttributeDeclarations();
        declareFunctions();
        resolveAppliedAttributes();
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
        if (diagnostics_.hasErrors() && !options_.retainInvalidModel) {
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
            if (declaration.methods.empty() && declaration.parents.empty()) {
                diagnostics_.error("FDN2090", "contract must declare at least one method",
                                   declaration.span);
            }
        }
    }

    void declareAttributes() {
        for (std::size_t index = 0; index < program_.attributeDeclarations.size(); ++index) {
            const auto &declaration = program_.attributeDeclarations[index];
            const auto separator = declaration.name.rfind('.');
            const auto sourceName = declaration.name.substr(
                separator == std::string::npos ? 0 : separator + 1);
            if (sourceName == "target" || sourceName == "blocking" ||
                sourceName == "callback") {
                diagnostics_.error("FDN2150", sourceName + " is reserved for the compiler",
                                   declaration.span);
            }
            if (!attributes_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2150", "duplicate attribute " + declaration.name,
                                   declaration.span);
            }
        }
    }

    static FirAttributeTarget lowerAttributeTarget(AttributeTarget target) {
        switch (target) {
        case AttributeTarget::Function:
            return FirAttributeTarget::Function;
        case AttributeTarget::Struct:
            return FirAttributeTarget::Struct;
        case AttributeTarget::Service:
            return FirAttributeTarget::Service;
        case AttributeTarget::Enum:
            return FirAttributeTarget::Enum;
        case AttributeTarget::Contract:
            return FirAttributeTarget::Contract;
        case AttributeTarget::Method:
            return FirAttributeTarget::Method;
        case AttributeTarget::Action:
            return FirAttributeTarget::Action;
        case AttributeTarget::Field:
            return FirAttributeTarget::Field;
        case AttributeTarget::Variant:
            return FirAttributeTarget::Variant;
        case AttributeTarget::Parameter:
            return FirAttributeTarget::Parameter;
        }
        return FirAttributeTarget::Function;
    }

    bool isCopyParameterType(const Type &type,
                             std::unordered_set<std::string> &active) const {
        if (isMachineScalar(type) && type != voidType && type != neverType) {
            return true;
        }
        if (type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) {
            return type.arguments.size() == 1;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return isCopyParameterType(type.arguments.front(), active);
        }
        if (type.kind != TypeKind::Struct && type.kind != TypeKind::Enum) {
            return false;
        }

        const auto key = semanticTypeKey(type);
        if (!active.insert(key).second) {
            return true;
        }
        auto copy = true;
        if (type.kind == TypeKind::Struct && type.declaration < model_.structs.size()) {
            const auto &name = program_.structs[type.declaration].name;
            for (const auto &function : program_.functions) {
                if (function.ownerType == name && function.name.ends_with(".drop")) {
                    copy = false;
                    break;
                }
            }
            if (copy) {
                for (const auto &field : model_.structs[type.declaration].fieldTypes) {
                    if (!isCopyParameterType(substitute(field, type.arguments), active)) {
                        copy = false;
                        break;
                    }
                }
            }
        } else if (type.declaration >= model_.enums.size()) {
            copy = false;
        } else {
            for (const auto &payload : model_.enums[type.declaration].payloadTypes) {
                if (payload.has_value() &&
                    !isCopyParameterType(substitute(*payload, type.arguments), active)) {
                    copy = false;
                    break;
                }
            }
        }
        active.erase(key);
        return copy;
    }

    bool isCopyParameterType(const Type &type) const {
        std::unordered_set<std::string> active;
        return isCopyParameterType(type, active);
    }

    Type resolveParameterType(const Parameter &parameter) {
        const auto base = resolveType(parameter.type);
        if (base == voidType) {
            return base;
        }
        switch (parameter.mode) {
        case ParameterMode::Bootstrap:
        case ParameterMode::Transfer:
            return base;
        case ParameterMode::Edit:
            return Type{TypeKind::Edit, 0, {base}};
        case ParameterMode::Read:
            if (isCopyParameterType(base)) {
                return base;
            }
            return Type{TypeKind::View, 0, {base}};
        }
        return invalidType;
    }

    Type specializeReadParameter(Type type, ParameterMode mode) const {
        if (mode == ParameterMode::Read && type.kind == TypeKind::View &&
            type.arguments.size() == 1 && isCopyParameterType(type.arguments.front())) {
            return type.arguments.front();
        }
        return type;
    }

    ParameterMode functionParameterMode(const Type &type) const {
        if (type.kind == TypeKind::View) {
            return ParameterMode::Read;
        }
        if (type.kind == TypeKind::Edit) {
            return ParameterMode::Edit;
        }
        return isCopyParameterType(type) ? ParameterMode::Read : ParameterMode::Transfer;
    }

    Type functionValueParameterType(Type type, ParameterMode mode) const {
        if (mode != ParameterMode::Read) {
            return type;
        }
        if (type.kind == TypeKind::View && type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        return isCopyParameterType(type)
                   ? type
                   : Type{TypeKind::View, 1, {std::move(type)}};
    }

    void resolveContracts() {
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            const auto &declaration = program_.contracts[index];
            auto &semantic = model_.contracts[index];
            setTypeParameters(declaration.typeParameters, declaration.span);
            semantic.typeParameterCount = declaration.typeParameters.size();
            std::unordered_set<std::string> parents;
            for (const auto &parent : declaration.parents) {
                const auto type = resolveType(parent);
                semantic.parents.push_back(type);
                if (type.kind != TypeKind::Contract) {
                    diagnostics_.error("FDN2140", "extends requires a contract", parent.span);
                    continue;
                }
                if (!parents.emplace(semanticTypeKey(type)).second) {
                    diagnostics_.error("FDN2141", "duplicate parent contract", parent.span);
                }
            }
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
                if (method.receiver == ReceiverKind::Own &&
                    method.defaultFunction.has_value()) {
                    diagnostics_.error("FDN2148", "own contract method cannot have a default",
                                       method.span);
                }
                SemanticContractMethod target;
                target.name = method.name;
                target.receiver = method.receiver;
                target.returnType = resolveType(method.returnType);
                target.exported = method.exported;
                target.span = method.span;
                target.originContract = index;
                target.originArguments.reserve(declaration.typeParameters.size());
                for (std::size_t parameter = 0;
                     parameter < declaration.typeParameters.size(); ++parameter) {
                    target.originArguments.emplace_back(TypeKind::Parameter, parameter);
                }
                target.defaultFunction = method.defaultFunction;
                if (containsBorrow(target.returnType) || containsBareSlice(target.returnType) ||
                    containsBareContract(target.returnType)) {
                    diagnostics_.error("FDN2063", "borrow cannot be returned from a method",
                                       method.returnType.span);
                }
                for (const auto &parameter : method.parameters) {
                    const auto type = resolveParameterType(parameter);
                    if (type == voidType || type == neverType) {
                        diagnostics_.error("FDN2016", "parameter cannot have type void or never",
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
                        diagnostics_.error("FDN2099",
                                           "contract value requires view, edit, or own",
                                           parameter.span);
                    }
                    target.parameterTypes.push_back(type);
                    target.parameterNames.push_back(parameter.name);
                    target.parameterModes.push_back(parameter.mode);
                }
                semantic.methods.push_back(std::move(target));
            }
        }

        std::vector<unsigned char> states(program_.contracts.size());
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            flattenContract(index, states);
        }
    }

    static bool sameContractSignature(const SemanticContractMethod &left,
                                      const SemanticContractMethod &right) {
        return left.receiver == right.receiver && left.returnType == right.returnType &&
               left.parameterTypes == right.parameterTypes &&
               left.parameterModes == right.parameterModes;
    }

    void mergeContractMethod(std::vector<SemanticContractMethod> &methods,
                             SemanticContractMethod method, SourceSpan span,
                             bool overriding,
                             std::unordered_map<std::string, SourceSpan> &ambiguousDefaults) {
        const auto found = std::find_if(methods.begin(), methods.end(), [&](const auto &current) {
            return current.name == method.name;
        });
        if (found == methods.end()) {
            methods.push_back(std::move(method));
            return;
        }
        if (!sameContractSignature(*found, method)) {
            diagnostics_.error("FDN2143", "conflicting inherited method " + method.name, span);
            return;
        }
        if (overriding) {
            *found = std::move(method);
            ambiguousDefaults.erase(found->name);
        } else if (found->defaultFunction.has_value() && method.defaultFunction.has_value() &&
                   found->defaultFunction != method.defaultFunction) {
            ambiguousDefaults.emplace(method.name, span);
        } else if (!found->defaultFunction.has_value() && method.defaultFunction.has_value()) {
            *found = std::move(method);
        }
    }

    void flattenContract(std::size_t index, std::vector<unsigned char> &states) {
        if (index >= states.size() || states[index] == 2) {
            return;
        }
        if (states[index] == 1) {
            diagnostics_.error("FDN2142", "contract inheritance cycle",
                               program_.contracts[index].span);
            return;
        }

        states[index] = 1;
        auto &semantic = model_.contracts[index];
        auto directMethods = std::move(semantic.methods);
        semantic.methods.clear();
        std::unordered_map<std::string, SourceSpan> ambiguousDefaults;
        for (std::size_t parentIndex = 0; parentIndex < semantic.parents.size(); ++parentIndex) {
            const auto &parent = semantic.parents[parentIndex];
            if (parent.kind != TypeKind::Contract ||
                parent.declaration >= program_.contracts.size()) {
                continue;
            }
            flattenContract(parent.declaration, states);
            for (const auto &source : model_.contracts[parent.declaration].methods) {
                auto method = source;
                method.returnType = substitute(method.returnType, parent.arguments);
                for (auto &parameter : method.parameterTypes) {
                    parameter = substitute(parameter, parent.arguments);
                }
                for (auto &argument : method.originArguments) {
                    argument = substitute(argument, parent.arguments);
                }
                mergeContractMethod(semantic.methods, std::move(method),
                                    program_.contracts[index].parents[parentIndex].span, false,
                                    ambiguousDefaults);
            }
        }
        for (auto &method : directMethods) {
            const auto span = method.span;
            mergeContractMethod(semantic.methods, std::move(method), span, true,
                                ambiguousDefaults);
        }
        for (const auto &[name, span] : ambiguousDefaults) {
            diagnostics_.error("FDN2144", "ambiguous inherited default " + name, span);
        }
        states[index] = 2;
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

    bool parallelTransferSafe(const Type &type) const {
        std::unordered_set<std::string> active;
        return parallelTransferSafe(type, active);
    }

    bool parallelTransferOptIn(std::size_t declaration) const {
        if (declaration >= model_.structs.size()) {
            return false;
        }
        for (const auto &use : model_.structs[declaration].attributes) {
            if (use.declaration < model_.attributeDeclarations.size() &&
                model_.attributeDeclarations[use.declaration].name ==
                    "std.concurrent.Transferable") {
                return true;
            }
        }
        return false;
    }

    bool parallelTransferSafe(const Type &type,
                              std::unordered_set<std::string> &active) const {
        if (isMachineScalar(type) || type == stringType) {
            return true;
        }
        if (type.kind == TypeKind::Parameter) {
            const auto &constraints =
                program_.functions[currentFunction_].transferableTypeParameters;
            return type.declaration < constraints.size() && constraints[type.declaration];
        }
        if (type.kind == TypeKind::Function) {
            return isTransferableFunction(type);
        }
        if ((type.kind == TypeKind::Channel || type.kind == TypeKind::Sender ||
             type.kind == TypeKind::Receiver) &&
            type.arguments.size() == 1) {
            return parallelTransferSafe(type.arguments.front(), active);
        }
        if (type.kind == TypeKind::Own && type.arguments.size() == 1) {
            return parallelTransferSafe(type.arguments.front(), active);
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return parallelTransferSafe(type.arguments.front(), active);
        }
        if (type.kind != TypeKind::Struct && type.kind != TypeKind::Enum) {
            return false;
        }

        const auto key = semanticTypeKey(type);
        if (!active.insert(key).second) {
            return true;
        }
        bool safe = true;
        if (type.kind == TypeKind::Struct) {
            if (type.declaration >= model_.structs.size() ||
                (type.declaration < methods_.size() &&
                 methods_[type.declaration].contains("drop") &&
                 !parallelTransferOptIn(type.declaration))) {
                safe = false;
            } else {
                for (const auto &field : model_.structs[type.declaration].fieldTypes) {
                    if (!parallelTransferSafe(substitute(field, type.arguments), active)) {
                        safe = false;
                        break;
                    }
                }
            }
        } else if (type.declaration >= model_.enums.size()) {
            safe = false;
        } else {
            for (const auto &payload : model_.enums[type.declaration].payloadTypes) {
                if (payload.has_value() &&
                    !parallelTransferSafe(substitute(*payload, type.arguments), active)) {
                    safe = false;
                    break;
                }
            }
        }
        active.erase(key);
        return safe;
    }

    void verifyTransferableTypeArguments(FirFunctionId function,
                                         const std::vector<Type> &arguments,
                                         SourceSpan span) {
        const auto &constraints = program_.functions[function].transferableTypeParameters;
        for (std::size_t index = 0;
             index < constraints.size() && index < arguments.size(); ++index) {
            if (constraints[index] && !parallelTransferSafe(arguments[index])) {
                diagnostics_.error(
                    "FDN2185",
                    "type argument is not safe to transfer between threads: " +
                        displayType(arguments[index]),
                    span);
            }
        }
    }

    bool isParallelPoolStart(const Type &base, const Function &function,
                             std::string_view member) const {
        return member == "Start" && base.kind == TypeKind::Struct &&
               base.declaration < program_.structs.size() &&
               program_.structs[base.declaration].name == "foundation.worker.Pool" &&
               function.packageName == "foundation.worker" &&
               function.ownerType == "foundation.worker.Pool";
    }

    void verifyParallelPoolTask(const MemberExpression &member, SourceSpan span) {
        if (member.arguments.size() != 1) {
            return;
        }
        const auto argument = member.arguments.front();
        const auto *spawn =
            std::get_if<SpawnExpression>(&program_.expressions[argument].value);
        if (spawn == nullptr) {
            diagnostics_.error(
                "FDN2185",
                "parallel Pool.Start requires a direct spawn expression",
                program_.expressions[argument].span);
            return;
        }
        const auto *call =
            std::get_if<CallExpression>(&program_.expressions[spawn->call].value);
        const auto &target = model_.callTargets[spawn->call];
        if (call == nullptr || !target.has_value() ||
            target->kind != CallTargetKind::Function) {
            return;
        }
        for (const auto callArgument : call->arguments) {
            const auto &type = model_.expressionTypes[callArgument];
            if (!parallelTransferSafe(type)) {
                diagnostics_.error(
                    "FDN2185",
                    "parallel task argument is not safe to transfer between threads: " +
                        displayType(type),
                    program_.expressions[callArgument].span.length == 0
                        ? span
                        : program_.expressions[callArgument].span);
            }
        }
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
        if (type.kind == TypeKind::Own && type.arguments.size() == 1 &&
            type.arguments.front().kind == TypeKind::Contract) {
            return false;
        }
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
                if (type == voidType || type == neverType) {
                    diagnostics_.error("FDN2022", "struct field cannot have type void or never",
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
                    diagnostics_.error("FDN2099",
                                       "contract value requires view, edit, or own",
                                       source.span);
                }
                semantic.fieldTypes.push_back(type);
            }
            std::unordered_set<std::string> implementations;
            for (const auto &implementation : declaration.implementations) {
                const auto type = resolveType(implementation.contract);
                semantic.implementations.push_back(type);
                std::optional<FirFieldId> delegate;
                if (implementation.delegate.has_value()) {
                    const auto found = fields.find(*implementation.delegate);
                    if (found == fields.end()) {
                        diagnostics_.error("FDN2146", "unknown delegation field " +
                                                       *implementation.delegate,
                                           implementation.span);
                    } else {
                        delegate = found->second;
                    }
                }
                semantic.implementationDelegates.push_back(delegate);
                if (type.kind != TypeKind::Contract) {
                    diagnostics_.error("FDN2092", "implements requires a contract",
                                       implementation.contract.span);
                    continue;
                }
                const auto key = std::to_string(type.declaration);
                if (!implementations.emplace(key).second) {
                    diagnostics_.error("FDN2093", "duplicate contract implementation",
                                       implementation.contract.span);
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
                if (type == voidType || type == neverType) {
                    diagnostics_.error("FDN2033", "enum payload cannot have type void or never",
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
                    diagnostics_.error("FDN2099",
                                       "contract value requires view, edit, or own",
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
                    if (child == voidType &&
                        program_.enums[type.declaration].builtin == BuiltinEnumKind::Result) {
                        continue;
                    }
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

    bool metadataType(const Type &type, SourceSpan span,
                      std::unordered_set<std::string> &active) {
        if (isNumeric(type) || type == boolType || type == stringType) {
            return true;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return metadataType(type.arguments.front(), span, active);
        }
        if (type.kind != TypeKind::Struct && type.kind != TypeKind::Enum) {
            diagnostics_.error("FDN2161", "attribute parameter type is not metadata-safe",
                               span);
            return false;
        }
        const auto key = std::to_string(static_cast<unsigned int>(type.kind)) + ':' +
                         std::to_string(type.declaration);
        if (!active.emplace(key).second) {
            diagnostics_.error("FDN2162", "attribute metadata type cycle", span);
            return false;
        }
        auto valid = true;
        for (const auto &[child, childSpan] : layoutChildren(type)) {
            valid = metadataType(child, childSpan, active) && valid;
        }
        active.erase(key);
        return valid;
    }

    void resolveAttributeDeclarations() {
        setTypeParameters({}, {});
        for (std::size_t index = 0; index < program_.attributeDeclarations.size(); ++index) {
            const auto &source = program_.attributeDeclarations[index];
            auto &target = model_.attributeDeclarations[index];
            target.name = source.name;
            target.repeatable = source.repeatable;
            target.exported = source.exported;
            if (source.targets.empty()) {
                diagnostics_.error("FDN2151", "attribute must declare at least one target",
                                   source.span);
            }
            std::unordered_set<unsigned int> targets;
            for (const auto sourceTarget : source.targets) {
                const auto lowered = lowerAttributeTarget(sourceTarget);
                if (!targets.emplace(static_cast<unsigned int>(lowered)).second) {
                    diagnostics_.error("FDN2151", "duplicate attribute target", source.span);
                }
                target.targets.push_back(lowered);
            }
            std::unordered_set<std::string> parameters;
            for (const auto &parameter : source.parameters) {
                if (!parameters.emplace(parameter.name).second) {
                    diagnostics_.error("FDN2151",
                                       "duplicate attribute parameter " + parameter.name,
                                       parameter.span);
                }
                const auto type = resolveType(parameter.type);
                std::unordered_set<std::string> active;
                static_cast<void>(metadataType(type, parameter.span, active));
                target.parameters.push_back({parameter.name, type});
            }
        }
    }

    bool attributeConstant(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (std::holds_alternative<IntegerExpression>(expression.value) ||
            std::holds_alternative<FloatingExpression>(expression.value) ||
            std::holds_alternative<BooleanExpression>(expression.value) ||
            std::holds_alternative<StringExpression>(expression.value)) {
            return true;
        }
        if (const auto *array = std::get_if<ArrayExpression>(&expression.value)) {
            return std::all_of(array->elements.begin(), array->elements.end(),
                               [this](const auto element) {
                                   return attributeConstant(element);
                               });
        }
        if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            const auto declaration = structs_.find(literal->type.name);
            if (declaration != structs_.end() &&
                literal->fields.size() != program_.structs[declaration->second].fields.size()) {
                return false;
            }
            return std::all_of(literal->fields.begin(), literal->fields.end(),
                               [this](const auto &field) {
                                   return attributeConstant(field.value);
                               });
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            if (member->base.has_value() &&
                !std::holds_alternative<NameExpression>(
                    program_.expressions[*member->base].value)) {
                return false;
            }
            return std::all_of(member->arguments.begin(), member->arguments.end(),
                               [this](const auto argument) {
                                   return attributeConstant(argument);
                               });
        }
        return false;
    }

    FirAttributeValue attributeValue(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        FirAttributeValue result;
        result.type = model_.expressionTypes[id];
        if (const auto *integer = std::get_if<IntegerExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Integer;
            result.magnitude = integer->magnitude;
            result.negative = integer->negative;
        } else if (const auto *floating =
                       std::get_if<FloatingExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Floating;
            result.text = floating->text;
        } else if (const auto *boolean = std::get_if<BooleanExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Boolean;
            result.boolean = boolean->value;
        } else if (const auto *string = std::get_if<StringExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::String;
            result.text = string->value;
        } else if (const auto *array = std::get_if<ArrayExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Array;
            for (const auto element : array->elements) {
                result.children.push_back(attributeValue(element));
            }
        } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Struct;
            for (const auto &field : literal->fields) {
                result.members.push_back(field.name);
                result.children.push_back(attributeValue(field.value));
            }
        } else if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            result.kind = FirAttributeValueKind::Enum;
            if (model_.enumTargets[id].has_value()) {
                result.variant = model_.enumTargets[id]->variant;
            }
            for (const auto argument : member->arguments) {
                result.children.push_back(attributeValue(argument));
            }
        }
        return result;
    }

    static const char *attributeTargetName(FirAttributeTarget target) {
        switch (target) {
        case FirAttributeTarget::Function:
            return "fn";
        case FirAttributeTarget::Struct:
            return "struct";
        case FirAttributeTarget::Service:
            return "service";
        case FirAttributeTarget::Enum:
            return "enum";
        case FirAttributeTarget::Contract:
            return "contract";
        case FirAttributeTarget::Method:
            return "method";
        case FirAttributeTarget::Action:
            return "action";
        case FirAttributeTarget::Field:
            return "field";
        case FirAttributeTarget::Variant:
            return "variant";
        case FirAttributeTarget::Parameter:
            return "parameter";
        }
        return "declaration";
    }

    std::vector<FirAttributeUse>
    resolveAttributes(const std::vector<AttributeApplication> &applications,
                      FirAttributeTarget target, std::string_view packageName) {
        std::vector<FirAttributeUse> result;
        std::unordered_set<FirAttributeId> seen;
        currentPackageOverride_ = packageName;
        setTypeParameters({}, {});
        for (const auto &application : applications) {
            if (application.name == "blocking" || application.name == "callback") {
                if (target != FirAttributeTarget::Function) {
                    diagnostics_.error(
                        "FDN2178", "@" + application.name + " can only target a function",
                        application.span);
                }
                continue;
            }
            const auto found = attributes_.find(application.name);
            if (found == attributes_.end()) {
                diagnostics_.error("FDN2152", "unknown attribute @" + application.name,
                                   application.span);
                continue;
            }
            const auto id = found->second;
            const auto &declaration = model_.attributeDeclarations[id];
            if (std::find(declaration.targets.begin(), declaration.targets.end(), target) ==
                declaration.targets.end()) {
                diagnostics_.error("FDN2153",
                                   "attribute @" + application.name + " cannot target " +
                                       attributeTargetName(target),
                                   application.span);
            }
            if (!declaration.repeatable && !seen.emplace(id).second) {
                diagnostics_.error("FDN2154",
                                   "attribute @" + application.name + " is not repeatable",
                                   application.span);
            }

            std::vector<const AttributeArgument *> ordered(declaration.parameters.size());
            std::size_t positional{};
            auto named = false;
            for (const auto &argument : application.arguments) {
                if (argument.name.has_value()) {
                    named = true;
                    const auto parameter = std::find_if(
                        declaration.parameters.begin(), declaration.parameters.end(),
                        [&](const auto &candidate) {
                            return candidate.name == *argument.name;
                        });
                    if (parameter == declaration.parameters.end()) {
                        diagnostics_.error("FDN2156",
                                           "unknown attribute argument " + *argument.name,
                                           argument.span);
                        continue;
                    }
                    const auto index = static_cast<std::size_t>(
                        std::distance(declaration.parameters.begin(), parameter));
                    if (ordered[index] != nullptr) {
                        diagnostics_.error("FDN2157",
                                           "duplicate attribute argument " + *argument.name,
                                           argument.span);
                        continue;
                    }
                    ordered[index] = &argument;
                    continue;
                }
                if (named) {
                    diagnostics_.error("FDN2155",
                                       "positional attribute argument follows named argument",
                                       argument.span);
                    continue;
                }
                if (positional >= ordered.size()) {
                    diagnostics_.error("FDN2156", "too many attribute arguments",
                                       argument.span);
                    continue;
                }
                ordered[positional++] = &argument;
            }

            FirAttributeUse use;
            use.declaration = id;
            for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
                if (ordered[index] == nullptr) {
                    diagnostics_.error("FDN2158",
                                       "missing attribute argument " +
                                           declaration.parameters[index].name,
                                       application.span);
                    continue;
                }
                const auto expression = ordered[index]->value;
                if (!attributeConstant(expression)) {
                    diagnostics_.error("FDN2160", "attribute argument must be constant",
                                       ordered[index]->span);
                    continue;
                }
                const auto actual = analyzeExpression(
                    expression, declaration.parameters[index].type, ExpressionUse::Inspect);
                requireSame(declaration.parameters[index].type, actual,
                            ordered[index]->span, "attribute argument");
                use.arguments.push_back({declaration.parameters[index].name,
                                         attributeValue(expression)});
            }
            result.push_back(std::move(use));
        }
        currentPackageOverride_ = {};
        return result;
    }

    void resolveAppliedAttributes() {
        currentFunction_ = program_.functions.size();
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &source = program_.structs[index];
            auto &target = model_.structs[index];
            target.attributes = resolveAttributes(
                source.attributes,
                source.kind == StructKind::Service ? FirAttributeTarget::Service
                                                   : FirAttributeTarget::Struct,
                source.packageName);
            target.fieldAttributes.resize(source.fields.size());
            for (std::size_t field = 0; field < source.fields.size(); ++field) {
                target.fieldAttributes[field] =
                    resolveAttributes(source.fields[field].attributes,
                                      FirAttributeTarget::Field, source.packageName);
            }
        }
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            const auto &source = program_.enums[index];
            auto &target = model_.enums[index];
            target.attributes = resolveAttributes(source.attributes, FirAttributeTarget::Enum,
                                                  source.packageName);
            target.variantAttributes.resize(source.variants.size());
            for (std::size_t variant = 0; variant < source.variants.size(); ++variant) {
                target.variantAttributes[variant] =
                    resolveAttributes(source.variants[variant].attributes,
                                      FirAttributeTarget::Variant, source.packageName);
            }
        }
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            const auto &source = program_.contracts[index];
            auto &target = model_.contracts[index];
            target.attributes = resolveAttributes(source.attributes,
                                                  FirAttributeTarget::Contract,
                                                  source.packageName);
            for (auto &method : target.methods) {
                const auto &origin = program_.contracts[method.originContract];
                const auto sourceMethod = std::find_if(
                    origin.methods.begin(), origin.methods.end(), [&](const auto &candidate) {
                        return candidate.name == method.name;
                    });
                if (sourceMethod == origin.methods.end()) {
                    continue;
                }
                method.attributes = resolveAttributes(sourceMethod->attributes,
                                                      FirAttributeTarget::Method,
                                                      origin.packageName);
                method.parameterAttributes.resize(sourceMethod->parameters.size());
                for (std::size_t parameter = 0;
                     parameter < sourceMethod->parameters.size(); ++parameter) {
                    method.parameterAttributes[parameter] = resolveAttributes(
                        sourceMethod->parameters[parameter].attributes,
                        FirAttributeTarget::Parameter, origin.packageName);
                }
            }
        }
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &source = program_.functions[index];
            auto &target = model_.functions[index];
            const auto attributeTarget =
                source.action
                    ? FirAttributeTarget::Action
                    : !source.ownerType.empty() ? FirAttributeTarget::Method
                                                : FirAttributeTarget::Function;
            target.attributes = resolveAttributes(source.attributes, attributeTarget,
                                                  source.packageName);
            target.parameterAttributes.resize(source.parameters.size());
            for (std::size_t parameter = 0; parameter < source.parameters.size(); ++parameter) {
                target.parameterAttributes[parameter] = resolveAttributes(
                    source.parameters[parameter].attributes, FirAttributeTarget::Parameter,
                    source.packageName);
            }
        }
    }

    void declareFunctions() {
        bool foundMain = false;
        std::unordered_set<std::string> cSymbols;
        std::unordered_set<std::string> callbackCancelSymbols;
        std::unordered_set<std::string> testNames;
        methods_.resize(program_.structs.size() + program_.enums.size());
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            setTypeParameters(function.typeParameters, function.span);
            semantic.typeParameterCount = function.typeParameters.size();
            semantic.returnType = function.inferredReturn ? invalidType
                                                          : resolveType(function.returnType);
            if (function.blocking) {
                const auto count = static_cast<std::size_t>(std::count_if(
                    function.attributes.begin(), function.attributes.end(),
                    [](const auto &attribute) { return attribute.name == "blocking"; }));
                if (count > 1) {
                    diagnostics_.error("FDN2179", "function has more than one @blocking",
                                       function.span);
                }
                for (const auto &attribute : function.attributes) {
                    if (attribute.name != "blocking") {
                        continue;
                    }
                    if (attribute.parenthesized || !attribute.arguments.empty()) {
                        diagnostics_.error(
                            "FDN2179",
                            "@blocking does not accept parentheses or arguments",
                            attribute.span);
                    }
                }
                if (!function.cSymbol.has_value() || function.hasBody || function.task ||
                    function.receiver.has_value() || function.closure) {
                    diagnostics_.error(
                        "FDN2178",
                        "@blocking requires a bodyless extern c function declaration",
                        function.span);
                }
            }
            if (function.callback) {
                const auto count = static_cast<std::size_t>(std::count_if(
                    function.attributes.begin(), function.attributes.end(),
                    [](const auto &attribute) { return attribute.name == "callback"; }));
                if (count > 1) {
                    diagnostics_.error("FDN2181", "function has more than one @callback",
                                       function.span);
                }
                if (function.blocking) {
                    diagnostics_.error(
                        "FDN2181", "function cannot be both @blocking and @callback",
                        function.span);
                }
                for (const auto &attribute : function.attributes) {
                    if (attribute.name != "callback" || !attribute.parenthesized) {
                        continue;
                    }
                    if (attribute.arguments.size() != 1 ||
                        attribute.arguments.front().name != "cancel") {
                        diagnostics_.error(
                            "FDN2181",
                            "@callback accepts only the named cancel symbol",
                            attribute.span);
                        continue;
                    }
                    const auto expression = attribute.arguments.front().value;
                    const auto *name = std::get_if<NameExpression>(
                        &program_.expressions[expression].value);
                    if (name == nullptr || !name->typeArguments.empty() ||
                        !isCIdentifier(name->name) || isReservedCSymbol(name->name)) {
                        diagnostics_.error(
                            "FDN2181", "@callback cancel must be a plain C ABI symbol",
                            attribute.arguments.front().span);
                        continue;
                    }
                    semantic.callbackCancelSymbol = name->name;
                    if (cSymbols.contains(name->name)) {
                        diagnostics_.error(
                            "FDN2181",
                            "callback cancel symbol conflicts with C symbol " + name->name,
                            attribute.arguments.front().span);
                    }
                    callbackCancelSymbols.emplace(name->name);
                }
                if (!function.cSymbol.has_value() || function.hasBody || function.task ||
                    function.receiver.has_value() || function.closure) {
                    diagnostics_.error(
                        "FDN2178",
                        "@callback requires a bodyless extern c function declaration",
                        function.span);
                }
                if (semantic.returnType != i32Type) {
                    diagnostics_.error(
                        "FDN2182", "@callback return type must be i32 completion status",
                        function.returnType.span);
                }
            }
            if (function.task && function.cSymbol.has_value()) {
                diagnostics_.error("FDN2163", "task cannot use an external ABI declaration",
                                   function.span);
            }
            if (function.task && function.name == "main") {
                diagnostics_.error("FDN2164", "main must be a function, not a task",
                                   function.span);
            }
            if (function.task && semantic.returnType == neverType) {
                diagnostics_.error("FDN2016", "task cannot return never", function.returnType.span);
            }
            if (!function.inferredReturn &&
                (containsBorrow(semantic.returnType) ||
                 containsBareSlice(semantic.returnType) ||
                 containsBareContract(semantic.returnType))) {
                diagnostics_.error("FDN2063", "borrow cannot be returned from a function",
                                   function.returnType.span);
            }
            for (const auto &parameter : function.parameters) {
                const auto type = parameter.inferredType ? invalidType
                                                         : resolveParameterType(parameter);
                semantic.parameterTypes.push_back(type);
                if (parameter.inferredType) {
                    continue;
                }
                if (type == voidType || type == neverType) {
                    diagnostics_.error("FDN2016", "parameter cannot have type void or never",
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
                    diagnostics_.error("FDN2099",
                                       "contract value requires view, edit, or own",
                                       parameter.span);
                }
                if (function.task && containsBorrow(type)) {
                    diagnostics_.error("FDN2165", "task parameter cannot borrow from its caller",
                                       parameter.span);
                }
            }

            if (function.cSymbol.has_value()) {
                if (!isCIdentifier(*function.cSymbol)) {
                    diagnostics_.error("FDN2110", "invalid C or C++ symbol " +
                                                       *function.cSymbol,
                                       function.span);
                } else if (!cSymbols.emplace(*function.cSymbol).second) {
                    diagnostics_.error("FDN2111", "duplicate C symbol " + *function.cSymbol,
                                       function.span);
                } else if (callbackCancelSymbols.contains(*function.cSymbol)) {
                    diagnostics_.error(
                        "FDN2181", "C symbol conflicts with callback cancel symbol " +
                                       *function.cSymbol,
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
                    if (!isCParameterType(semantic.parameterTypes[parameter],
                                          function.hasBody ? std::string_view{}
                                                           : std::string_view{*function.cSymbol},
                                          function.packageName)) {
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

            if (function.testName.has_value()) {
                if (function.testName->empty()) {
                    diagnostics_.error("FDN2211", "test name cannot be empty", function.span);
                } else if (!testNames.emplace(*function.testName).second) {
                    diagnostics_.error("FDN2211", "duplicate test name " + *function.testName,
                                       function.span);
                }
                if (!function.typeParameters.empty() || !function.parameters.empty() ||
                    semantic.returnType != voidType || function.receiver.has_value() ||
                    function.task || function.cSymbol.has_value() || function.closure) {
                    diagnostics_.error("FDN2211", "test declaration has an invalid signature",
                                       function.span);
                }
                signatures_.push_back({semantic.returnType, semantic.parameterTypes});
                continue;
            }
            if (function.closure) {
                signatures_.push_back({semantic.returnType, semantic.parameterTypes});
                continue;
            }
            if (!function.ownerType.empty()) {
                const auto structOwner = structs_.find(function.ownerType);
                const auto enumOwner = enums_.find(function.ownerType);
                if (structOwner != structs_.end() || enumOwner != enums_.end()) {
                    const auto ownerIsStruct = structOwner != structs_.end();
                    const auto owner = ownerIsStruct
                                           ? structOwner->second
                                           : program_.structs.size() + enumOwner->second;
                    const auto prefix = function.ownerType + '.';
                    const auto methodName = function.name.starts_with(prefix)
                                                ? function.name.substr(prefix.size())
                                                : function.name;
                    if (function.action &&
                        (!ownerIsStruct ||
                         program_.structs[structOwner->second].kind != StructKind::Service)) {
                        diagnostics_.error("FDN2215",
                                           "action owner must be a service", function.span);
                    }
                    if (function.action && !function.receiver.has_value()) {
                        diagnostics_.error("FDN2215",
                                           "action requires an explicit receiver", function.span);
                    }
                    if (function.action && methodName == "drop") {
                        diagnostics_.error("FDN2215", "action cannot be named drop",
                                           function.span);
                    }
                    if (function.receiver.has_value()) {
                        if (!methods_[owner].emplace(methodName, index).second) {
                            diagnostics_.error("FDN2095", "duplicate method " + methodName,
                                               function.span);
                        }
                        if (ownerIsStruct && methodName == "drop" &&
                            (function.receiver != ReceiverKind::Edit ||
                             semantic.parameterTypes.size() != 1 ||
                             semantic.returnType != voidType)) {
                            diagnostics_.error(
                                "FDN2137",
                                "drop must have signature fn drop(edit) void", function.span);
                        }
                    } else if (!functions_.emplace(function.ownerType + '.' + methodName,
                                                   index)
                                    .second) {
                        diagnostics_.error("FDN2001", "duplicate associated function " +
                                                               methodName,
                                           function.span);
                    }
                } else if (!function.receiver.has_value() ||
                           !contracts_.contains(function.ownerType)) {
                    diagnostics_.error(
                        "FDN2094",
                        function.receiver.has_value()
                            ? "method owner is not a struct, enum, or contract"
                            : "associated function owner is not a struct or enum",
                        function.span);
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
            if (!function.receiver.has_value() && function.name == "len") {
                diagnostics_.error("FDN2018", "len is a reserved builtin", function.span);
            }
            if (!function.receiver.has_value() && function.name == "null") {
                diagnostics_.error("FDN2018", "null is a reserved builtin", function.span);
            }
            if (!function.receiver.has_value() && function.name == "isNull") {
                diagnostics_.error("FDN2018", "isNull is a reserved builtin", function.span);
            }
            if (!function.receiver.has_value() && function.name == "channel") {
                diagnostics_.error("FDN2018", "channel is a reserved builtin", function.span);
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
            const Type argumentsType{
                TypeKind::View, 0, {Type{TypeKind::Slice, 0, {stringType}}}};
            const auto acceptsArguments =
                semantic.parameterTypes.size() == 1 &&
                semantic.parameterTypes.front() == argumentsType;
            if (!function.typeParameters.empty() ||
                (!semantic.parameterTypes.empty() && !acceptsArguments) ||
                semantic.returnType != i32Type) {
                diagnostics_.error(
                    "FDN2007",
                    "main must have signature fn main() i32 or fn main(args view [String]) i32",
                    function.span);
            }
        }
        if (!foundMain && options_.requireMain) {
            diagnostics_.error("FDN2006", "program must declare main", {0, 0, 1, 1});
        }
    }

    void verifyImplementations() {
        for (std::size_t structId = 0; structId < program_.structs.size(); ++structId) {
            const auto &declaration = program_.structs[structId];
            const auto &semantic = model_.structs[structId];
            std::vector<Type> structArguments;
            structArguments.reserve(declaration.typeParameters.size());
            for (std::size_t parameter = 0; parameter < declaration.typeParameters.size();
                 ++parameter) {
                structArguments.emplace_back(TypeKind::Parameter, parameter);
            }
            const Type concrete{TypeKind::Struct, structId, std::move(structArguments)};
            for (std::size_t implementationIndex = 0;
                 implementationIndex < semantic.implementations.size(); ++implementationIndex) {
                const auto &implementation = semantic.implementations[implementationIndex];
                if (implementation.kind != TypeKind::Contract ||
                    implementation.declaration >= program_.contracts.size()) {
                    continue;
                }
                const auto &contract = program_.contracts[implementation.declaration];
                const auto &contractSemantic = model_.contracts[implementation.declaration];
                if (implementationIndex < semantic.implementationDelegates.size() &&
                    semantic.implementationDelegates[implementationIndex].has_value()) {
                    const auto field = *semantic.implementationDelegates[implementationIndex];
                    if (field < semantic.fieldTypes.size()) {
                        const auto delegated = substitute(semantic.fieldTypes[field],
                                                          concrete.arguments);
                        const auto implemented =
                            implementedContract(delegated, implementation.declaration);
                        if (!implemented.has_value() || implemented->ambiguous ||
                            implemented->contract != implementation) {
                            diagnostics_.error(
                                "FDN2147",
                                "delegation field does not implement " + contract.name,
                                declaration.implementations[implementationIndex].span);
                        }
                    }
                }
                for (std::size_t methodIndex = 0;
                     methodIndex < contractSemantic.methods.size();
                     ++methodIndex) {
                    const auto &requiredMethod = contractSemantic.methods[methodIndex];
                    const auto found = methods_[structId].find(requiredMethod.name);
                    if (found == methods_[structId].end()) {
                        const auto delegated =
                            implementationIndex < semantic.implementationDelegates.size() &&
                            semantic.implementationDelegates[implementationIndex].has_value();
                        if (delegated && requiredMethod.receiver == ReceiverKind::Own) {
                            diagnostics_.error(
                                "FDN2149",
                                "own method must be implemented locally when using delegation",
                                declaration.implementations[implementationIndex].span);
                            continue;
                        }
                        std::unordered_set<std::string> active;
                        if (contractMethodTarget(concrete, implementation, methodIndex, active)
                                .has_value()) {
                            continue;
                        }
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
                    const auto &required = requiredMethod;
                    if (provided.receiver != required.receiver) {
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
                        if (parameter + 1 >= provided.parameters.size() ||
                            parameter >= required.parameterModes.size() ||
                            provided.parameters[parameter + 1].mode !=
                                required.parameterModes[parameter]) {
                            diagnostics_.error(
                                "FDN2195",
                                "parameter mode mismatch for method " + requiredMethod.name,
                                provided.span);
                        }
                        requireSame(
                            substitute(required.parameterTypes[parameter],
                                       implementation.arguments),
                            signature.parameters[parameter + 1], provided.span,
                            "contract method parameter");
                    }
                    requireSame(substitute(required.returnType, implementation.arguments),
                                signature.returnType, provided.span,
                                "contract method return");
                    const auto &origin = program_.contracts[required.originContract];
                    if (declaration.packageName != origin.packageName &&
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

    struct WorkflowTarget {
        FirFunctionId function{};
        std::vector<Type> typeArguments;
        Type successType{invalidType};
    };

    std::optional<WorkflowTarget>
    analyzeWorkflowTarget(std::string_view name, Type input, Type error,
                          std::optional<Type> expectedSuccess, SourceSpan span,
                          std::string_view role) {
        auto found = functions_.find(std::string(name));
        if (found == functions_.end() && name.find('.') == std::string_view::npos &&
            !currentPackage().empty()) {
            found = functions_.find(std::string(currentPackage()) + '.' + std::string(name));
        }
        if (found == functions_.end()) {
            diagnostics_.error("FDN2221", "unknown " + std::string(role) + " " +
                                                  std::string(name),
                               span);
            return std::nullopt;
        }
        const auto id = found->second;
        const auto &declaration = program_.functions[id];
        const auto &signature = signatures_[id];
        if (declaration.receiver.has_value() || declaration.task || declaration.blocking ||
            declaration.callback || declaration.testName.has_value() ||
            declaration.cSymbol.has_value()) {
            diagnostics_.error("FDN2222", std::string(role) +
                                                  " must be a synchronous Foundation function",
                               span);
        }
        if (declaration.parameters.size() != 1 || signature.parameters.size() != 1) {
            diagnostics_.error("FDN2223", std::string(role) +
                                                  " must accept exactly one input",
                               span);
            return std::nullopt;
        }
        if (declaration.parameters.front().mode != ParameterMode::Read) {
            diagnostics_.error("FDN2224", std::string(role) +
                                                  " input must use read mode",
                               declaration.parameters.front().span);
        }

        auto parameter = signature.parameters.front();
        if (parameter.kind == TypeKind::View && parameter.arguments.size() == 1) {
            parameter = parameter.arguments.front();
        }
        if (!isResult(signature.returnType) || signature.returnType.arguments.size() != 2) {
            diagnostics_.error("FDN2225", std::string(role) +
                                                  " must return Result<Value, Error>",
                               declaration.returnType.span);
            return std::nullopt;
        }

        std::vector<std::optional<Type>> inferred(declaration.typeParameters.size());
        inferType(parameter, input, inferred, span, role);
        inferType(signature.returnType.arguments[1], error, inferred, span, role);
        if (expectedSuccess.has_value()) {
            inferType(signature.returnType.arguments[0], *expectedSuccess, inferred, span, role);
        }
        auto typeArguments =
            completeInference(inferred, declaration.typeParameters, span, name);
        requireSame(substitute(parameter, typeArguments), input, span, role);
        requireSame(error, substitute(signature.returnType.arguments[1], typeArguments), span,
                    role);
        const auto success = substitute(signature.returnType.arguments[0], typeArguments);
        if (expectedSuccess.has_value()) {
            requireSame(*expectedSuccess, success, span, role);
        }
        if (!program_.functions[currentFunction_].typeParameters.empty() &&
            !declaration.typeParameters.empty()) {
            genericCalls_.push_back({currentFunction_, id, typeArguments, span});
        }
        return WorkflowTarget{id, std::move(typeArguments), success};
    }

    void analyzeWorkflow(const Function &function, SemanticFunction &semantic) {
        const auto &source = *function.workflow;
        SemanticWorkflowFunction workflow;
        workflow.kind = source.kind;
        workflow.successType = resolveType(source.successType);
        workflow.errorType = resolveType(source.errorType);

        if (function.parameters.size() != 1 || semantic.parameterTypes.size() != 1) {
            diagnostics_.error("FDN2226", "workflow requires exactly one input",
                               function.span);
            semantic.workflow = std::move(workflow);
            return;
        }
        if (function.parameters.front().mode != ParameterMode::Read) {
            diagnostics_.error("FDN2224", "workflow input must use read mode",
                               function.parameters.front().span);
        }
        auto input = semantic.parameterTypes.front();
        if (input.kind == TypeKind::View && input.arguments.size() == 1) {
            input = input.arguments.front();
        }
        workflow.inputType = input;
        if (!isResult(semantic.returnType) || semantic.returnType.arguments.size() != 2) {
            diagnostics_.error("FDN2227", "workflow callable type must be a Result",
                               function.returnType.span);
        } else {
            requireSame(workflow.successType, semantic.returnType.arguments[0], function.span,
                        "workflow output");
            workflow.failureType = semantic.returnType.arguments[1];
            if (source.kind == WorkflowKind::Pipeline) {
                requireSame(workflow.errorType, workflow.failureType, function.span,
                            "pipeline error");
            } else if (workflow.failureType.kind != TypeKind::Enum ||
                       workflow.failureType.declaration >= model_.enums.size() ||
                       model_.enums[workflow.failureType.declaration].payloadTypes.size() != 2 ||
                       !model_.enums[workflow.failureType.declaration]
                            .payloadTypes[1]
                            .has_value()) {
                diagnostics_.error("FDN2227", "saga failure type is invalid", function.span);
            } else {
                workflow.failureDetailsType = substitute(
                    *model_.enums[workflow.failureType.declaration].payloadTypes[1],
                    workflow.failureType.arguments);
            }
        }

        auto current = input;
        for (std::size_t index = 0; index < source.steps.size(); ++index) {
            const auto &step = source.steps[index];
            const auto last = index + 1 == source.steps.size();
            const auto expected = source.kind == WorkflowKind::Saga
                                      ? std::optional<Type>{last ? workflow.successType : voidType}
                                      : std::nullopt;
            const auto target = analyzeWorkflowTarget(
                step.function, source.kind == WorkflowKind::Pipeline ? current : input,
                workflow.errorType, expected, step.functionSpan, "workflow step");
            SemanticWorkflowStep lowered;
            lowered.name = step.name;
            lowered.attempts = step.attempts;
            if (target.has_value()) {
                lowered.function = target->function;
                lowered.typeArguments = target->typeArguments;
                if (source.kind == WorkflowKind::Pipeline) {
                    current = target->successType;
                }
            }
            if (step.compensation.has_value() && step.compensationSpan.has_value()) {
                const auto compensation = analyzeWorkflowTarget(
                    *step.compensation, input, workflow.errorType, voidType,
                    *step.compensationSpan, "saga compensation");
                if (compensation.has_value()) {
                    lowered.compensation = compensation->function;
                    lowered.compensationTypeArguments = compensation->typeArguments;
                }
            }
            workflow.steps.push_back(std::move(lowered));
        }
        if (source.kind == WorkflowKind::Pipeline) {
            requireSame(workflow.successType, current, function.span, "pipeline output");
        }
        semantic.workflow = std::move(workflow);
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
        taskWaitRoot_.reset();
        taskWaitVoidStatement_ = false;
        channelStorage_ = 0;
        blockingStorage_ = 0;
        callbackStorage_ = 0;
        iterationStorage_ = 0;
        loopScopeBases_.clear();
        loopJumps_ = 0;
        unsafeDepth_ = 0;

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

        if (function.stateTransition.has_value()) {
            const auto owner = enums_.find(function.ownerType);
            if (owner == enums_.end()) {
                diagnostics_.error("FDN2196", "state transition owner is not an enum",
                                   function.span);
                return;
            }
            const auto machine = Type{TypeKind::Enum, owner->second};
            const auto receiver = Type{TypeKind::Edit, 0, {machine}};
            if (semantic.parameterTypes.empty()) {
                diagnostics_.error("FDN2196", "state transition requires an edit receiver",
                                   function.span);
                return;
            }
            requireSame(receiver, semantic.parameterTypes.front(), function.span,
                        "state transition receiver");
            const auto &transition = *function.stateTransition;
            if (transition.sourceVariants.empty()) {
                diagnostics_.error("FDN2197", "state transition requires a valid source state",
                                   function.span);
            }
            if (transition.destinationVariant >=
                model_.enums[owner->second].payloadTypes.size()) {
                diagnostics_.error("FDN2198", "state transition destination is invalid",
                                   function.span);
                return;
            }
            const auto payload =
                model_.enums[owner->second].payloadTypes[transition.destinationVariant];
            if (payload.has_value()) {
                if (!transition.destinationParameter.has_value() ||
                    *transition.destinationParameter >= semantic.parameterTypes.size()) {
                    diagnostics_.error("FDN2198",
                                       "state transition destination payload is missing",
                                       function.span);
                } else {
                    const auto parameter = *transition.destinationParameter;
                    requireSame(*payload, semantic.parameterTypes[parameter],
                                function.parameters[parameter].span,
                                "state transition destination payload");
                }
            } else if (transition.destinationParameter.has_value()) {
                diagnostics_.error("FDN2198",
                                   "unit state transition cannot store a payload",
                                   function.span);
            }
            if (!isResult(semantic.returnType) || semantic.returnType.arguments.size() != 2 ||
                semantic.returnType.arguments.front() != voidType) {
                diagnostics_.error("FDN2199",
                                   "state transition must return Result<void, E>",
                                   function.returnType.span);
            }
            return;
        }

        if (function.stateTimeout.has_value()) {
            const auto owner = enums_.find(function.ownerType);
            if (owner == enums_.end() || owner->second >= program_.enums.size() ||
                !program_.enums[owner->second].stateMachine) {
                diagnostics_.error("FDN2230", "state timeout owner is not a state machine",
                                   function.span);
                return;
            }
            const auto machine = Type{TypeKind::Enum, owner->second};
            const auto readableMachine = isCopyParameterType(machine)
                                             ? machine
                                             : Type{TypeKind::View, 0, {machine}};
            if (semantic.parameterTypes.size() != 1) {
                diagnostics_.error("FDN2231", "state timeout accessor requires one state",
                                   function.span);
            } else {
                requireSame(readableMachine, semantic.parameterTypes.front(), function.span,
                            "state timeout accessor state");
            }
            if (!isOption(semantic.returnType) ||
                semantic.returnType.arguments.size() != 1 ||
                semantic.returnType.arguments.front() != u64Type) {
                diagnostics_.error("FDN2232", "state timeout accessor must return Option<u64>",
                                   function.returnType.span);
            }
            const auto &timeout = *function.stateTimeout;
            if (timeout.sourceVariants.empty() || timeout.nanoseconds == 0) {
                diagnostics_.error("FDN2233", "state timeout metadata is invalid",
                                   function.span);
            }
            return;
        }

        if (function.workflow.has_value()) {
            analyzeWorkflow(function, semantic);
            return;
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
            const auto previousTaskWaitRoot = taskWaitRoot_;
            if (program_.functions[currentFunction_].task && !hasElse) {
                taskWaitRoot_ = variable->initializer;
            }
            const auto initializer = analyzeExpression(
                variable->initializer,
                !hasElse && declared.kind != TypeKind::Invalid ? std::optional<Type>{declared}
                                                               : std::nullopt);
            taskWaitRoot_ = previousTaskWaitRoot;
            if (hasElse) {
                if (!isResult(initializer) || initializer.arguments.size() != 2) {
                    diagnostics_.error("FDN2053", "const else requires a Result initializer",
                                       statement.span);
                    declared = invalidType;
                } else {
                    const auto payload = initializer.arguments[0];
                    if (variable->type.has_value()) {
                        requireSame(declared, payload, statement.span, "const else payload");
                    } else {
                        declared = payload;
                    }
                    const auto successState = resultOutstanding_;
                    const auto successMoves = moveStates_;
                    const auto successLoans = loanStates_;
                    scopes_.emplace_back();
                    const auto errorLocal = addLocal(
                        variable->elseBinding.value_or("$constElseError" + std::to_string(id)),
                        initializer.arguments[1], false, statement.span);
                    if (!variable->elseBinding.has_value()) {
                        resultOutstanding_[errorLocal] = false;
                    }
                    model_.statementElseLocals[id] = errorLocal;
                    const auto exits = analyzeBlock(*variable->elseBlock, false);
                    reportScope(scopes_.back());
                    scopes_.pop_back();
                    restoreOutstanding(successState);
                    restoreMoves(successMoves);
                    restoreLoans(successLoans);
                    if (!exits) {
                        diagnostics_.error("FDN2054", "const else block must exit",
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
                diagnostics_.error("FDN2099", "contract value requires view, edit, or own",
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

            if (destructure->type.name == "Channel") {
                if (source.kind != TypeKind::Channel || source.arguments.size() != 1) {
                    diagnostics_.error(
                        "FDN2130",
                        "Channel destructuring requires a Channel value or owner",
                        statement.span);
                }
                if (!destructure->type.arguments.empty()) {
                    const auto pattern = resolveType(destructure->type);
                    requireSame(pattern, source, statement.span, "Channel pattern");
                }
                std::vector<bool> bound(2);
                std::vector<FirFieldId> fields;
                std::vector<FirLocalId> bindings;
                for (const auto &pattern : destructure->fields) {
                    std::optional<FirFieldId> field;
                    if (pattern.field == "sender") {
                        field = 0;
                    } else if (pattern.field == "receiver") {
                        field = 1;
                    }
                    if (!field.has_value()) {
                        diagnostics_.error("FDN2025", "unknown Channel field " + pattern.field,
                                           pattern.span);
                        bindings.push_back(
                            addLocal(pattern.binding, invalidType, false, pattern.span));
                        fields.push_back(0);
                        continue;
                    }
                    if (bound[*field]) {
                        diagnostics_.error("FDN2132",
                                           "duplicate field pattern " + pattern.field,
                                           pattern.span);
                    }
                    bound[*field] = true;
                    const auto payload = source.kind == TypeKind::Channel &&
                                                 source.arguments.size() == 1
                                             ? source.arguments.front()
                                             : invalidType;
                    const auto endpoint = Type{*field == 0 ? TypeKind::Sender
                                                           : TypeKind::Receiver,
                                               0, {payload}};
                    bindings.push_back(
                        addLocal(pattern.binding, endpoint, false, pattern.span));
                    fields.push_back(*field);
                }
                if (!bound[0]) {
                    diagnostics_.error("FDN2133", "Channel pattern is missing field sender",
                                       statement.span);
                }
                if (!bound[1]) {
                    diagnostics_.error("FDN2133", "Channel pattern is missing field receiver",
                                       statement.span);
                }
                model_.statementStructTargets[id] = StructDestructureTarget{
                    source.kind == TypeKind::Channel ? source : invalidType, owned,
                    std::move(fields), std::move(bindings)};
                return false;
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
                std::optional<Type> expected;
                if (local.has_value()) {
                    const auto &declaration =
                        model_.functions[currentFunction_].locals[*local];
                    expected = declaration.type;
                    if (declaration.readBinding &&
                        (expected->kind == TypeKind::View ||
                         expected->kind == TypeKind::Edit) &&
                        expected->arguments.size() == 1) {
                        expected = expected->arguments.front();
                    }
                }
                const auto value = analyzeExpression(assignment->value, expected);
                if (!local.has_value()) {
                    return false;
                }
                model_.expressionLocals[assignment->target] = *local;
                model_.expressionTypes[assignment->target] = *expected;
                model_.expressionReads[assignment->target] =
                    model_.functions[currentFunction_].locals[*local].readBinding;
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
                requireSame(*expected, value, statement.span, "assignment");
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
        if (const auto *resultElse = std::get_if<ResultElseStatement>(&statement.value)) {
            const auto previousTaskWaitRoot = taskWaitRoot_;
            const auto previousTaskWaitVoidStatement = taskWaitVoidStatement_;
            if (program_.functions[currentFunction_].task) {
                taskWaitRoot_ = resultElse->expression;
                taskWaitVoidStatement_ = true;
            }
            const auto result = analyzeExpression(resultElse->expression);
            taskWaitRoot_ = previousTaskWaitRoot;
            taskWaitVoidStatement_ = previousTaskWaitVoidStatement;
            if (!isResult(result) || result.arguments.size() != 2) {
                diagnostics_.error("FDN2053", "expression else requires a Result value",
                                   statement.span);
                return false;
            }
            if (result.arguments.front() != voidType) {
                diagnostics_.error(
                    "FDN2053",
                    "expression else requires Result<void, E>; bind a non-void success value",
                    statement.span);
            }
            const auto successState = resultOutstanding_;
            const auto successMoves = moveStates_;
            const auto successLoans = loanStates_;
            scopes_.emplace_back();
            const auto errorLocal = addLocal(
                resultElse->errorBinding.value_or("$resultElseError" + std::to_string(id)),
                result.arguments[1], false, statement.span);
            if (!resultElse->errorBinding.has_value()) {
                resultOutstanding_[errorLocal] = false;
            }
            model_.statementElseLocals[id] = errorLocal;
            const auto exits = analyzeBlock(resultElse->elseBlock, false);
            reportScope(scopes_.back());
            scopes_.pop_back();
            restoreOutstanding(successState);
            restoreMoves(successMoves);
            restoreLoans(successLoans);
            if (!exits) {
                diagnostics_.error("FDN2054", "expression else block must exit",
                                   statement.span);
            }
            return false;
        }
        if (const auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            const auto previousTaskWaitRoot = taskWaitRoot_;
            const auto previousTaskWaitVoidStatement = taskWaitVoidStatement_;
            if (program_.functions[currentFunction_].task) {
                taskWaitRoot_ = expression->expression;
                taskWaitVoidStatement_ = true;
            }
            const auto type = analyzeExpression(expression->expression);
            taskWaitRoot_ = previousTaskWaitRoot;
            taskWaitVoidStatement_ = previousTaskWaitVoidStatement;
            if (isResult(type)) {
                diagnostics_.error("FDN2051", "Result value must be handled or discarded",
                                   statement.span);
            }
            if (requiresDrop(type)) {
                diagnostics_.error("FDN2076", "owned value must be handled or discarded",
                                   statement.span);
            }
            const auto &target = model_.callTargets[expression->expression];
            if (type == neverType ||
                (target.has_value() && target->kind == CallTargetKind::Panic)) {
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
            if (returned->tail && expected == voidType) {
                const auto type = analyzeExpression(*returned->value);
                if (isResult(type)) {
                    diagnostics_.error("FDN2051", "Result value must be handled or discarded",
                                       statement.span);
                }
                if (requiresDrop(type)) {
                    diagnostics_.error("FDN2076", "owned value must be handled or discarded",
                                       statement.span);
                }
                const auto &target = model_.callTargets[*returned->value];
                if (type == neverType ||
                    (target.has_value() && target->kind == CallTargetKind::Panic)) {
                    std::fill(resultOutstanding_.begin(), resultOutstanding_.end(), false);
                    return true;
                }
                return false;
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
            const auto previousTaskWaitRoot = taskWaitRoot_;
            const auto previousTaskWaitVoidStatement = taskWaitVoidStatement_;
            if (program_.functions[currentFunction_].task) {
                taskWaitRoot_ = discarded->value;
                taskWaitVoidStatement_ = true;
            }
            static_cast<void>(analyzeExpression(discarded->value));
            taskWaitRoot_ = previousTaskWaitRoot;
            taskWaitVoidStatement_ = previousTaskWaitVoidStatement;
            return false;
        }
        if (const auto *unsafe = std::get_if<UnsafeStatement>(&statement.value)) {
            if (!unsafe->safetyProof) {
                diagnostics_.error(
                    "FDN2212",
                    "unsafe block requires an immediately preceding // SAFETY: proof",
                    statement.span);
            }
            ++unsafeDepth_;
            const auto returns = analyzeBlock(unsafe->body, true);
            --unsafeDepth_;
            return returns;
        }
        if (std::holds_alternative<BreakStatement>(statement.value) ||
            std::holds_alternative<ContinueStatement>(statement.value)) {
            const auto keyword = std::holds_alternative<BreakStatement>(statement.value)
                                     ? "break"
                                     : "continue";
            if (loopScopeBases_.empty()) {
                diagnostics_.error("FDN2200", std::string(keyword) +
                                                  " is only available inside a loop",
                                   statement.span);
                return true;
            }
            reportScopesFrom(loopScopeBases_.back());
            model_.statementDrops[id] = activeDropsFrom(loopScopeBases_.back());
            return true;
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

        if (const auto *loop = std::get_if<ForStatement>(&statement.value)) {
            const auto loansBefore = loanStates_;
            auto sequence = analyzeExpression(loop->sequence, std::nullopt,
                                              ExpressionUse::Inspect);
            auto base = sequence;
            if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
                 base.kind == TypeKind::Edit) &&
                base.arguments.size() == 1) {
                base = base.arguments.front();
            }
            const auto indexedSequence =
                (base.kind == TypeKind::Array || base.kind == TypeKind::Slice) &&
                base.arguments.size() == 1;
            auto iterator = false;
            auto nextFunction = FirFunctionId{};
            std::vector<Type> nextTypeArguments;
            auto nextResult = invalidType;
            auto element = indexedSequence ? base.arguments.front() : invalidType;
            if (!indexedSequence && base.kind == TypeKind::Struct &&
                base.declaration < methods_.size()) {
                const auto found = methods_[base.declaration].find("Next");
                if (found != methods_[base.declaration].end()) {
                    nextFunction = found->second;
                    const auto &declaration = program_.functions[nextFunction];
                    const auto &signature = signatures_[nextFunction];
                    nextTypeArguments = base.arguments;
                    nextResult = substitute(signature.returnType, nextTypeArguments);
                    iterator = declaration.receiver == ReceiverKind::Edit &&
                               signature.parameters.size() == 1 && isOption(nextResult) &&
                               nextResult.arguments.size() == 1;
                    if (iterator) {
                        element = nextResult.arguments.front();
                    }
                }
                if (!iterator) {
                    diagnostics_.error(
                        "FDN2204",
                        "iterator requires fn Next(&self) Option<T>", statement.span);
                }
            } else if (!indexedSequence) {
                diagnostics_.error("FDN2201",
                                   "for requires an array, sequence view, or iterator",
                                   statement.span);
            }
            if (iterator && loop->editable) {
                diagnostics_.error("FDN2205",
                                   "iterator values cannot use editable iteration",
                                   statement.span);
            } else if (!iterator && loop->editable &&
                       (!isPlaceExpression(loop->sequence) ||
                        !editablePlace(loop->sequence))) {
                diagnostics_.error("FDN2202", "editable iteration requires a mutable place",
                                   statement.span);
            }
            if (!iterator && loop->editable && sequence.kind == TypeKind::View) {
                diagnostics_.error("FDN2071", "shared view cannot become an edit",
                                   statement.span);
            }
            if (iterator && isPlaceExpression(loop->sequence) &&
                (sequence.kind == TypeKind::View || !editablePlace(loop->sequence))) {
                diagnostics_.error("FDN2202", "iterator requires a mutable place",
                                   statement.span);
            }
            if (!iterator && program_.functions[currentFunction_].task &&
                !isPlaceExpression(loop->sequence)) {
                diagnostics_.error(
                    "FDN2203",
                    "task iteration requires a bound sequence so it survives suspension",
                    statement.span);
            }

            const auto root = placeRootLocal(loop->sequence);
            if (root.has_value()) {
                const auto requested = iterator || loop->editable ? LoanState::Edit
                                                                  : LoanState::View;
                if ((requested == LoanState::View && loanStates_[*root] == LoanState::Edit) ||
                    (requested == LoanState::Edit &&
                     loanStates_[*root] != LoanState::None)) {
                    diagnostics_.error(
                        "FDN2073",
                        "conflicting borrow of binding " +
                            model_.functions[currentFunction_].locals[*root].name,
                        statement.span);
                } else if (requested == LoanState::Edit ||
                           loanStates_[*root] == LoanState::None) {
                    loanStates_[*root] = requested;
                }
            }

            const auto before = resultOutstanding_;
            const auto movesBefore = moveStates_;
            const auto outerLocals = movesBefore.size();
            const auto persistentScope = scopes_.size();
            scopes_.emplace_back();
            const auto suffix = std::to_string(iterationStorage_++);
            const auto ownsSequence = iterator && !isPlaceExpression(loop->sequence);
            const auto sequenceType = iterator
                                          ? (ownsSequence
                                                 ? base
                                                 : Type{TypeKind::Edit, 0, {base}})
                                          : Type{
                                                loop->editable ? TypeKind::Edit : TypeKind::View,
                                                0, {Type{TypeKind::Slice, 0, {element}}}};
            const auto sequenceStorage =
                addLocal("$forSequence" + suffix, sequenceType, ownsSequence, statement.span);
            scopes_.emplace_back();
            const auto loopScope = persistentScope + 1;
            const auto index = addLocal(loop->indexBinding.value_or("$forIndex" + suffix),
                                        usizeType, false, statement.span);
            auto valueType = element;
            auto readBinding = false;
            auto mutableBinding = false;
            if (!iterator && loop->editable) {
                valueType = Type{TypeKind::Edit, 0, {element}};
                readBinding = true;
                mutableBinding = true;
            } else if (!iterator && requiresDrop(element)) {
                valueType = Type{TypeKind::View, 0, {element}};
                readBinding = true;
            }
            const auto value =
                addLocal(loop->valueBinding, valueType, mutableBinding, statement.span);
            model_.functions[currentFunction_].locals[value].readBinding = readBinding;

            const auto loopJumpsBefore = loopJumps_;
            loopScopeBases_.push_back(loopScope);
            const auto bodyReturns = analyzeBlock(loop->body, false);
            loopScopeBases_.pop_back();
            const auto bodyHasLoopJump = loopJumps_ != loopJumpsBefore;
            const auto bodyState = outstandingPrefix(before.size());
            const auto bodyMoves = movePrefix(outerLocals);
            model_.blockDrops[loop->body] = scopeDrops(scopes_.back());
            reportScope(scopes_.back());
            scopes_.pop_back();
            reportScope(scopes_.back());
            scopes_.pop_back();

            std::vector<bool> merged(before.size());
            for (std::size_t local = 0; local < before.size(); ++local) {
                merged[local] = before[local] || bodyState[local];
            }
            restoreOutstanding(merged);
            std::vector<MoveState> loopMoves(outerLocals);
            for (std::size_t local = 0; local < outerLocals; ++local) {
                if ((!bodyReturns || bodyHasLoopJump) &&
                    movesBefore[local] != bodyMoves[local]) {
                    diagnostics_.error(
                        "FDN2079",
                        "loop body cannot leave binding " +
                            model_.functions[currentFunction_].locals[local].name + " moved",
                        statement.span);
                }
                loopMoves[local] = (bodyReturns && !bodyHasLoopJump) ||
                                           movesBefore[local] == bodyMoves[local]
                                       ? movesBefore[local]
                                       : MoveState::MaybeMoved;
            }
            restoreMoves(loopMoves);
            restoreLoans(loansBefore);
            model_.forTargets[id] = ForTarget{
                sequenceStorage, index, value, sequenceType, loop->editable, iterator,
                nextFunction, std::move(nextTypeArguments), nextResult, ownsSequence};
            return false;
        }

        if (const auto *selection = std::get_if<SelectStatement>(&statement.value)) {
            if (!program_.functions[currentFunction_].task) {
                diagnostics_.error("FDN2174", "select is only available inside a task",
                                   statement.span);
            }

            if (selection->timeout.has_value() &&
                selection->timeout->duration.has_value()) {
                requireSame(u64Type,
                            analyzeExpression(*selection->timeout->duration),
                            selection->timeout->span,
                            "dynamic select timeout");
            }

            std::vector<Type> payloads;
            std::vector<std::optional<FirLocalId>> bindings(selection->operations.size());
            payloads.reserve(selection->operations.size());
            for (const auto &arm : selection->operations) {
                const auto previousTaskWaitRoot = taskWaitRoot_;
                taskWaitRoot_ = arm.operation;
                const auto result = analyzeExpression(arm.operation);
                taskWaitRoot_ = previousTaskWaitRoot;
                if (!isResult(result) || result.arguments.size() != 2) {
                    diagnostics_.error("FDN2175",
                                       "select operation must be Sender.send or Receiver.receive",
                                       arm.span);
                    payloads.push_back(invalidType);
                    continue;
                }
                payloads.push_back(result.arguments[0]);
                if (!model_.channelOperationTargets[arm.operation].has_value()) {
                    diagnostics_.error("FDN2175",
                                       "select operation must be Sender.send or Receiver.receive",
                                       arm.span);
                }
            }

            const auto baselineOutstanding = resultOutstanding_;
            const auto baselineMoves = moveStates_;
            const auto baselineLoans = loanStates_;
            const auto baselineCount = baselineMoves.size();
            std::vector<std::vector<bool>> branchOutstanding;
            std::vector<std::vector<MoveState>> branchMoves;
            std::vector<std::vector<LoanState>> branchLoans;
            std::vector<bool> branchReturns;

            const auto analyzeArm = [&](AstBlockId body,
                                        std::optional<std::pair<std::string, Type>> binding,
                                        std::optional<FirLocalId *> target) {
                restoreOutstanding(baselineOutstanding);
                restoreMoves(baselineMoves);
                restoreLoans(baselineLoans);
                scopes_.emplace_back();
                if (binding.has_value()) {
                    const auto local = addLocal(binding->first, binding->second, false,
                                                program_.blocks[body].span);
                    if (target.has_value()) {
                        **target = local;
                    }
                }
                const auto returns = analyzeBlock(body, false);
                reportScope(scopes_.back());
                scopes_.pop_back();
                branchOutstanding.push_back(outstandingPrefix(baselineOutstanding.size()));
                branchMoves.push_back(movePrefix(baselineCount));
                branchLoans.push_back(loanPrefix(baselineLoans.size()));
                branchReturns.push_back(returns);
            };

            for (std::size_t index = 0; index < selection->operations.size(); ++index) {
                const auto &arm = selection->operations[index];
                const auto payload = index < payloads.size() ? payloads[index] : invalidType;
                const auto &operation = model_.channelOperationTargets[arm.operation];
                const auto send = operation.has_value() &&
                                  operation->kind == ChannelOperationKind::Send;
                if (arm.binding.has_value() && (send || payload == voidType)) {
                    diagnostics_.error("FDN2176",
                                       "select binding requires a receive payload", arm.span);
                }
                if (!arm.binding.has_value() && !send && payload != voidType &&
                    payload.kind != TypeKind::Invalid) {
                    diagnostics_.error("FDN2177",
                                       "select receive payload requires a const binding",
                                       arm.span);
                }
                std::optional<std::pair<std::string, Type>> binding;
                std::optional<FirLocalId *> target;
                if (arm.binding.has_value() && !send && payload != voidType) {
                    binding = std::pair{*arm.binding, payload};
                    target = &bindings[index].emplace();
                }
                analyzeArm(arm.body, std::move(binding), target);
            }
            if (selection->timeout.has_value()) {
                analyzeArm(selection->timeout->body, std::nullopt, std::nullopt);
            }

            const auto errorType = Type{TypeKind::Enum, enums_.at("ChannelError"), {}};
            std::optional<FirLocalId> errorLocal;
            analyzeArm(selection->errorBlock,
                       std::pair{selection->errorBinding, errorType},
                       &errorLocal.emplace());
            const auto deadlineStorage = addLocal(
                "$selectDeadline" + std::to_string(channelStorage_++), u64Type, false,
                statement.span);
            model_.selectTargets[id] =
                SelectTarget{std::move(bindings), *errorLocal, deadlineStorage};

            std::vector<bool> mergedOutstanding(baselineOutstanding.size(), false);
            auto mergedMoves = baselineMoves;
            auto mergedLoans = baselineLoans;
            auto hasContinuingBranch = false;
            for (std::size_t branch = 0; branch < branchReturns.size(); ++branch) {
                if (branchReturns[branch]) {
                    continue;
                }
                if (!hasContinuingBranch) {
                    mergedOutstanding = branchOutstanding[branch];
                    mergedMoves = branchMoves[branch];
                    mergedLoans = branchLoans[branch];
                    hasContinuingBranch = true;
                    continue;
                }
                for (std::size_t local = 0; local < mergedOutstanding.size(); ++local) {
                    mergedOutstanding[local] =
                        mergedOutstanding[local] || branchOutstanding[branch][local];
                    if (mergedMoves[local] != branchMoves[branch][local]) {
                        mergedMoves[local] = MoveState::MaybeMoved;
                    }
                    mergedLoans[local] =
                        mergeLoan(mergedLoans[local], branchLoans[branch][local]);
                }
            }
            restoreOutstanding(hasContinuingBranch ? mergedOutstanding
                                                   : baselineOutstanding);
            restoreMoves(hasContinuingBranch ? mergedMoves : baselineMoves);
            restoreLoans(hasContinuingBranch ? mergedLoans : baselineLoans);
            return !hasContinuingBranch;
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
        const auto loopJumpsBefore = loopJumps_;
        loopScopeBases_.push_back(scopes_.size());
        const auto bodyReturns = analyzeBlock(loop.body, true);
        loopScopeBases_.pop_back();
        const auto bodyHasLoopJump = loopJumps_ != loopJumpsBefore;
        const auto bodyState = outstandingPrefix(before.size());
        const auto bodyMoves = movePrefix(movesBefore.size());
        std::vector<bool> merged(before.size());
        for (std::size_t local = 0; local < before.size(); ++local) {
            merged[local] = before[local] || bodyState[local];
        }
        restoreOutstanding(merged);
        std::vector<MoveState> loopMoves(movesBefore.size());
        for (std::size_t local = 0; local < loopMoves.size(); ++local) {
            if ((!bodyReturns || bodyHasLoopJump) &&
                movesBefore[local] != bodyMoves[local]) {
                diagnostics_.error("FDN2079", "loop body cannot leave binding " +
                                                   model_.functions[currentFunction_]
                                                       .locals[local]
                                                       .name +
                                                   " moved",
                                   statement.span);
            }
            loopMoves[local] = (bodyReturns && !bodyHasLoopJump) ||
                                       movesBefore[local] == bodyMoves[local]
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
            type = expected.has_value() && isNumeric(*expected) ? *expected : i32Type;
            if (isInteger(type) &&
                !integerLiteralFits(type, integer->magnitude, integer->negative)) {
                diagnostics_.error("FDN2005",
                                   "integer literal does not fit " +
                                       std::string(typeName(type)),
                                   expression.span);
            }
        } else if (const auto *floating =
                       std::get_if<FloatingExpression>(&expression.value)) {
            type = expected == f32Type ? f32Type : f64Type;
            if (type == f32Type) {
                float value{};
                const auto conversion = std::from_chars(
                    floating->text.data(), floating->text.data() + floating->text.size(), value);
                if (conversion.ec != std::errc{} ||
                    conversion.ptr != floating->text.data() + floating->text.size()) {
                    diagnostics_.error("FDN2005", "floating-point literal does not fit f32",
                                       expression.span);
                }
            }
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
                const auto &semantic = model_.functions[currentFunction_];
                const auto parameter =
                    std::find(semantic.parameters.begin(), semantic.parameters.end(), *local);
                const auto parameterIndex = static_cast<std::size_t>(
                    std::distance(semantic.parameters.begin(), parameter));
                const auto targetRead = semantic.locals[*local].readBinding ||
                                        (parameter != semantic.parameters.end() &&
                                         parameterIndex < program_.functions[currentFunction_]
                                                              .parameters.size() &&
                                         program_.functions[currentFunction_]
                                                 .parameters[parameterIndex]
                                                 .mode == ParameterMode::Read);
                if (targetRead && use == ExpressionUse::Inspect &&
                    (type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
                    type.arguments.size() == 1) {
                    type = type.arguments.front();
                    model_.expressionReads[id] = true;
                }
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
                auto function = functions_.end();
                if (name->name.find('.') == std::string::npos && !currentPackage().empty()) {
                    function = functions_.find(std::string(currentPackage()) + '.' + name->name);
                }
                if (function == functions_.end()) {
                    function = functions_.find(name->name);
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
                if (program_.functions[functionId].task) {
                    diagnostics_.error("FDN2163", "task cannot be used as a function value",
                                       expression.span);
                }
                if (program_.functions[functionId].blocking) {
                    diagnostics_.error("FDN2178",
                                       "blocking function cannot be used as a function value",
                                       expression.span);
                }
                if (program_.functions[functionId].callback) {
                    diagnostics_.error("FDN2178",
                                       "callback function cannot be used as a function value",
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
                        const auto mode =
                            index < program_.functions[functionId].parameters.size()
                                ? program_.functions[functionId].parameters[index].mode
                                : ParameterMode::Bootstrap;
                        inferType(functionValueParameterType(signature.parameters[index], mode),
                                  expected->arguments[index + 1], inferred, expression.span,
                                  "function value parameter");
                    }
                }
                const auto typeArguments = completeInference(
                    inferred, program_.functions[functionId].typeParameters, expression.span,
                    name->name);
                verifyTransferableTypeArguments(functionId, typeArguments, expression.span);
                std::vector<Type> parts;
                parts.push_back(substitute(signature.returnType, typeArguments));
                for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
                    const auto mode = index < program_.functions[functionId].parameters.size()
                                          ? program_.functions[functionId].parameters[index].mode
                                          : ParameterMode::Bootstrap;
                    parts.push_back(functionValueParameterType(
                        substitute(signature.parameters[index], typeArguments), mode));
                }
                const auto qualifier = expected.has_value() &&
                                               isTransferableFunction(*expected)
                                           ? transferableFunctionQualifier
                                           : 0;
                type = Type{TypeKind::Function, qualifier, std::move(parts)};
                model_.functionValueTargets[id] =
                    FunctionValueTarget{functionId, std::move(typeArguments)};
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            type = analyzeUnary(id, *unary, expected, expression.span);
        } else if (const auto *ownership = std::get_if<OwnershipExpression>(&expression.value)) {
            type = analyzeOwnership(id, *ownership, expected, expression.span);
        } else if (const auto *spawn = std::get_if<SpawnExpression>(&expression.value)) {
            type = analyzeSpawn(id, *spawn, expression.span);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
            type = analyzeBinary(*binary, expected, expression.span);
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
        } else if (const auto *conditional =
                       std::get_if<ConditionalExpression>(&expression.value)) {
            type = analyzeConditional(id, *conditional, expected, expression.span);
        } else {
            type = analyzeMatch(id, std::get<MatchExpression>(expression.value), expected,
                                expression.span);
        }
        if (expected.has_value()) {
            if (const auto conversion = contractConversion(*expected, type);
                conversion.has_value()) {
                model_.expressionContractConversions[id] = *conversion;
                type = *expected;
            }
        }
        model_.expressionTypes[id] = type;
        return type;
    }

    Type analyzeOwnership(AstExpressionId id, const OwnershipExpression &ownership,
                          std::optional<Type> expected, SourceSpan span) {
        if (ownership.operation == OwnershipOperator::Transfer) {
            if (const auto *member = std::get_if<MemberExpression>(
                    &program_.expressions[ownership.operand].value);
                member != nullptr && member->invoked && member->member == "wait" &&
                member->base.has_value()) {
                if (!member->arguments.empty() || !member->typeArguments.empty()) {
                    diagnostics_.error("FDN2167", "Task.wait does not accept arguments", span);
                }
                const auto task = analyzeExpression(*member->base, std::nullopt,
                                                    ExpressionUse::Consume);
                if (program_.functions[currentFunction_].task &&
                    (taskWaitRoot_ != id ||
                     !std::holds_alternative<NameExpression>(
                         program_.expressions[*member->base].value))) {
                    diagnostics_.error(
                        "FDN2168",
                        "suspending task wait must be a standalone binding or void statement",
                        span);
                }
                if (task.kind != TypeKind::Task || task.arguments.size() != 1) {
                    diagnostics_.error("FDN2166", "$value.wait() requires a Task", span);
                    return invalidType;
                }
                if (program_.functions[currentFunction_].task && taskWaitVoidStatement_ &&
                    task.arguments.front() != voidType) {
                    diagnostics_.error("FDN2168",
                                       "standalone suspending task wait requires Task<void>",
                                       span);
                }
                model_.taskWaitTargets[id] = TaskWaitTarget{*member->base};
                return task.arguments.front();
            }
            const auto value = analyzeExpression(ownership.operand, expected,
                                                 ExpressionUse::Consume);
            if (value.kind == TypeKind::View || value.kind == TypeKind::Edit) {
                diagnostics_.error("FDN2068", "cannot transfer a borrowed value", span);
                return invalidType;
            }
            model_.ownershipTargets[id] = OwnershipTarget{ownership.operation, std::nullopt};
            return value;
        }
        if (ownership.operation == OwnershipOperator::New) {
            if (!std::holds_alternative<StructExpression>(
                    program_.expressions[ownership.operand].value) &&
                !std::holds_alternative<CallExpression>(
                    program_.expressions[ownership.operand].value) &&
                !std::holds_alternative<MemberExpression>(
                    program_.expressions[ownership.operand].value)) {
                diagnostics_.error("FDN2191", "new requires a struct literal or call", span);
            }
            const auto value = analyzeExpression(ownership.operand, std::nullopt,
                                                 ExpressionUse::Consume);
            if (value == voidType || value.kind == TypeKind::Invalid ||
                value.kind == TypeKind::View || value.kind == TypeKind::Edit) {
                diagnostics_.error("FDN2068", "new requires an owned value", span);
                return invalidType;
            }
            model_.ownershipTargets[id] = OwnershipTarget{ownership.operation, std::nullopt};
            return value;
        }
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

        if (!isPlaceExpression(ownership.operand)) {
            static_cast<void>(analyzeExpression(ownership.operand, std::nullopt,
                                                ExpressionUse::Inspect));
            diagnostics_.error("FDN2069", "borrow requires a place", span);
            return invalidType;
        }
        const auto value = analyzeExpression(ownership.operand, std::nullopt,
                                             ExpressionUse::Inspect);
        const auto local = placeRootLocal(ownership.operand);
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
                diagnostics_.error("FDN2069", "borrow requires a place", span);
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
            !editablePlace(ownership.operand)) {
            diagnostics_.error("FDN2072", "edit requires a mutable place", span);
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

    Type analyzeSpawn(AstExpressionId, const SpawnExpression &spawn, SourceSpan span) {
        if (!std::holds_alternative<CallExpression>(program_.expressions[spawn.call].value)) {
            static_cast<void>(analyzeExpression(spawn.call));
            diagnostics_.error("FDN2163", "spawn requires a direct task call", span);
            return invalidType;
        }
        const auto previous = spawnCall_;
        spawnCall_ = spawn.call;
        const auto result = analyzeExpression(spawn.call);
        spawnCall_ = previous;
        const auto &target = model_.callTargets[spawn.call];
        if (!target.has_value() || target->kind != CallTargetKind::Function ||
            target->function >= program_.functions.size() ||
            !program_.functions[target->function].task) {
            return invalidType;
        }
        return Type{TypeKind::Task, 0, {result}};
    }

    Type analyzeUnary(AstExpressionId id, const UnaryExpression &unary,
                      std::optional<Type> expected,
                      SourceSpan span) {
        if (unary.operation == UnaryOperator::Dereference) {
            const auto operand = analyzeExpression(unary.operand, std::nullopt,
                                                   ExpressionUse::Inspect);
            if (operand.kind != TypeKind::Raw && operand.kind != TypeKind::RawConst) {
                diagnostics_.error("FDN2213", "unary * requires a raw pointer", span);
                return invalidType;
            }
            if (unsafeDepth_ == 0) {
                diagnostics_.error("FDN2213",
                                   "raw pointer dereference requires an unsafe block", span);
            }
            if (operand.arguments.size() != 1 || operand.arguments.front() == voidType) {
                diagnostics_.error("FDN2214", "void raw pointer cannot be dereferenced", span);
                return invalidType;
            }
            return operand.arguments.front();
        }
        const auto numericExpected = expected.has_value() &&
                                             (isSignedInteger(*expected) ||
                                              isFloating(*expected))
                                         ? expected
                                         : std::nullopt;
        const auto operand = analyzeExpression(unary.operand, numericExpected,
                                               ExpressionUse::Inspect);
        if (unary.operation == UnaryOperator::Negate) {
            if (!isSignedInteger(operand) && !isFloating(operand)) {
                diagnostics_.error("FDN2011",
                                   "unary - requires a signed integer or floating-point value",
                                   span);
                return invalidType;
            }
            return operand;
        }
        if (operand == boolType) {
            return boolType;
        }

        auto base = operand;
        if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
             base.kind == TypeKind::Edit) &&
            base.arguments.size() == 1) {
            base = base.arguments.front();
        }
        if (base.kind == TypeKind::String || base.kind == TypeKind::Array ||
            base.kind == TypeKind::Slice) {
            model_.emptyTests[id] = true;
            CallTarget target;
            target.kind = CallTargetKind::Len;
            target.receiver = unary.operand;
            model_.callTargets[id] = std::move(target);
            return boolType;
        }
        auto hasEmptyMethod = base.kind == TypeKind::Struct &&
                              base.declaration < methods_.size() &&
                              methods_[base.declaration].contains("IsEmpty");
        if (base.kind == TypeKind::Contract && base.declaration < model_.contracts.size()) {
            hasEmptyMethod = std::any_of(
                model_.contracts[base.declaration].methods.begin(),
                model_.contracts[base.declaration].methods.end(),
                [](const SemanticContractMethod &method) { return method.name == "IsEmpty"; });
        }
        if (hasEmptyMethod) {
            const MemberExpression method{unary.operand, "IsEmpty", {}, true, {}, {}, {}};
            const auto result = analyzeMethod(id, method, operand, base, span);
            const auto &target = model_.callTargets[id];
            auto readOnly = false;
            if (target.has_value() && target->kind == CallTargetKind::Method &&
                target->function < program_.functions.size()) {
                readOnly = program_.functions[target->function].receiver == ReceiverKind::View;
            } else if (target.has_value() && target->kind == CallTargetKind::ContractMethod &&
                       target->contract < model_.contracts.size() &&
                       target->method < model_.contracts[target->contract].methods.size()) {
                readOnly = model_.contracts[target->contract].methods[target->method].receiver ==
                           ReceiverKind::View;
            }
            if (!readOnly || result != boolType) {
                diagnostics_.error("FDN2220",
                                   "empty test requires fn IsEmpty(self) bool", span);
            }
            model_.emptyTests[id] = true;
            return boolType;
        }
        diagnostics_.error("FDN2220",
                           "unary ! requires bool, String, a sequence, or fn IsEmpty(self) bool",
                           span);
        return invalidType;
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
        auto &semantic = model_.functions[closureId];
        auto &signature = signatures_[closureId];
        auto contextual = expected;
        if (contextual.has_value() &&
            (contextual->kind == TypeKind::View || contextual->kind == TypeKind::Edit) &&
            contextual->arguments.size() == 1) {
            contextual = contextual->arguments.front();
        }
        const auto needsInference = closure.inferredReturn ||
                                    std::any_of(closure.parameters.begin(),
                                                closure.parameters.end(),
                                                [](const auto &parameter) {
                                                    return parameter.inferredType;
                                                });
        if (needsInference) {
            if (!contextual.has_value() || contextual->kind != TypeKind::Function) {
                diagnostics_.error(
                    "FDN2184",
                    "anonymous function signature requires an expected function type", span);
                return invalidType;
            }
            if (contextual->arguments.size() != closure.parameters.size() + 1) {
                diagnostics_.error(
                    "FDN2184",
                    "anonymous function parameter count does not match expected function type",
                    span);
                return invalidType;
            }
            if (closure.inferredReturn) {
                semantic.returnType = contextual->arguments.front();
            }
            auto modeMismatch = false;
            for (std::size_t index = 0; index < closure.parameters.size(); ++index) {
                if (!closure.parameters[index].inferredType) {
                    continue;
                }
                const auto target = contextual->arguments[index + 1];
                auto inferred = target;
                const auto mode = closure.parameters[index].mode;
                if (mode == ParameterMode::Read && target.kind == TypeKind::View &&
                    target.declaration == 1 && target.arguments.size() == 1) {
                    inferred = Type{TypeKind::View, 0, {target.arguments.front()}};
                } else if (mode == ParameterMode::Read && !isCopyParameterType(target)) {
                    inferred = invalidType;
                    modeMismatch = true;
                } else if (mode == ParameterMode::Edit && target.kind != TypeKind::Edit) {
                    inferred = invalidType;
                    modeMismatch = true;
                } else if (mode == ParameterMode::Transfer &&
                           (target.kind == TypeKind::View || target.kind == TypeKind::Edit)) {
                    inferred = invalidType;
                    modeMismatch = true;
                }
                semantic.parameterTypes[index] = inferred;
            }
            if (modeMismatch) {
                diagnostics_.error(
                    "FDN2011",
                    "anonymous parameter mode does not match expected function type", span);
                return invalidType;
            }
            signature = {semantic.returnType, semantic.parameterTypes};
        }
        if (closure.inferredReturn &&
            (containsBorrow(semantic.returnType) || containsBareSlice(semantic.returnType) ||
             containsBareContract(semantic.returnType))) {
            diagnostics_.error("FDN2063", "borrow cannot be returned from a function",
                               closure.span);
        }
        for (std::size_t index = 0; index < closure.parameters.size(); ++index) {
            if (!closure.parameters[index].inferredType) {
                continue;
            }
            const auto type = semantic.parameterTypes[index];
            if (type == voidType || type == neverType) {
                diagnostics_.error("FDN2016", "parameter cannot have type void or never",
                                   closure.parameters[index].span);
            }
            if (containsBareSlice(type)) {
                diagnostics_.error("FDN2080", "slice parameter requires view or edit",
                                   closure.parameters[index].span);
            }
            if (containsNestedBorrow(type, true)) {
                diagnostics_.error("FDN2064", "parameter contains a nested borrow",
                                   closure.parameters[index].span);
            }
            if (containsBareContract(type)) {
                diagnostics_.error("FDN2099", "contract value requires view, edit, or own",
                                   closure.parameters[index].span);
            }
        }
        std::vector<Type> parts;
        parts.reserve(signature.parameters.size() + 1);
        parts.push_back(signature.returnType);
        for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
            const auto mode = index < closure.parameters.size()
                                  ? closure.parameters[index].mode
                                  : ParameterMode::Bootstrap;
            parts.push_back(functionValueParameterType(signature.parameters[index], mode));
        }
        const auto qualifier = contextual.has_value() && isTransferableFunction(*contextual)
                                   ? transferableFunctionQualifier
                                   : 0;
        const Type closureType{TypeKind::Function, qualifier, std::move(parts)};
        if (contextual.has_value()) {
            requireSame(*contextual, closureType, span, "closure signature");
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
            auto captureMode = capture.mode;
            if (captureMode == CaptureMode::Copy && requiresDrop(type)) {
                captureMode = CaptureMode::View;
            }
            if (containsBorrow(type) && captureMode != CaptureMode::View &&
                captureMode != CaptureMode::Edit) {
                diagnostics_.error("FDN2123", "capture cannot own an existing borrow",
                                   capture.span);
            }
            if (captureMode == CaptureMode::Copy) {
                // Copyable captures are stored directly in the closure environment.
            } else if (captureMode == CaptureMode::Own) {
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
                const auto requested = captureMode == CaptureMode::View ? LoanState::View
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
            captureModes.push_back(captureMode);
            captureTypes.push_back(type);
        }

        if (isTransferableFunction(closureType)) {
            for (std::size_t index = 0; index < captureTypes.size(); ++index) {
                if (captureModes[index] == CaptureMode::View ||
                    captureModes[index] == CaptureMode::Edit) {
                    diagnostics_.error(
                        "FDN2185",
                        "transferable function cannot borrow capture " +
                            closure.captures[index].name,
                        closure.captures[index].span);
                } else if (!parallelTransferSafe(captureTypes[index])) {
                    diagnostics_.error(
                        "FDN2185",
                        "transferable function capture is not safe to transfer between threads: " +
                            displayType(captureTypes[index]),
                        closure.captures[index].span);
                }
            }
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
        requireSame(usizeType,
                    analyzeExpression(index.index, usizeType, ExpressionUse::Inspect), span,
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

    Type analyzeBinary(const BinaryExpression &binary, std::optional<Type> expected,
                       SourceSpan span) {
        const auto numericExpected = expected.has_value() && isNumeric(*expected)
                                         ? expected
                                         : std::nullopt;
        const auto left = analyzeExpression(binary.left, numericExpected,
                                            ExpressionUse::Inspect);
        const auto movesBeforeRight = moveStates_;
        const auto rightExpected = isNumeric(left) ? std::optional<Type>{left}
                                   : (left.kind == TypeKind::Raw ||
                                      left.kind == TypeKind::RawConst)
                                       ? std::optional<Type>{usizeType}
                                       : std::nullopt;
        const auto right = analyzeExpression(binary.right, rightExpected,
                                             ExpressionUse::Inspect);
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
            if (left.kind == TypeKind::Raw || left.kind == TypeKind::RawConst) {
                if (unsafeDepth_ == 0) {
                    diagnostics_.error("FDN2213",
                                       "raw pointer arithmetic requires an unsafe block", span);
                }
                if (left.arguments.size() != 1 || left.arguments.front() == voidType) {
                    diagnostics_.error("FDN2214",
                                       "void raw pointer does not support arithmetic", span);
                    return invalidType;
                }
                requireSame(usizeType, right, span, "raw pointer offset");
                return left;
            }
            if (left == stringType || right == stringType) {
                requireSame(stringType, left, span, "string concatenation operand");
                requireSame(stringType, right, span, "string concatenation operand");
                return stringType;
            }
            if (!isNumeric(left)) {
                diagnostics_.error("FDN2011", "arithmetic operand requires a numeric type", span);
                return invalidType;
            }
            requireSame(left, right, span, "arithmetic operand");
            return left;
        case BinaryOperator::Subtract:
            if (left.kind == TypeKind::Raw || left.kind == TypeKind::RawConst) {
                if (unsafeDepth_ == 0) {
                    diagnostics_.error("FDN2213",
                                       "raw pointer arithmetic requires an unsafe block", span);
                }
                if (left.arguments.size() != 1 || left.arguments.front() == voidType) {
                    diagnostics_.error("FDN2214",
                                       "void raw pointer does not support arithmetic", span);
                    return invalidType;
                }
                requireSame(usizeType, right, span, "raw pointer offset");
                return left;
            }
            [[fallthrough]];
        case BinaryOperator::Multiply:
        case BinaryOperator::Divide:
            if (!isNumeric(left)) {
                diagnostics_.error("FDN2011", "arithmetic operand requires a numeric type", span);
                return invalidType;
            }
            requireSame(left, right, span, "arithmetic operand");
            return left;
        case BinaryOperator::Remainder:
            if (!isInteger(left)) {
                diagnostics_.error("FDN2011", "remainder operand requires an integer type", span);
                return invalidType;
            }
            requireSame(left, right, span, "arithmetic operand");
            return left;
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual:
            if (!isNumeric(left)) {
                diagnostics_.error("FDN2011", "comparison operand requires a numeric type", span);
                return boolType;
            }
            requireSame(left, right, span, "comparison operand");
            return boolType;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            if (left.kind == TypeKind::Parameter || right.kind == TypeKind::Parameter ||
                left.kind == TypeKind::Struct || right.kind == TypeKind::Struct ||
                left.kind == TypeKind::Enum || right.kind == TypeKind::Enum ||
                left.kind == TypeKind::Function || right.kind == TypeKind::Function ||
                left.kind == TypeKind::Task || right.kind == TypeKind::Task ||
                left.kind == TypeKind::Channel || right.kind == TypeKind::Channel ||
                left.kind == TypeKind::Sender || right.kind == TypeKind::Sender ||
                left.kind == TypeKind::Receiver || right.kind == TypeKind::Receiver) {
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

    Type analyzeCallArgument(AstExpressionId argument, std::optional<Type> expected,
                             ParameterMode mode, SourceSpan span,
                             std::optional<Type> &implicitBorrow) {
        const auto *ownership = std::get_if<OwnershipExpression>(
            &program_.expressions[argument].value);
        if (mode == ParameterMode::Edit &&
            (ownership == nullptr || ownership->operation != OwnershipOperator::Edit)) {
            diagnostics_.error("FDN2192", "edit parameter requires & at the call site", span);
        }
        if (mode != ParameterMode::Read) {
            const auto actual = analyzeExpression(argument, expected, ExpressionUse::Consume);
            if (mode == ParameterMode::Transfer && requiresDrop(actual) &&
                isPlaceExpression(argument) &&
                (ownership == nullptr ||
                 ownership->operation != OwnershipOperator::Transfer)) {
                diagnostics_.error("FDN2193", "transfer parameter requires $ at the call site",
                                   span);
            }
            return actual;
        }
        if (ownership != nullptr &&
            (ownership->operation == OwnershipOperator::Edit ||
             ownership->operation == OwnershipOperator::Transfer)) {
            diagnostics_.error("FDN2194", "read parameter does not accept & or $", span);
        }
        if (!expected.has_value() || expected->kind != TypeKind::View ||
            expected->arguments.size() != 1) {
            const auto local = placeRootLocal(argument);
            const auto ownedCopy = expected.has_value() && local.has_value() &&
                                   isCopyParameterType(*expected) &&
                                   model_.functions[currentFunction_].locals[*local].type.kind ==
                                       TypeKind::Own &&
                                   model_.functions[currentFunction_]
                                           .locals[*local]
                                           .type.arguments.size() == 1 &&
                                   model_.functions[currentFunction_]
                                           .locals[*local]
                                           .type.arguments.front() == *expected;
            if (ownedCopy) {
                static_cast<void>(analyzeExpression(argument, std::nullopt,
                                                    ExpressionUse::Inspect));
                implicitBorrow = Type{TypeKind::View, 0, {*expected}};
                if (loanStates_[*local] == LoanState::Edit) {
                    diagnostics_.error(
                        "FDN2073",
                        "conflicting borrow of binding " +
                            model_.functions[currentFunction_].locals[*local].name,
                        span);
                } else if (loanStates_[*local] == LoanState::None) {
                    loanStates_[*local] = LoanState::View;
                }
                return *expected;
            }
            return analyzeExpression(argument, expected, ExpressionUse::Inspect);
        }
        if (ownership != nullptr && ownership->operation == OwnershipOperator::View) {
            return analyzeExpression(argument, expected, ExpressionUse::Consume);
        }

        const auto &readTarget = expected->arguments.front();
        const auto contextualExpected =
            readTarget.kind == TypeKind::Contract || readTarget.kind == TypeKind::Slice
                ? std::optional<Type>{}
                : std::optional<Type>{readTarget};
        const auto actual =
            analyzeExpression(argument, contextualExpected, ExpressionUse::Inspect);
        if (model_.expressionReads[argument] &&
            (readTarget.kind == TypeKind::Contract || readTarget.kind == TypeKind::Slice)) {
            return *expected;
        }
        if (actual.kind == TypeKind::View) {
            if (actual.arguments.size() == 1 && actual.arguments.front() == readTarget) {
                return *expected;
            }
            return actual;
        }
        if (isCopyParameterType(actual) && readTarget.kind != TypeKind::Contract &&
            readTarget.kind != TypeKind::Slice) {
            return actual;
        }
        auto target = actual;
        if ((actual.kind == TypeKind::Own || actual.kind == TypeKind::Edit) &&
            actual.arguments.size() == 1) {
            target = actual.arguments.front();
        }
        if (target.kind == TypeKind::Array && target.arguments.size() == 1) {
            target = Type{TypeKind::Slice, 0, {target.arguments.front()}};
        }
        const Type borrowed{TypeKind::View, expected->declaration, {target}};
        implicitBorrow = borrowed;

        if (const auto local = placeRootLocal(argument); local.has_value()) {
            if (loanStates_[*local] == LoanState::Edit) {
                diagnostics_.error(
                    "FDN2073",
                    "conflicting borrow of binding " +
                        model_.functions[currentFunction_].locals[*local].name,
                    span);
            } else if (loanStates_[*local] == LoanState::None) {
                loanStates_[*local] = LoanState::View;
            }
        }
        return borrowed;
    }

    bool hasNamedArguments(
        const std::vector<std::optional<std::string>> &argumentNames) const {
        return std::any_of(argumentNames.begin(), argumentNames.end(),
                           [](const auto &name) { return name.has_value(); });
    }

    void rejectNamedArguments(const std::vector<std::optional<std::string>> &argumentNames,
                              std::string_view target, SourceSpan span) {
        if (hasNamedArguments(argumentNames)) {
            diagnostics_.error("FDN2209",
                               std::string(target) + " does not accept named arguments", span);
        }
    }

    std::vector<std::size_t> mapArgumentParameters(
        const std::vector<std::optional<std::string>> &argumentNames,
        std::size_t argumentCount, const std::vector<std::string> &parameterNames,
        SourceSpan span) {
        std::vector<std::size_t> result(argumentCount);
        for (std::size_t index = 0; index < argumentCount; ++index) {
            result[index] = index;
        }
        if (!hasNamedArguments(argumentNames)) {
            return result;
        }

        std::vector<bool> assigned(parameterNames.size());
        std::size_t positional = 0;
        auto named = false;
        for (std::size_t source = 0; source < argumentCount; ++source) {
            const auto name = source < argumentNames.size() ? argumentNames[source]
                                                            : std::nullopt;
            std::size_t parameter = source;
            if (name.has_value()) {
                named = true;
                const auto found = std::find(parameterNames.begin(), parameterNames.end(), *name);
                if (found == parameterNames.end()) {
                    diagnostics_.error("FDN2206", "unknown named argument " + *name, span);
                    continue;
                }
                parameter = static_cast<std::size_t>(found - parameterNames.begin());
            } else {
                if (named) {
                    diagnostics_.error("FDN2207",
                                       "positional argument cannot follow a named argument",
                                       span);
                }
                while (positional < assigned.size() && assigned[positional]) {
                    ++positional;
                }
                parameter = positional++;
            }
            if (parameter >= assigned.size()) {
                continue;
            }
            result[source] = parameter;
            if (assigned[parameter]) {
                diagnostics_.error("FDN2208",
                                   "argument " + parameterNames[parameter] +
                                       " is supplied more than once",
                                   span);
            }
            assigned[parameter] = true;
        }
        return result;
    }

    Type analyzeCall(AstExpressionId id, const CallExpression &call, SourceSpan span) {
        const auto loansBefore = loanStates_;
        const auto borrowsAllowedBefore = transientBorrowsAllowed_;
        transientBorrowsAllowed_ = true;
        std::vector<Type> explicitTypes;
        explicitTypes.reserve(call.typeArguments.size());
        for (const auto &argument : call.typeArguments) {
            explicitTypes.push_back(resolveType(argument));
        }
        std::vector<std::size_t> argumentParameters(call.arguments.size());
        for (std::size_t index = 0; index < argumentParameters.size(); ++index) {
            argumentParameters[index] = index;
        }
        std::vector<std::optional<Type>> argumentExpectations(call.arguments.size());
        std::vector<ParameterMode> parameterModes(call.arguments.size(),
                                                  ParameterMode::Bootstrap);
        if (const auto local = lookupLocal(call.callee); local.has_value()) {
            rejectNamedArguments(call.argumentNames, "function value call", span);
            auto functionType = model_.functions[currentFunction_].locals[*local].type;
            if ((functionType.kind == TypeKind::View || functionType.kind == TypeKind::Edit) &&
                functionType.arguments.size() == 1) {
                functionType = functionType.arguments.front();
            }
            if (functionType.kind == TypeKind::Function && !functionType.arguments.empty()) {
                for (std::size_t index = 0;
                     index < call.arguments.size() && index + 1 < functionType.arguments.size();
                     ++index) {
                    argumentExpectations[index] = functionType.arguments[index + 1];
                    parameterModes[index] =
                        functionParameterMode(functionType.arguments[index + 1]);
                }
            }
        } else if (call.callee == "print" || call.callee == "panic") {
            rejectNamedArguments(call.argumentNames, call.callee, span);
            if (!argumentExpectations.empty()) {
                argumentExpectations.front() = stringType;
            }
        } else if (call.callee == "channel") {
            rejectNamedArguments(call.argumentNames, "channel", span);
            if (!argumentExpectations.empty()) {
                argumentExpectations.front() = u64Type;
            }
        } else if (call.callee == "len") {
            rejectNamedArguments(call.argumentNames, "len", span);
        } else {
            auto found = functions_.end();
            if (call.callee.find('.') == std::string::npos && !currentPackage().empty()) {
                found = functions_.find(std::string(currentPackage()) + '.' + call.callee);
            }
            if (found == functions_.end()) {
                found = functions_.find(call.callee);
            }
            if (found == functions_.end() && call.callee.find('.') == std::string::npos) {
                found = functions_.find("std.prelude." + call.callee);
            }
            if (found != functions_.end()) {
                const auto &declaration = program_.functions[found->second];
                const auto &signature = signatures_[found->second];
                std::vector<std::string> parameterNames;
                parameterNames.reserve(declaration.parameters.size());
                for (const auto &parameter : declaration.parameters) {
                    parameterNames.push_back(parameter.name);
                }
                argumentParameters = mapArgumentParameters(
                    call.argumentNames, call.arguments.size(), parameterNames, span);
                const auto hasCompleteExplicitTypes =
                    declaration.typeParameters.empty() ||
                    explicitTypes.size() == declaration.typeParameters.size();
                if (hasCompleteExplicitTypes) {
                    for (std::size_t source = 0; source < call.arguments.size(); ++source) {
                        const auto parameter = argumentParameters[source];
                        if (parameter < signature.parameters.size()) {
                            argumentExpectations[source] =
                                substitute(signature.parameters[parameter], explicitTypes);
                        }
                    }
                } else {
                    for (std::size_t source = 0; source < call.arguments.size(); ++source) {
                        const auto parameter = argumentParameters[source];
                        if (parameter < signature.parameters.size()) {
                            argumentExpectations[source] = signature.parameters[parameter];
                        }
                    }
                }
                for (std::size_t source = 0; source < parameterModes.size(); ++source) {
                    const auto parameter = argumentParameters[source];
                    if (parameter < declaration.parameters.size()) {
                        parameterModes[source] = declaration.parameters[parameter].mode;
                    }
                }
            }
        }
        std::vector<Type> arguments;
        std::vector<std::optional<Type>> implicitBorrows(call.arguments.size());
        arguments.reserve(call.arguments.size());
        for (std::size_t index = 0; index < argumentExpectations.size(); ++index) {
            if (argumentExpectations[index].has_value()) {
                argumentExpectations[index] = specializeReadParameter(
                    *argumentExpectations[index], parameterModes[index]);
            }
        }
        const auto inspectsArguments = call.callee == "print" || call.callee == "panic" ||
                                       call.callee == "len" || call.callee == "isNull";
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto argument = call.arguments[index];
            arguments.push_back(
                inspectsArguments
                    ? analyzeExpression(argument, argumentExpectations[index],
                                        ExpressionUse::Inspect)
                    : analyzeCallArgument(argument, argumentExpectations[index],
                                          parameterModes[index], span,
                                          implicitBorrows[index]));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;

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
            std::vector<std::optional<CallTarget::ContractConversion>> conversions(
                arguments.size());
            for (std::size_t index = 0; index < count; ++index) {
                const auto expected = functionType.arguments[index + 1];
                if (const auto conversion = contractConversion(expected, arguments[index]);
                    conversion.has_value()) {
                    conversions[index] = *conversion;
                } else {
                    requireSame(expected, arguments[index], span, "function value argument");
                }
                if (model_.expressionBorrowedClosures[call.arguments[index]] &&
                    expected.kind != TypeKind::View && expected.kind != TypeKind::Edit) {
                    diagnostics_.error("FDN2127", "borrowed closure cannot escape", span);
                }
            }
            CallTarget target;
            target.kind = CallTargetKind::FunctionValue;
            target.local = *local;
            target.argumentBorrows = std::move(implicitBorrows);
            target.argumentConversions = std::move(conversions);
            target.argumentParameters = std::move(argumentParameters);
            model_.callTargets[id] = std::move(target);
            return functionType.arguments.front();
        }

        if (call.callee == "channel") {
            if (explicitTypes.size() != 1) {
                diagnostics_.error("FDN2043", "channel expects one type argument", span);
            }
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "channel expects one capacity argument", span);
            } else {
                requireSame(u64Type, arguments.front(), span, "channel capacity");
            }
            const auto payload = explicitTypes.size() == 1 ? explicitTypes.front() : invalidType;
            if (containsBorrow(payload)) {
                diagnostics_.error("FDN2165", "channel payload cannot contain a borrow", span);
            }
            CallTarget target;
            target.kind = CallTargetKind::Channel;
            target.typeArguments.push_back(payload);
            model_.callTargets[id] = std::move(target);
            return Type{TypeKind::Channel, 0, {payload}};
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
            return neverType;
        }
        if (call.callee == "len") {
            if (!explicitTypes.empty()) {
                diagnostics_.error("FDN2043", "len does not accept type arguments", span);
            }
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "len expects one argument", span);
            } else {
                auto sequence = arguments.front();
                if ((sequence.kind == TypeKind::View || sequence.kind == TypeKind::Edit) &&
                    sequence.arguments.size() == 1) {
                    sequence = sequence.arguments.front();
                }
                if (sequence.kind != TypeKind::String && sequence.kind != TypeKind::Array &&
                    sequence.kind != TypeKind::Slice) {
                    diagnostics_.error("FDN2011", "len requires a String, array, or slice", span);
                }
            }
            CallTarget target;
            target.kind = CallTargetKind::Len;
            model_.callTargets[id] = std::move(target);
            return usizeType;
        }
        if (call.callee == "null") {
            if (explicitTypes.size() != 1 ||
                (explicitTypes.front().kind != TypeKind::Raw &&
                 explicitTypes.front().kind != TypeKind::RawConst)) {
                diagnostics_.error("FDN2214", "null expects one raw pointer type argument",
                                   span);
            }
            if (!arguments.empty()) {
                diagnostics_.error("FDN2010", "null does not accept value arguments", span);
            }
            if (unsafeDepth_ == 0) {
                diagnostics_.error("FDN2213", "raw pointer construction requires an unsafe block",
                                   span);
            }
            CallTarget target;
            target.kind = CallTargetKind::Null;
            target.typeArguments = explicitTypes;
            model_.callTargets[id] = std::move(target);
            return explicitTypes.size() == 1 ? explicitTypes.front() : invalidType;
        }
        if (call.callee == "isNull") {
            if (!explicitTypes.empty()) {
                diagnostics_.error("FDN2043", "isNull does not accept type arguments", span);
            }
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "isNull expects one argument", span);
            } else if (arguments.front().kind != TypeKind::Raw &&
                       arguments.front().kind != TypeKind::RawConst) {
                diagnostics_.error("FDN2214", "isNull requires a raw pointer", span);
            }
            CallTarget target;
            target.kind = CallTargetKind::IsNull;
            model_.callTargets[id] = std::move(target);
            return boolType;
        }

        auto found = functions_.end();
        if (call.callee.find('.') == std::string::npos && !currentPackage().empty()) {
            found = functions_.find(std::string(currentPackage()) + '.' + call.callee);
        }
        if (found == functions_.end()) {
            found = functions_.find(call.callee);
        }
        if (found == functions_.end() && call.callee.find('.') == std::string::npos) {
            found = functions_.find("std.prelude." + call.callee);
        }
        if (found == functions_.end()) {
            diagnostics_.error("FDN2009", "unknown function " + call.callee, span);
            return invalidType;
        }
        const auto function = found->second;
        if (program_.functions[function].task && spawnCall_ != id) {
            diagnostics_.error("FDN2163", "task call requires spawn", span);
        } else if (!program_.functions[function].task && spawnCall_ == id) {
            diagnostics_.error("FDN2163", "spawn requires a task call", span);
        }
        if (program_.functions[function].name == "main") {
            diagnostics_.error("FDN2019", "main cannot be called", span);
        }
        const auto &signature = signatures_[function];
        if (program_.functions[function].cSymbol.has_value() &&
            (containsRawPointer(signature.returnType) ||
             std::any_of(signature.parameters.begin(), signature.parameters.end(),
                         containsRawPointer)) &&
            unsafeDepth_ == 0) {
            diagnostics_.error("FDN2213",
                               "C ABI call with a raw pointer requires an unsafe block", span);
        }
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
        for (std::size_t source = 0; source < count; ++source) {
            const auto parameter = argumentParameters[source];
            if (parameter >= signature.parameters.size()) {
                continue;
            }
            auto pattern = signature.parameters[parameter];
            if (parameterModes[source] == ParameterMode::Read &&
                pattern.kind == TypeKind::View && pattern.arguments.size() == 1 &&
                isCopyParameterType(arguments[source])) {
                pattern = pattern.arguments.front();
            }
            inferType(pattern, arguments[source], inferred, span, "function argument");
        }
        const auto typeArguments = completeInference(
            inferred, program_.functions[function].typeParameters, span, call.callee);
        verifyTransferableTypeArguments(function, typeArguments, span);
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t source = 0; source < count; ++source) {
            const auto parameter = argumentParameters[source];
            if (parameter >= signature.parameters.size()) {
                continue;
            }
            const auto expected = specializeReadParameter(
                substitute(signature.parameters[parameter], typeArguments),
                parameterModes[source]);
            if (const auto conversion = contractConversion(expected, arguments[source]);
                conversion.has_value()) {
                conversions[source] = *conversion;
            } else {
                requireSame(expected, arguments[source], span, "function argument");
            }
            if (model_.expressionBorrowedClosures[call.arguments[source]] &&
                expected.kind != TypeKind::View && expected.kind != TypeKind::Edit) {
                diagnostics_.error("FDN2127", "borrowed closure cannot escape", span);
            }
        }
        CallTarget target;
        target.kind = CallTargetKind::Function;
        target.function = function;
        target.typeArguments = typeArguments;
        target.argumentBorrows = std::move(implicitBorrows);
        target.argumentConversions = std::move(conversions);
        target.argumentParameters = std::move(argumentParameters);
        model_.callTargets[id] = std::move(target);
        const auto returnType = substitute(signature.returnType, typeArguments);
        if (program_.functions[function].blocking) {
            if (!program_.functions[currentFunction_].task) {
                diagnostics_.error("FDN2180",
                                   "blocking call is only available inside a task", span);
            } else if (taskWaitRoot_ != id) {
                diagnostics_.error(
                    "FDN2168",
                    "blocking call must be a standalone task binding or discard", span);
            } else {
                std::vector<FirLocalId> storages;
                storages.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    storages.push_back(addLocal(
                        "$blockingArgument" + std::to_string(blockingStorage_++),
                        substitute(signature.parameters[index], typeArguments), false, span));
                }
                std::optional<FirLocalId> resultStorage;
                if (returnType != voidType) {
                    resultStorage = addLocal(
                        "$blockingResult" + std::to_string(blockingStorage_++),
                        returnType, false, span);
                    resultOutstanding_[*resultStorage] = false;
                }
                model_.blockingCallTargets[id] =
                    BlockingCallTarget{function, std::move(storages), resultStorage};
            }
        }
        if (program_.functions[function].callback &&
            !program_.functions[function].blocking) {
            if (!program_.functions[currentFunction_].task) {
                diagnostics_.error("FDN2183",
                                   "callback call is only available inside a task", span);
            } else if (taskWaitRoot_ != id) {
                diagnostics_.error(
                    "FDN2168",
                    "callback call must be a standalone task binding or discard", span);
            } else {
                std::vector<FirLocalId> storages;
                storages.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    storages.push_back(addLocal(
                        "$callbackArgument" + std::to_string(callbackStorage_++),
                        substitute(signature.parameters[index], typeArguments), false, span));
                }
                const auto resultStorage = addLocal(
                    "$callbackResult" + std::to_string(callbackStorage_++), i32Type,
                    false, span);
                model_.callbackCallTargets[id] =
                    CallbackCallTarget{function, std::move(storages), resultStorage};
            }
        }
        if (!program_.functions[currentFunction_].typeParameters.empty() &&
            !program_.functions[function].typeParameters.empty()) {
            genericCalls_.push_back({currentFunction_, function, typeArguments, span});
        }
        return returnType;
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
            for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
                const auto &parameter = signature.parameters[index];
                const auto concrete = substitute(parameter, target.typeArguments);
                const auto readVoid =
                    index < program_.functions[target.function].parameters.size() &&
                    program_.functions[target.function].parameters[index].mode ==
                        ParameterMode::Read &&
                    concrete.kind == TypeKind::View && concrete.arguments.size() == 1 &&
                    concrete.arguments.front() == voidType;
                if (concrete == voidType || readVoid) {
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
            auto layoutRoot = root;
            while ((layoutRoot.kind == TypeKind::Own ||
                    layoutRoot.kind == TypeKind::View ||
                    layoutRoot.kind == TypeKind::Edit) &&
                   layoutRoot.arguments.size() == 1) {
                layoutRoot = layoutRoot.arguments.front();
            }
            if (layoutRoot.kind != TypeKind::Struct && layoutRoot.kind != TypeKind::Enum) {
                continue;
            }
            const auto rootKey = semanticTypeKey(layoutRoot);
            if (validated.contains(rootKey)) {
                continue;
            }
            std::vector<Frame> stack;
            stack.push_back({layoutRoot, rootKey, layoutChildren(layoutRoot), 0});
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
            if (!initialized[field] && !declaration.fields[field].defaultFunction.has_value()) {
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
        std::vector<StructLiteralTarget::DefaultField> defaults;
        for (std::size_t field = 0; field < initialized.size(); ++field) {
            if (!initialized[field] && declaration.fields[field].defaultFunction.has_value()) {
                defaults.push_back({field, *declaration.fields[field].defaultFunction});
            }
        }
        Type result{TypeKind::Struct, type, arguments};
        model_.structTargets[id] =
            StructLiteralTarget{result, std::move(fields), std::move(defaults)};
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
            if (const auto targetType = machineScalarType(name->name);
                targetType.has_value()) {
                if (!member.invoked || member.member != "From") {
                    for (const auto argument : member.arguments) {
                        static_cast<void>(analyzeExpression(argument));
                    }
                    diagnostics_.error("FDN2100",
                                       "unknown numeric type member " + member.member, span);
                    return invalidType;
                }
                if (!name->typeArguments.empty() || !member.typeArguments.empty()) {
                    diagnostics_.error("FDN2043",
                                       "numeric conversion does not accept type arguments",
                                       span);
                }
                rejectNamedArguments(member.argumentNames, "numeric conversion", span);
                if (member.arguments.size() != 1) {
                    for (const auto argument : member.arguments) {
                        static_cast<void>(analyzeExpression(argument));
                    }
                    diagnostics_.error("FDN2010", "numeric conversion expects one argument",
                                       span);
                    return invalidType;
                }
                const auto sourceType = analyzeExpression(member.arguments.front());
                if (!isNumeric(sourceType)) {
                    diagnostics_.error("FDN2011", "numeric conversion requires a numeric value",
                                       span);
                    return invalidType;
                }
                CallTarget target;
                target.kind = CallTargetKind::NumericConversion;
                target.typeArguments = {sourceType, *targetType};
                model_.callTargets[id] = std::move(target);
                if (numericConversionIsInfallible(sourceType, *targetType)) {
                    return *targetType;
                }
                const auto result = enums_.find("Result");
                const auto numberError = enums_.find("NumberError");
                if (result == enums_.end() || numberError == enums_.end()) {
                    diagnostics_.error("FDN2017", "numeric conversion builtins are unavailable",
                                       span);
                    return invalidType;
                }
                return Type{TypeKind::Enum, result->second,
                            {*targetType, Type{TypeKind::Enum, numberError->second, {}}}};
            }
            auto ownerName = name->name;
            if (!structs_.contains(ownerName) && !enums_.contains(ownerName) &&
                ownerName.find('.') == std::string::npos && !currentPackage().empty()) {
                ownerName = std::string(currentPackage()) + '.' + ownerName;
            }
            const auto associatedName = ownerName + '.' + member.member;
            const auto associated = functions_.find(associatedName);
            if ((structs_.contains(ownerName) || enums_.contains(ownerName)) &&
                associated != functions_.end() &&
                !program_.functions[associated->second].receiver.has_value() &&
                !program_.functions[associated->second].ownerType.empty()) {
                if (!member.invoked) {
                    diagnostics_.error("FDN2190", "associated function must be called", span);
                    return invalidType;
                }
                if (!member.typeArguments.empty()) {
                    diagnostics_.error(
                        "FDN2043",
                        "associated function type arguments belong on the owner type", span);
                }
                const auto &declaration = program_.functions[associated->second];
                if (declaration.packageName != currentPackage() && !declaration.exported) {
                    diagnostics_.error("FDN3008",
                                       "associated function " + member.member +
                                           " is not exported",
                                       span);
                }
                return analyzeCall(
                    id,
                    CallExpression{associatedName, name->typeArguments, member.arguments,
                                   member.argumentNames, member.argumentNameSpans},
                    span);
            }
            if (structs_.contains(ownerName)) {
                for (const auto argument : member.arguments) {
                    static_cast<void>(analyzeExpression(argument));
                }
                diagnostics_.error("FDN2190",
                                   "unknown associated function " + member.member, span);
                return invalidType;
            }
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
        if (!member.invoked && member.member == "pointer" &&
            (((sourceType.kind == TypeKind::View || sourceType.kind == TypeKind::Edit) &&
              sourceType.arguments.size() == 1 &&
              sourceType.arguments.front().kind == TypeKind::Slice &&
              sourceType.arguments.front().arguments.size() == 1) ||
             (sourceType.kind == TypeKind::Slice && sourceType.arguments.size() == 1))) {
            if (!member.arguments.empty() || !member.typeArguments.empty()) {
                diagnostics_.error("FDN2100", "slice pointer does not accept arguments", span);
            }
            if (unsafeDepth_ == 0) {
                diagnostics_.error("FDN2213",
                                   "slice pointer access requires an unsafe block", span);
            }
            const auto editable = sourceType.kind == TypeKind::Edit;
            const auto element = sourceType.kind == TypeKind::Slice
                                     ? sourceType.arguments.front()
                                     : sourceType.arguments.front().arguments.front();
            return Type{editable ? TypeKind::Raw : TypeKind::RawConst, 0, {element}};
        }
        if ((sourceType.kind == TypeKind::Sender || sourceType.kind == TypeKind::Receiver) &&
            sourceType.arguments.size() == 1) {
            rejectNamedArguments(member.argumentNames, "channel operation", span);
            const auto send = sourceType.kind == TypeKind::Sender && member.member == "send";
            const auto receive =
                sourceType.kind == TypeKind::Receiver && member.member == "receive";
            const auto clone = sourceType.kind == TypeKind::Sender && member.member == "clone";
            if (!member.invoked || (!send && !receive && !clone)) {
                for (const auto argument : member.arguments) {
                    static_cast<void>(analyzeExpression(argument));
                }
                diagnostics_.error("FDN2100",
                                   "unknown " + std::string(typeName(sourceType)) +
                                       " member " + member.member,
                                   span);
                return invalidType;
            }
            if (!member.typeArguments.empty()) {
                diagnostics_.error("FDN2043", "channel operation does not accept type arguments",
                                   span);
            }
            if (clone) {
                if (!member.arguments.empty()) {
                    diagnostics_.error("FDN2010", "Sender.clone does not accept arguments",
                                       span);
                    for (const auto argument : member.arguments) {
                        static_cast<void>(analyzeExpression(argument));
                    }
                }
                model_.channelSenderClones[id] = true;
                return sourceType;
            }
            if (!program_.functions[currentFunction_].task || taskWaitRoot_ != id) {
                diagnostics_.error(
                    "FDN2168",
                    "suspending channel operation must be a standalone task binding or discard",
                    span);
            }
            const auto *endpointName = std::get_if<NameExpression>(&baseExpression.value);
            const auto endpoint = model_.expressionLocals[*member.base];
            if (endpointName == nullptr || !endpoint.has_value()) {
                diagnostics_.error("FDN2173", "channel operation requires a local endpoint",
                                   span);
                for (const auto argument : member.arguments) {
                    static_cast<void>(analyzeExpression(argument));
                }
                return invalidType;
            }

            const auto payload = sourceType.arguments.front();
            const auto error = Type{TypeKind::Enum, enums_.at("ChannelError"), {}};
            const auto resultPayload = send ? voidType : payload;
            const auto result =
                Type{TypeKind::Enum, enums_.at("Result"), {resultPayload, error}};
            std::optional<AstExpressionId> value;
            std::optional<FirLocalId> valueStorage;
            if (send) {
                const auto expectedArguments = payload == voidType ? 0U : 1U;
                if (member.arguments.size() != expectedArguments) {
                    diagnostics_.error("FDN2010", "wrong argument count for Sender.send", span);
                    for (const auto argument : member.arguments) {
                        static_cast<void>(analyzeExpression(argument));
                    }
                } else if (payload != voidType) {
                    value = member.arguments.front();
                    const auto actual = analyzeExpression(*value, payload, ExpressionUse::Consume);
                    requireSame(payload, actual, span, "channel payload");
                    if (model_.expressionBorrowedClosures[*value]) {
                        diagnostics_.error("FDN2127", "borrowed closure cannot enter a channel",
                                           span);
                    }
                    valueStorage = addLocal("$channelValue" +
                                                std::to_string(channelStorage_++),
                                            payload, false, span);
                }
            } else {
                if (!member.arguments.empty()) {
                    diagnostics_.error("FDN2010", "Receiver.receive does not accept arguments",
                                       span);
                    for (const auto argument : member.arguments) {
                        static_cast<void>(analyzeExpression(argument));
                    }
                }
            }
            const auto resultStorage = addLocal(
                "$channelResult" + std::to_string(channelStorage_++), result, false, span);
            resultOutstanding_[resultStorage] = false;
            model_.channelOperationTargets[id] = ChannelOperationTarget{
                send ? ChannelOperationKind::Send : ChannelOperationKind::Receive,
                *endpoint, value, valueStorage, resultStorage};
            return result;
        }
        auto base = sourceType;
        if (base.kind == TypeKind::Task) {
            rejectNamedArguments(member.argumentNames, "Task.wait", span);
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            if (member.invoked && member.member == "wait") {
                diagnostics_.error("FDN2166", "Task.wait consumes its handle; use $handle.wait()",
                                   span);
            } else {
                diagnostics_.error("FDN2100", "unknown Task member " + member.member, span);
            }
            return invalidType;
        }
        if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
             base.kind == TypeKind::Edit) &&
            base.arguments.size() == 1) {
            const auto value = base.arguments.front();
            base = value;
        }
        if (base.kind == TypeKind::Channel && base.arguments.size() == 1) {
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            if (member.invoked) {
                diagnostics_.error("FDN2100", "unknown Channel member " + member.member, span);
                return invalidType;
            }
            std::optional<FirFieldId> field;
            if (member.member == "sender") {
                field = 0;
            } else if (member.member == "receiver") {
                field = 1;
            }
            if (!field.has_value()) {
                diagnostics_.error("FDN2025", "unknown Channel field " + member.member, span);
                return invalidType;
            }
            model_.expressionFields[id] = *field;
            const auto result = Type{*field == 0 ? TypeKind::Sender : TypeKind::Receiver,
                                     0, {base.arguments.front()}};
            if (use == ExpressionUse::Consume) {
                diagnostics_.error("FDN2078", "Channel endpoint cannot move independently; "
                                              "destructure the Channel",
                                   span);
            }
            return result;
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

    struct DefaultMethodCandidate {
        Type contract{invalidType};
        std::size_t method{};
    };

    std::optional<DefaultMethodCandidate> findDefaultMethod(const Type &base,
                                                            std::string_view name,
                                                            SourceSpan span) {
        std::optional<DefaultMethodCandidate> result;
        if (base.kind != TypeKind::Struct || base.declaration >= model_.structs.size()) {
            return result;
        }
        for (const auto &implementation : model_.structs[base.declaration].implementations) {
            const auto contract = substitute(implementation, base.arguments);
            if (contract.kind != TypeKind::Contract ||
                contract.declaration >= model_.contracts.size()) {
                continue;
            }
            const auto &methods = model_.contracts[contract.declaration].methods;
            for (std::size_t method = 0; method < methods.size(); ++method) {
                const auto &candidate = methods[method];
                if (candidate.name != name || !candidate.defaultFunction.has_value()) {
                    continue;
                }
                if (!result.has_value()) {
                    result = DefaultMethodCandidate{contract, method};
                    continue;
                }
                const auto &current = model_.contracts[result->contract.declaration]
                                          .methods[result->method];
                auto same = current.defaultFunction == candidate.defaultFunction &&
                            current.receiver == candidate.receiver &&
                            substitute(current.returnType, result->contract.arguments) ==
                                substitute(candidate.returnType, contract.arguments) &&
                            current.parameterTypes.size() == candidate.parameterTypes.size();
                for (std::size_t parameter = 0;
                     same && parameter < current.parameterTypes.size(); ++parameter) {
                    same = substitute(current.parameterTypes[parameter],
                                      result->contract.arguments) ==
                           substitute(candidate.parameterTypes[parameter], contract.arguments);
                }
                if (!same) {
                    diagnostics_.error("FDN2145", "ambiguous default method " +
                                                       std::string(name),
                                       span);
                    return std::nullopt;
                }
            }
        }
        return result;
    }

    Type analyzeDefaultMethod(AstExpressionId id, const MemberExpression &member,
                              const Type &sourceType, const Type &base,
                              const DefaultMethodCandidate &candidate, SourceSpan span) {
        const auto &method =
            model_.contracts[candidate.contract.declaration].methods[candidate.method];
        const auto &origin = program_.contracts[method.originContract];
        if (origin.packageName != currentPackage() && !method.exported) {
            diagnostics_.error("FDN3008", "method " + member.member + " is not exported", span);
        }
        if (method.receiver == ReceiverKind::Edit &&
            (sourceType.kind == TypeKind::View || !editablePlace(*member.base))) {
            diagnostics_.error("FDN2101", "edit method requires an editable receiver", span);
        }
        if (method.receiver == ReceiverKind::Own) {
            diagnostics_.error("FDN2103", "own default method requires an owned contract value",
                               span);
        }

        const auto loansBefore = loanStates_;
        const auto borrowsAllowedBefore = transientBorrowsAllowed_;
        transientBorrowsAllowed_ = true;
        const auto argumentParameters = mapArgumentParameters(
            member.argumentNames, member.arguments.size(), method.parameterNames, span);
        std::vector<Type> arguments;
        std::vector<std::optional<Type>> implicitBorrows(member.arguments.size());
        arguments.reserve(member.arguments.size());
        for (std::size_t source = 0; source < member.arguments.size(); ++source) {
            const auto parameter = argumentParameters[source];
            const auto mode = parameter < method.parameterModes.size()
                                  ? method.parameterModes[parameter]
                                  : ParameterMode::Bootstrap;
            const auto expected = parameter < method.parameterTypes.size()
                                      ? std::optional<Type>{specializeReadParameter(
                                            substitute(method.parameterTypes[parameter],
                                                       candidate.contract.arguments),
                                            mode)}
                                      : std::nullopt;
            arguments.push_back(analyzeCallArgument(
                member.arguments[source], expected, mode, span, implicitBorrows[source]));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;
        if (arguments.size() != method.parameterTypes.size()) {
            diagnostics_.error("FDN2010", "wrong argument count for method " + member.member,
                               span);
        }
        const auto count = std::min(arguments.size(), method.parameterTypes.size());
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t source = 0; source < count; ++source) {
            const auto parameter = argumentParameters[source];
            if (parameter >= method.parameterTypes.size()) {
                continue;
            }
            const auto mode = parameter < method.parameterModes.size()
                                  ? method.parameterModes[parameter]
                                  : ParameterMode::Bootstrap;
            const auto expected = specializeReadParameter(
                substitute(method.parameterTypes[parameter], candidate.contract.arguments),
                mode);
            if (const auto conversion = contractConversion(expected, arguments[source]);
                conversion.has_value()) {
                conversions[source] = *conversion;
            } else {
                requireSame(expected, arguments[source], span, "method argument");
            }
        }

        const auto borrowKind = method.receiver == ReceiverKind::Edit ? TypeKind::Edit
                                                                      : TypeKind::View;
        const Type concreteBorrow{borrowKind, 0, {base}};
        const Type contractBorrow{borrowKind, 0, {candidate.contract}};
        auto receiverConversion = contractConversion(contractBorrow, concreteBorrow);
        if (!receiverConversion.has_value()) {
            diagnostics_.error("FDN2096", "default method receiver does not implement contract",
                               span);
        }

        CallTarget target;
        target.kind = CallTargetKind::ContractMethod;
        target.receiver = *member.base;
        target.receiverType = concreteBorrow;
        target.contract = candidate.contract.declaration;
        target.method = candidate.method;
        target.typeArguments = candidate.contract.arguments;
        target.receiverConversion = std::move(receiverConversion);
        target.argumentBorrows = std::move(implicitBorrows);
        target.argumentConversions = std::move(conversions);
        target.argumentParameters = argumentParameters;
        model_.callTargets[id] = std::move(target);
        return substitute(method.returnType, candidate.contract.arguments);
    }

    Type analyzeMethod(AstExpressionId id, const MemberExpression &member,
                       const Type &sourceType, const Type &base, SourceSpan span) {
        if (!member.typeArguments.empty()) {
            diagnostics_.error("FDN2043", "method does not accept type arguments", span);
        }
        if (base.kind == TypeKind::Contract) {
            return analyzeContractMethod(id, member, sourceType, base, span);
        }
        const auto methodOwner = base.kind == TypeKind::Struct
                                     ? base.declaration
                                 : base.kind == TypeKind::Enum
                                     ? program_.structs.size() + base.declaration
                                     : methods_.size();
        if ((base.kind != TypeKind::Struct && base.kind != TypeKind::Enum) ||
            methodOwner >= methods_.size()) {
            for (const auto argument : member.arguments) {
                static_cast<void>(analyzeExpression(argument));
            }
            diagnostics_.error("FDN2050", "method call requires a struct, enum, or contract",
                               span);
            return invalidType;
        }
        const auto found = methods_[methodOwner].find(member.member);
        if (found == methods_[methodOwner].end()) {
            if (base.kind == TypeKind::Struct) {
                if (const auto method = findDefaultMethod(base, member.member, span);
                    method.has_value()) {
                    return analyzeDefaultMethod(id, member, sourceType, base, *method, span);
                }
            }
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
        const auto &signature = signatures_[function];
        std::vector<std::string> parameterNames;
        if (declaration.parameters.size() > 1) {
            parameterNames.reserve(declaration.parameters.size() - 1);
            for (auto parameter = declaration.parameters.begin() + 1;
                 parameter != declaration.parameters.end(); ++parameter) {
                parameterNames.push_back(parameter->name);
            }
        }
        const auto argumentParameters = mapArgumentParameters(
            member.argumentNames, member.arguments.size(), parameterNames, span);
        std::vector<Type> arguments;
        std::vector<std::optional<Type>> implicitBorrows(member.arguments.size());
        arguments.reserve(member.arguments.size());
        for (std::size_t source = 0; source < member.arguments.size(); ++source) {
            const auto parameter = argumentParameters[source] + 1;
            const auto mode = parameter < declaration.parameters.size()
                                  ? declaration.parameters[parameter].mode
                                  : ParameterMode::Bootstrap;
            const auto expected = parameter < signature.parameters.size()
                                      ? std::optional<Type>{specializeReadParameter(
                                            substitute(signature.parameters[parameter],
                                                       base.arguments),
                                            mode)}
                                      : std::nullopt;
            arguments.push_back(analyzeCallArgument(
                member.arguments[source], expected, mode, span, implicitBorrows[source]));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;

        const auto parameterCount = signature.parameters.empty()
                                        ? std::size_t{}
                                        : signature.parameters.size() - 1;
        if (arguments.size() != parameterCount) {
            diagnostics_.error("FDN2010", "wrong argument count for method " + member.member,
                               span);
        }
        const auto count = std::min(arguments.size(), parameterCount);
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t source = 0; source < count; ++source) {
            const auto parameter = argumentParameters[source] + 1;
            if (parameter >= signature.parameters.size()) {
                continue;
            }
            const auto mode = parameter < declaration.parameters.size()
                                  ? declaration.parameters[parameter].mode
                                  : ParameterMode::Bootstrap;
            const auto expected = specializeReadParameter(
                substitute(signature.parameters[parameter], base.arguments), mode);
            if (const auto conversion = contractConversion(expected, arguments[source]);
                conversion.has_value()) {
                conversions[source] = *conversion;
            } else {
                requireSame(expected, arguments[source], span, "method argument");
            }
        }
        if (isParallelPoolStart(base, declaration, member.member)) {
            verifyParallelPoolTask(member, span);
        }

        CallTarget target;
        target.kind = CallTargetKind::Method;
        target.function = function;
        target.typeArguments = base.arguments;
        target.receiver = *member.base;
        target.receiverType = substitute(signature.parameters.front(), base.arguments);
        target.argumentBorrows = std::move(implicitBorrows);
        target.argumentConversions = std::move(conversions);
        target.argumentParameters = argumentParameters;
        model_.callTargets[id] = std::move(target);
        return substitute(signature.returnType, base.arguments);
    }

    Type analyzeContractMethod(AstExpressionId id, const MemberExpression &member,
                               const Type &sourceType, const Type &base, SourceSpan span) {
        if (base.declaration >= program_.contracts.size()) {
            return invalidType;
        }
        const auto &contract = model_.contracts[base.declaration];
        std::optional<std::size_t> methodIndex;
        for (std::size_t index = 0; index < contract.methods.size(); ++index) {
            if (contract.methods[index].name == member.member) {
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
        const auto &method = contract.methods[*methodIndex];
        const auto &semantic = method;
        const auto &origin = program_.contracts[method.originContract];
        if (origin.packageName != currentPackage() && !method.exported) {
            diagnostics_.error("FDN3008", "method " + member.member + " is not exported", span);
        }
        if (method.receiver == ReceiverKind::Edit &&
            (sourceType.kind == TypeKind::View ||
             (sourceType.kind != TypeKind::Edit && sourceType.kind != TypeKind::Own) ||
             (sourceType.kind == TypeKind::Own && !editablePlace(*member.base)))) {
            diagnostics_.error("FDN2101", "edit method requires an editable receiver", span);
        }
        if (method.receiver == ReceiverKind::Own) {
            if (sourceType.kind != TypeKind::Own ||
                !std::holds_alternative<NameExpression>(
                    program_.expressions[*member.base].value)) {
                diagnostics_.error("FDN2103", "own method requires an owned contract binding",
                                   span);
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
        const auto argumentParameters = mapArgumentParameters(
            member.argumentNames, member.arguments.size(), semantic.parameterNames, span);
        std::vector<Type> arguments;
        std::vector<std::optional<Type>> implicitBorrows(member.arguments.size());
        arguments.reserve(member.arguments.size());
        for (std::size_t source = 0; source < member.arguments.size(); ++source) {
            const auto parameter = argumentParameters[source];
            const auto mode = parameter < semantic.parameterModes.size()
                                  ? semantic.parameterModes[parameter]
                                  : ParameterMode::Bootstrap;
            const auto expected = parameter < semantic.parameterTypes.size()
                                      ? std::optional<Type>{specializeReadParameter(
                                            substitute(semantic.parameterTypes[parameter],
                                                       base.arguments),
                                            mode)}
                                      : std::nullopt;
            arguments.push_back(analyzeCallArgument(
                member.arguments[source], expected, mode, span, implicitBorrows[source]));
        }
        restoreLoans(loansBefore);
        transientBorrowsAllowed_ = borrowsAllowedBefore;
        if (arguments.size() != semantic.parameterTypes.size()) {
            diagnostics_.error("FDN2010", "wrong argument count for method " + member.member,
                               span);
        }
        const auto count = std::min(arguments.size(), semantic.parameterTypes.size());
        std::vector<std::optional<CallTarget::ContractConversion>> conversions(arguments.size());
        for (std::size_t source = 0; source < count; ++source) {
            const auto parameter = argumentParameters[source];
            if (parameter >= semantic.parameterTypes.size()) {
                continue;
            }
            const auto mode = parameter < semantic.parameterModes.size()
                                  ? semantic.parameterModes[parameter]
                                  : ParameterMode::Bootstrap;
            const auto expected = specializeReadParameter(
                substitute(semantic.parameterTypes[parameter], base.arguments), mode);
            if (const auto conversion = contractConversion(expected, arguments[source]);
                conversion.has_value()) {
                conversions[source] = *conversion;
            } else {
                requireSame(expected, arguments[source], span, "method argument");
            }
        }

        CallTarget target;
        target.kind = CallTargetKind::ContractMethod;
        target.receiver = *member.base;
        target.receiverType = sourceType;
        target.contract = base.declaration;
        target.method = *methodIndex;
        target.typeArguments = base.arguments;
        target.argumentBorrows = std::move(implicitBorrows);
        target.argumentConversions = std::move(conversions);
        target.argumentParameters = argumentParameters;
        model_.callTargets[id] = std::move(target);
        return substitute(semantic.returnType, base.arguments);
    }

    bool editablePlace(AstExpressionId id) const {
        const auto &expression = program_.expressions[id];
        if (const auto *unary = std::get_if<UnaryExpression>(&expression.value);
            unary != nullptr && unary->operation == UnaryOperator::Dereference) {
            return unary->operand < model_.expressionTypes.size() &&
                   model_.expressionTypes[unary->operand].kind == TypeKind::Raw;
        }
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
        if (const auto *unary = std::get_if<UnaryExpression>(&expression.value);
            unary != nullptr && unary->operation == UnaryOperator::Dereference) {
            auto pointer = unary->operand < model_.expressionTypes.size()
                               ? model_.expressionTypes[unary->operand]
                               : invalidType;
            if (const auto *name = std::get_if<NameExpression>(
                    &program_.expressions[unary->operand].value)) {
                if (const auto local = lookupLocal(name->name); local.has_value()) {
                    pointer = model_.functions[currentFunction_].locals[*local].type;
                }
            }
            if ((pointer.kind == TypeKind::Raw || pointer.kind == TypeKind::RawConst) &&
                pointer.arguments.size() == 1) {
                return pointer.arguments.front();
            }
            return invalidType;
        }
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
        if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            return unary->operation == UnaryOperator::Dereference;
        }
        if (std::holds_alternative<NameExpression>(expression.value)) {
            return true;
        }
        if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            return !member->invoked && member->base.has_value() &&
                   isPlaceExpression(*member->base);
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
        if (hasNamedArguments(constructor.argumentNames)) {
            const auto &payloadName = declaration.variants[*variant].payloadName;
            if (!payloadName.has_value()) {
                rejectNamedArguments(constructor.argumentNames, "enum variant", span);
            } else {
                static_cast<void>(mapArgumentParameters(
                    constructor.argumentNames, constructor.arguments.size(), {*payloadName},
                    span));
            }
        }
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
        auto payloadPattern = model_.enums[*enumType].payloadTypes[*variant];
        if (payloadPattern.has_value()) {
            std::vector<Type> knownArguments;
            knownArguments.reserve(inferred.size());
            auto complete = true;
            for (const auto &argument : inferred) {
                complete = complete && argument.has_value();
                knownArguments.push_back(argument.value_or(invalidType));
            }
            if (complete && substitute(*payloadPattern, knownArguments) == voidType &&
                declaration.builtin == BuiltinEnumKind::Result) {
                payloadPattern.reset();
            }
        }
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
        auto matchedType = value;
        const auto borrowedMatch =
            (value.kind == TypeKind::View || value.kind == TypeKind::Edit) &&
            value.arguments.size() == 1;
        if (borrowedMatch) {
            matchedType = value.arguments.front();
        }
        if (matchedType.kind != TypeKind::Enum ||
            matchedType.declaration >= program_.enums.size()) {
            for (const auto &arm : match.arms) {
                scopes_.emplace_back();
                if (arm.guard.has_value()) {
                    static_cast<void>(analyzeExpression(*arm.guard));
                }
                for (const auto statement : program_.blocks[arm.block].statements) {
                    static_cast<void>(analyzeStatement(statement));
                }
                if (arm.expression.has_value()) {
                    static_cast<void>(analyzeExpression(*arm.expression));
                }
                scopes_.pop_back();
            }
            diagnostics_.error("FDN2037", "match requires an enum value", span);
            return invalidType;
        }

        const auto enumType = matchedType.declaration;
        const auto &declaration = program_.enums[enumType];
        const auto beforeArms = resultOutstanding_;
        const auto movesBeforeArms = moveStates_;
        const auto loansBeforeArms = loanStates_;
        std::vector<bool> covered(declaration.variants.size());
        std::vector<std::unordered_set<std::string>> literalPatterns(
            declaration.variants.size());
        std::vector<FirVariantId> variants;
        std::vector<std::optional<FirLocalId>> bindings;
        std::vector<std::optional<FirLocalId>> guardBindings;
        std::vector<std::vector<FirLocalId>> drops;
        std::vector<std::vector<bool>> armStates;
        std::vector<std::vector<MoveState>> armMoveStates;
        std::vector<std::vector<LoanState>> armLoanStates;
        auto borrowedClosure = false;
        auto wildcardCovered = false;
        auto result = invalidType;
        for (const auto &arm : match.arms) {
            restoreOutstanding(beforeArms);
            restoreMoves(movesBeforeArms);
            restoreLoans(loansBeforeArms);
            if (wildcardCovered) {
                diagnostics_.error("FDN2039", "match arm is unreachable after wildcard",
                                   arm.span);
            }

            std::optional<FirVariantId> variant;
            if (!arm.wildcard) {
                variant = findVariant(enumType, arm.variant);
            }
            if (!arm.wildcard && !variant.has_value()) {
                diagnostics_.error("FDN2035", "unknown variant " + arm.variant, arm.span);
            } else if (variant.has_value()) {
                if (declaration.packageName != currentPackage() &&
                    !declaration.variants[*variant].exported) {
                    diagnostics_.error("FDN3008", "variant " + arm.variant + " is not exported",
                                       arm.span);
                }
            }

            scopes_.emplace_back();
            std::optional<FirLocalId> binding;
            std::optional<FirLocalId> guardBinding;
            const auto unconditional = !arm.guard.has_value();
            const auto analyzeGuard = [&](std::optional<Type> payload) {
                if (!arm.guard.has_value()) {
                    return;
                }
                if (arm.binding.has_value() && payload.has_value()) {
                    scopes_.emplace_back();
                    auto guardType = *payload;
                    auto readBinding = false;
                    if (!isCopyParameterType(guardType)) {
                        guardType = guardType.kind == TypeKind::Own &&
                                            guardType.arguments.size() == 1
                                        ? Type{TypeKind::View, 0,
                                               {guardType.arguments.front()}}
                                        : Type{TypeKind::View, 0, {guardType}};
                        readBinding = true;
                    }
                    guardBinding = addLocal(*arm.binding, guardType, false, arm.span);
                    model_.functions[currentFunction_].locals[*guardBinding].readBinding =
                        readBinding;
                }
                const auto movesBeforeGuard = movePrefix(movesBeforeArms.size());
                requireSame(boolType,
                            analyzeExpression(*arm.guard, boolType,
                                              ExpressionUse::Inspect),
                            program_.expressions[*arm.guard].span, "match guard");
                const auto movesAfterGuard = movePrefix(movesBeforeArms.size());
                for (std::size_t local = 0; local < movesBeforeGuard.size(); ++local) {
                    if (movesBeforeGuard[local] != movesAfterGuard[local]) {
                        diagnostics_.error(
                            "FDN2228", "match guard cannot move binding " +
                                           model_.functions[currentFunction_].locals[local].name,
                            program_.expressions[*arm.guard].span);
                    }
                }
                restoreMoves(movesBeforeGuard);
                restoreLoans(loansBeforeArms);
                if (guardBinding.has_value()) {
                    reportScope(scopes_.back());
                    scopes_.pop_back();
                }
            };

            if (arm.wildcard) {
                if (arm.binding.has_value() || arm.pattern.has_value()) {
                    diagnostics_.error("FDN2041", "wildcard does not accept a payload",
                                       arm.span);
                }
                analyzeGuard(std::nullopt);
                if (unconditional) {
                    wildcardCovered = true;
                    std::fill(covered.begin(), covered.end(), true);
                }
            } else if (variant.has_value()) {
                auto payload = model_.enums[enumType].payloadTypes[*variant];
                if (payload.has_value()) {
                    payload = substitute(*payload, matchedType.arguments);
                    if (*payload == voidType &&
                        declaration.builtin == BuiltinEnumKind::Result) {
                        payload.reset();
                    }
                }
                if (!payload.has_value()) {
                    if (arm.binding.has_value() || arm.pattern.has_value()) {
                        diagnostics_.error("FDN2041", "unit pattern does not accept a payload",
                                           arm.span);
                    }
                    if (covered[*variant]) {
                        diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                           arm.span);
                    }
                    analyzeGuard(std::nullopt);
                    if (unconditional) {
                        covered[*variant] = true;
                    }
                } else if (arm.binding.has_value()) {
                    if (covered[*variant]) {
                        diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                           arm.span);
                    }
                    analyzeGuard(payload);
                    if (unconditional) {
                        covered[*variant] = true;
                    }
                    auto bindingType = *payload;
                    auto readBinding = false;
                    if (borrowedMatch && !isCopyParameterType(bindingType)) {
                        bindingType = bindingType.kind == TypeKind::Own &&
                                              bindingType.arguments.size() == 1
                                          ? Type{TypeKind::View, 0,
                                                 {bindingType.arguments.front()}}
                                          : Type{TypeKind::View, 0, {bindingType}};
                        readBinding = true;
                    }
                    binding = addLocal(*arm.binding, bindingType, false, arm.span);
                    model_.functions[currentFunction_].locals[*binding].readBinding =
                        readBinding;
                } else if (arm.pattern.has_value()) {
                    const auto &patternExpression = program_.expressions[*arm.pattern];
                    std::optional<std::string> key;
                    if (const auto *integer =
                            std::get_if<IntegerExpression>(&patternExpression.value)) {
                        key = "integer:" + std::string(integer->negative ? "-" : "+") +
                              std::to_string(integer->magnitude);
                    } else if (const auto *boolean =
                                   std::get_if<BooleanExpression>(&patternExpression.value)) {
                        key = boolean->value ? "bool:true" : "bool:false";
                    } else if (const auto *string =
                                   std::get_if<StringExpression>(&patternExpression.value)) {
                        key = "string:" + string->value;
                    } else {
                        diagnostics_.error(
                            "FDN2210",
                            "match payload pattern must be an integer, boolean, or string literal",
                            patternExpression.span);
                    }
                    const auto patternType = analyzeExpression(*arm.pattern, *payload);
                    requireSame(*payload, patternType, patternExpression.span,
                                "match payload pattern");
                    if (covered[*variant] ||
                        (unconditional && key.has_value() &&
                         !literalPatterns[*variant].insert(*key).second)) {
                        diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                           arm.span);
                    }
                    analyzeGuard(std::nullopt);
                    if (unconditional && *payload == boolType &&
                        literalPatterns[*variant].contains("bool:true") &&
                        literalPatterns[*variant].contains("bool:false")) {
                        covered[*variant] = true;
                    }
                } else {
                    diagnostics_.error("FDN2041", "payload pattern requires a binding or literal",
                                       arm.span);
                    covered[*variant] = true;
                    analyzeGuard(std::nullopt);
                }
            } else {
                analyzeGuard(std::nullopt);
            }
            const auto armExpected = expected.has_value()
                                         ? expected
                                         : (result.kind == TypeKind::Invalid
                                                ? std::nullopt
                                                : std::optional<Type>{result});
            auto armExits = false;
            for (const auto statement : program_.blocks[arm.block].statements) {
                armExits = analyzeStatement(statement) || armExits;
            }
            const auto tailType = arm.expression.has_value()
                                      ? analyzeExpression(*arm.expression, armExpected)
                                      : voidType;
            const auto armType = armExits ? neverType : tailType;
            if (arm.expression.has_value()) {
                borrowedClosure = borrowedClosure ||
                                  model_.expressionBorrowedClosures[*arm.expression];
            }
            drops.push_back(scopeDrops(scopes_.back()));
            reportScope(scopes_.back());
            scopes_.pop_back();
            armStates.push_back(outstandingPrefix(beforeArms.size()));
            armMoveStates.push_back(movePrefix(movesBeforeArms.size()));
            armLoanStates.push_back(loanPrefix(loansBeforeArms.size()));
            if (result.kind == TypeKind::Invalid || result == neverType) {
                result = armType;
            } else if (armType != neverType) {
                requireSame(result, armType, arm.span, "match arm");
            }
            variants.push_back(variant.value_or(0));
            bindings.push_back(binding);
            guardBindings.push_back(guardBinding);
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
        model_.matchTargets[id] =
            MatchTarget{matchedType, std::move(variants), std::move(bindings),
                        std::move(guardBindings), std::move(drops)};
        model_.expressionBorrowedClosures[id] = borrowedClosure;
        return result;
    }

    Type analyzeConditional(AstExpressionId id, const ConditionalExpression &conditional,
                            std::optional<Type> expected, SourceSpan span) {
        requireSame(boolType, analyzeExpression(conditional.condition), span,
                    "conditional expression condition");

        const auto before = resultOutstanding_;
        const auto movesBefore = moveStates_;
        const auto loansBefore = loanStates_;
        struct BranchState {
            Type type{invalidType};
            std::vector<bool> outstanding;
            std::vector<MoveState> moves;
            std::vector<LoanState> loans;
            bool returns{};
            bool borrowedClosure{};
        };
        const auto analyzeBranch = [&](AstBlockId block, AstExpressionId value,
                                       std::optional<Type> branchExpected) {
            restoreOutstanding(before);
            restoreMoves(movesBefore);
            restoreLoans(loansBefore);
            scopes_.emplace_back();
            auto returns = false;
            for (const auto statement : program_.blocks[block].statements) {
                if (analyzeStatement(statement)) {
                    returns = true;
                }
            }
            const auto type = analyzeExpression(value, branchExpected);
            model_.blockDrops[block] = scopeDrops(scopes_.back());
            reportScope(scopes_.back());
            scopes_.pop_back();
            return BranchState{type,
                               outstandingPrefix(before.size()),
                               movePrefix(movesBefore.size()),
                               loanPrefix(loansBefore.size()),
                               returns,
                               model_.expressionBorrowedClosures[value]};
        };

        const auto thenState =
            analyzeBranch(conditional.thenBlock, conditional.thenValue, expected);
        const auto elseExpected = expected.has_value()
                                      ? expected
                                      : std::optional<Type>{thenState.type};
        const auto elseState =
            analyzeBranch(conditional.elseBlock, conditional.elseValue, elseExpected);
        if (thenState.type != neverType && elseState.type != neverType) {
            requireSame(thenState.type, elseState.type, span, "conditional expression branch");
        }

        std::vector<bool> merged(before.size());
        for (std::size_t local = 0; local < before.size(); ++local) {
            if (thenState.returns && elseState.returns) {
                merged[local] = false;
            } else if (thenState.returns) {
                merged[local] = elseState.outstanding[local];
            } else if (elseState.returns) {
                merged[local] = thenState.outstanding[local];
            } else {
                merged[local] = thenState.outstanding[local] ||
                                elseState.outstanding[local];
            }
        }
        restoreOutstanding(merged);
        restoreMoves(mergeMoves(movesBefore, thenState.moves, elseState.moves,
                                thenState.returns, elseState.returns));
        restoreLoans(mergeLoans(loansBefore, thenState.loans, elseState.loans,
                                thenState.returns, elseState.returns));
        model_.expressionBorrowedClosures[id] =
            thenState.borrowedClosure || elseState.borrowedClosure;
        return thenState.type == neverType ? elseState.type : thenState.type;
    }

    void setTypeParameters(const std::vector<std::string> &parameters, SourceSpan span) {
        typeParameters_.clear();
        currentTypeParameterNames_ = parameters;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto &name = parameters[index];
            if (name == "transferable" || isBuiltinType(name) || structs_.contains(name) ||
                enums_.contains(name) ||
                contracts_.contains(name) ||
                !typeParameters_.emplace(name, index).second) {
                diagnostics_.error("FDN2042", "duplicate or shadowing type parameter " + name,
                                   span);
            }
        }
    }

    Type resolveType(const TypeSyntax &syntax) {
        if (syntax.name == "[function-read]" || syntax.name == "[function-edit]" ||
            syntax.name == "[function-transfer]") {
            if (syntax.arguments.size() != 1) {
                diagnostics_.error("FDN2043", "invalid function parameter mode", syntax.span);
                return invalidType;
            }
            auto target = resolveType(syntax.arguments.front());
            if (target == voidType || target == neverType) {
                diagnostics_.error("FDN2016", "function parameter cannot have type void or never",
                                   syntax.span);
                return invalidType;
            }
            if (syntax.name == "[function-read]") {
                return isCopyParameterType(target)
                           ? target
                           : Type{TypeKind::View, 1, {std::move(target)}};
            }
            if (syntax.name == "[function-edit]") {
                return Type{TypeKind::Edit, 0, {std::move(target)}};
            }
            return target;
        }
        std::optional<Type> base;
        if (syntax.name == "void") {
            base = voidType;
        } else if (syntax.name == "never") {
            base = neverType;
        } else if (syntax.name == "i8") {
            base = i8Type;
        } else if (syntax.name == "i16") {
            base = i16Type;
        } else if (syntax.name == "i32") {
            base = i32Type;
        } else if (syntax.name == "i64") {
            base = i64Type;
        } else if (syntax.name == "u8") {
            base = u8Type;
        } else if (syntax.name == "u16") {
            base = u16Type;
        } else if (syntax.name == "u32") {
            base = u32Type;
        } else if (syntax.name == "u64") {
            base = u64Type;
        } else if (syntax.name == "isize") {
            base = isizeType;
        } else if (syntax.name == "usize") {
            base = usizeType;
        } else if (syntax.name == "f32") {
            base = f32Type;
        } else if (syntax.name == "f64") {
            base = f64Type;
        } else if (syntax.name == "bool") {
            base = boolType;
        } else if (syntax.name == "String") {
            base = stringType;
        } else if (syntax.name == "[raw]") {
            base = Type{TypeKind::Raw};
        } else if (syntax.name == "[raw-const]") {
            base = Type{TypeKind::RawConst};
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
        } else if (syntax.name == "[transferable-function]") {
            base = Type{TypeKind::Function, transferableFunctionQualifier};
        } else if (syntax.name == "Task") {
            base = Type{TypeKind::Task};
        } else if (syntax.name == "Channel") {
            base = Type{TypeKind::Channel};
        } else if (syntax.name == "Sender") {
            base = Type{TypeKind::Sender};
        } else if (syntax.name == "Receiver") {
            base = Type{TypeKind::Receiver};
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
                if (arguments[index] == voidType || arguments[index] == neverType) {
                    diagnostics_.error("FDN2016", "function parameter cannot have type void or never",
                                       syntax.span);
                }
            }
        } else if (base->kind == TypeKind::Own || base->kind == TypeKind::View ||
                   base->kind == TypeKind::Edit || base->kind == TypeKind::Array ||
                   base->kind == TypeKind::Slice || base->kind == TypeKind::Raw ||
                   base->kind == TypeKind::RawConst || base->kind == TypeKind::Task ||
                   base->kind == TypeKind::Channel || base->kind == TypeKind::Sender ||
                   base->kind == TypeKind::Receiver) {
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
            if (target == voidType || target == neverType || target.kind == TypeKind::View ||
                target.kind == TypeKind::Edit || target.kind == TypeKind::Own) {
                diagnostics_.error("FDN2064",
                                   syntax.name + " requires a direct non-void value type",
                                   syntax.span);
            }
            if (base->kind == TypeKind::Own && target.kind == TypeKind::Slice) {
                diagnostics_.error("FDN2080", "slice cannot be owned directly", syntax.span);
            }
        } else if ((base->kind == TypeKind::Array || base->kind == TypeKind::Slice) &&
                   !base->arguments.empty() &&
                   (base->arguments.front() == voidType ||
                    base->arguments.front() == neverType)) {
            diagnostics_.error("FDN2047", "array or slice element cannot be void or never",
                               syntax.span);
        } else if ((base->kind == TypeKind::Raw || base->kind == TypeKind::RawConst) &&
                   !base->arguments.empty()) {
            const auto &pointee = base->arguments.front();
            const auto supported = pointee == voidType ||
                                   (isMachineScalar(pointee) && pointee != neverType) ||
                                   ((pointee.kind == TypeKind::Raw ||
                                     pointee.kind == TypeKind::RawConst) &&
                                    pointee.arguments.size() == 1);
            if (!supported) {
                diagnostics_.error("FDN2214",
                                   "raw pointer pointee must be a C ABI scalar or pointer",
                                   syntax.span);
            }
        } else if (base->kind == TypeKind::Task && !base->arguments.empty() &&
                   containsBorrow(base->arguments.front())) {
            diagnostics_.error("FDN2165", "Task result cannot contain a borrow", syntax.span);
        } else if ((base->kind == TypeKind::Channel || base->kind == TypeKind::Sender ||
                    base->kind == TypeKind::Receiver) &&
                   !base->arguments.empty() && containsBorrow(base->arguments.front())) {
            diagnostics_.error("FDN2165", "channel payload cannot contain a borrow",
                               syntax.span);
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
        if (result.kind == TypeKind::View && result.declaration == 1 &&
            result.arguments.size() == 1 && isCopyParameterType(result.arguments.front())) {
            return result.arguments.front();
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
        if (pattern.kind == TypeKind::View && pattern.declaration == 1 &&
            pattern.arguments.size() == 1) {
            if (actual.kind == TypeKind::View && actual.arguments.size() == 1) {
                inferType(pattern.arguments.front(), actual.arguments.front(), inferred, span,
                          context);
                return;
            }
            if (isCopyParameterType(actual)) {
                inferType(pattern.arguments.front(), actual, inferred, span, context);
                return;
            }
        }
        if ((pattern.kind == TypeKind::View || pattern.kind == TypeKind::Edit ||
             pattern.kind == TypeKind::Own) &&
            pattern.arguments.size() == 1 &&
            pattern.arguments.front().kind == TypeKind::Contract &&
            (actual.kind == TypeKind::View || actual.kind == TypeKind::Edit ||
             actual.kind == TypeKind::Own) &&
            actual.arguments.size() == 1 && actual.arguments.front().kind == TypeKind::Struct &&
            (pattern.kind == TypeKind::View || pattern.kind == actual.kind)) {
            const auto implemented = implementedContract(
                actual.arguments.front(), pattern.arguments.front().declaration);
            if (implemented.has_value() && !implemented->ambiguous) {
                inferType(pattern.arguments.front(), implemented->contract, inferred, span,
                          context);
                return;
            }
        }
        if ((pattern.kind == TypeKind::Struct || pattern.kind == TypeKind::Enum ||
             pattern.kind == TypeKind::Contract ||
             pattern.kind == TypeKind::Function ||
             pattern.kind == TypeKind::Task || pattern.kind == TypeKind::Channel ||
             pattern.kind == TypeKind::Sender || pattern.kind == TypeKind::Receiver ||
             pattern.kind == TypeKind::Array || pattern.kind == TypeKind::Slice ||
             pattern.kind == TypeKind::Own || pattern.kind == TypeKind::View ||
             pattern.kind == TypeKind::Edit) &&
            pattern.kind == actual.kind &&
            (pattern.kind == TypeKind::Function ||
             pattern.declaration == actual.declaration) &&
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
        return name == "void" || name == "never" || name == "i8" || name == "i16" ||
               name == "i32" || name == "i64" || name == "u8" || name == "u16" ||
               name == "u32" || name == "u64" || name == "isize" || name == "usize" ||
               name == "f32" || name == "f64" || name == "bool" ||
               name == "String" ||
               name == "Option" || name == "Result" || name == "ChannelError" ||
               name == "NumberError" ||
               name == "Task" ||
               name == "Channel" || name == "Sender" || name == "Receiver" ||
               name == "own" || name == "view" ||
               name == "edit";
    }

    std::string_view currentPackage() const {
        if (!currentPackageOverride_.empty()) {
            return currentPackageOverride_;
        }
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

    struct ImplementationMatch {
        Type contract{invalidType};
        std::optional<FirFieldId> delegate;
        bool ambiguous{};
    };

    std::optional<ImplementationMatch> implementedContract(const Type &concrete,
                                                           std::size_t contract) const {
        if (concrete.kind != TypeKind::Struct || concrete.declaration >= model_.structs.size()) {
            return std::nullopt;
        }
        struct PendingImplementation {
            Type contract{invalidType};
            std::optional<FirFieldId> delegate;
        };
        std::vector<PendingImplementation> pending;
        const auto &semantic = model_.structs[concrete.declaration];
        for (std::size_t index = 0; index < semantic.implementations.size(); ++index) {
            const auto &implementation = semantic.implementations[index];
            if (implementation.kind == TypeKind::Contract) {
                const auto delegate = index < semantic.implementationDelegates.size()
                                          ? semantic.implementationDelegates[index]
                                          : std::nullopt;
                pending.push_back(
                    {substitute(implementation, concrete.arguments), delegate});
            }
        }
        std::unordered_set<std::string> visited;
        std::optional<ImplementationMatch> result;
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            const auto visitKey = semanticTypeKey(current.contract) + ':' +
                                  (current.delegate.has_value()
                                       ? std::to_string(*current.delegate)
                                       : std::string("local"));
            if (!visited.emplace(visitKey).second) {
                continue;
            }
            if (current.contract.declaration == contract) {
                if (!result.has_value()) {
                    result = ImplementationMatch{current.contract, current.delegate, false};
                } else if (result->contract != current.contract ||
                           result->delegate != current.delegate) {
                    result->ambiguous = true;
                }
                continue;
            }
            if (current.contract.declaration >= model_.contracts.size()) {
                continue;
            }
            for (const auto &parent : model_.contracts[current.contract.declaration].parents) {
                pending.push_back(
                    {substitute(parent, current.contract.arguments), current.delegate});
            }
        }
        return result;
    }

    std::optional<CallTarget::ContractMethodTarget>
    contractMethodTarget(const Type &concrete, const Type &contract, std::size_t method,
                         std::unordered_set<std::string> &active) const {
        if (concrete.kind != TypeKind::Struct || concrete.declaration >= methods_.size() ||
            contract.kind != TypeKind::Contract ||
            contract.declaration >= model_.contracts.size() ||
            method >= model_.contracts[contract.declaration].methods.size()) {
            return std::nullopt;
        }
        const auto &required = model_.contracts[contract.declaration].methods[method];
        if (const auto found = methods_[concrete.declaration].find(required.name);
            found != methods_[concrete.declaration].end()) {
            return CallTarget::ContractMethodTarget{found->second, concrete.arguments, false,
                                                    invalidType, {}};
        }

        const auto key = semanticTypeKey(concrete) + ':' + semanticTypeKey(contract) + ':' +
                         std::to_string(method);
        if (!active.emplace(key).second) {
            return std::nullopt;
        }
        const auto implementation = implementedContract(concrete, contract.declaration);
        if (implementation.has_value() && !implementation->ambiguous &&
            implementation->contract == contract &&
            implementation->delegate.has_value()) {
            const auto field = *implementation->delegate;
            if (field < model_.structs[concrete.declaration].fieldTypes.size()) {
                const auto delegated = substitute(
                    model_.structs[concrete.declaration].fieldTypes[field], concrete.arguments);
                if (auto target = contractMethodTarget(delegated, contract, method, active);
                    target.has_value()) {
                    if (!target->contractDefault) {
                        target->delegatePath.insert(target->delegatePath.begin(), field);
                    }
                    active.erase(key);
                    return target;
                }
            }
        }
        active.erase(key);
        if (required.defaultFunction.has_value()) {
            std::vector<Type> typeArguments;
            typeArguments.reserve(required.originArguments.size());
            for (const auto &argument : required.originArguments) {
                typeArguments.push_back(substitute(argument, contract.arguments));
            }
            const auto defaultContractArguments = typeArguments;
            return CallTarget::ContractMethodTarget{*required.defaultFunction,
                                                    std::move(typeArguments), true,
                                                    Type{TypeKind::Contract,
                                                         required.originContract,
                                                         defaultContractArguments},
                                                    {}};
        }
        return std::nullopt;
    }

    std::optional<CallTarget::ContractConversion>
    contractConversion(const Type &expected, const Type &actual) const {
        if ((expected.kind != TypeKind::View && expected.kind != TypeKind::Edit &&
             expected.kind != TypeKind::Own) ||
            expected.arguments.size() != 1 ||
            expected.arguments.front().kind != TypeKind::Contract ||
            (actual.kind != TypeKind::View && actual.kind != TypeKind::Edit &&
             actual.kind != TypeKind::Own) ||
            actual.arguments.size() != 1 || actual.arguments.front().kind != TypeKind::Struct ||
            (expected.kind == TypeKind::Edit && actual.kind != TypeKind::Edit) ||
            (expected.kind == TypeKind::Own && actual.kind != TypeKind::Own) ||
            (expected.kind != TypeKind::Own && actual.kind == TypeKind::Own)) {
            return std::nullopt;
        }
        const auto concrete = actual.arguments.front();
        const auto contract = expected.arguments.front();
        const auto implemented = implementedContract(concrete, contract.declaration);
        if (!implemented.has_value() || implemented->ambiguous ||
            implemented->contract != contract) {
            return std::nullopt;
        }

        CallTarget::ContractConversion conversion;
        conversion.sourceType = actual;
        conversion.concreteType = concrete;
        conversion.contractType = contract;
        conversion.targetType = expected;
        const auto &methods = model_.contracts[contract.declaration].methods;
        conversion.methods.reserve(methods.size());
        for (std::size_t method = 0; method < methods.size(); ++method) {
            std::unordered_set<std::string> active;
            const auto target = contractMethodTarget(concrete, contract, method, active);
            if (!target.has_value()) {
                return std::nullopt;
            }
            conversion.methods.push_back(*target);
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

    bool isOption(const Type &type) const {
        return type.kind == TypeKind::Enum && type.declaration < program_.enums.size() &&
               program_.enums[type.declaration].builtin == BuiltinEnumKind::Option;
    }

    bool requiresDrop(const Type &type) const {
        std::unordered_set<std::string> active;
        return requiresDrop(type, active);
    }

    bool requiresDrop(const Type &type, std::unordered_set<std::string> &active) const {
        if (type.kind == TypeKind::String || type.kind == TypeKind::Own ||
            type.kind == TypeKind::Task || type.kind == TypeKind::Channel ||
            type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver ||
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

    std::vector<FirLocalId> activeDropsFrom(std::size_t firstScope) const {
        std::vector<FirLocalId> drops;
        for (auto index = scopes_.size(); index > firstScope; --index) {
            auto current = scopeDrops(scopes_[index - 1]);
            drops.insert(drops.end(), current.begin(), current.end());
        }
        return drops;
    }

    void reportScopesFrom(std::size_t firstScope) {
        ++loopJumps_;
        for (auto index = scopes_.size(); index > firstScope; --index) {
            reportScope(scopes_[index - 1]);
        }
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
        if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
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
            std::string name = isTransferableFunction(type) ? "transferable fn(" : "fn(";
            for (std::size_t index = 1; index < type.arguments.size(); ++index) {
                if (index != 1) {
                    name += ", ";
                }
                const auto &parameter = type.arguments[index];
                if (parameter.kind == TypeKind::View && parameter.declaration == 1 &&
                    parameter.arguments.size() == 1) {
                    name += displayType(parameter.arguments.front());
                } else if (parameter.kind == TypeKind::Edit &&
                           parameter.arguments.size() == 1) {
                    name += '&' + displayType(parameter.arguments.front());
                } else if (parameter.kind != TypeKind::Own &&
                           !isCopyParameterType(parameter)) {
                    name += '$' + displayType(parameter);
                } else {
                    name += displayType(parameter);
                }
            }
            name += ") ";
            name += displayType(type.arguments.front());
            return name;
        }
        if (type.kind == TypeKind::Task && type.arguments.size() == 1) {
            return "Task<" + displayType(type.arguments.front()) + '>';
        }
        if ((type.kind == TypeKind::Channel || type.kind == TypeKind::Sender ||
             type.kind == TypeKind::Receiver) && type.arguments.size() == 1) {
            return std::string(typeName(type)) + '<' + displayType(type.arguments.front()) + '>';
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
            expected == actual || actual == neverType) {
            return;
        }
        if (expected.kind == TypeKind::RawConst && actual.kind == TypeKind::Raw &&
            expected.arguments == actual.arguments) {
            return;
        }
        if (expected.kind == TypeKind::Function && expected.declaration == 0 &&
            isTransferableFunction(actual) && expected.arguments == actual.arguments) {
            return;
        }
        if ((expected.kind == TypeKind::View || expected.kind == TypeKind::Edit) &&
            expected.kind == actual.kind && expected.declaration == actual.declaration &&
            expected.arguments.size() == 1 && actual.arguments.size() == 1 &&
            expected.arguments.front().kind == TypeKind::Function &&
            expected.arguments.front().declaration == 0 &&
            isTransferableFunction(actual.arguments.front()) &&
            expected.arguments.front().arguments == actual.arguments.front().arguments) {
            return;
        }
        if ((expected.kind == TypeKind::Raw || expected.kind == TypeKind::RawConst) &&
            expected.arguments.size() == 1 && expected.arguments.front() == voidType &&
            (actual.kind == TypeKind::Raw || actual.kind == TypeKind::RawConst) &&
            actual.arguments.size() == 1 &&
            !(expected.kind == TypeKind::Raw && actual.kind == TypeKind::RawConst)) {
            return;
        }
        diagnostics_.error("FDN2011",
                           std::string(context) + " expects " + displayType(expected) + ", got " +
                               displayType(actual),
                           span);
    }

    const Program &program_;
    Diagnostics &diagnostics_;
    AnalyzeOptions options_;
    SemanticModel model_;
    std::unordered_map<std::string, FirStructId> structs_;
    std::unordered_map<std::string, FirEnumId> enums_;
    std::unordered_map<std::string, std::size_t> contracts_;
    std::unordered_map<std::string, FirAttributeId> attributes_;
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
    std::string_view currentPackageOverride_;
    bool transientBorrowsAllowed_{};
    std::optional<AstExpressionId> spawnCall_;
    std::optional<AstExpressionId> taskWaitRoot_;
    bool taskWaitVoidStatement_{};
    std::size_t channelStorage_{};
    std::size_t blockingStorage_{};
    std::size_t callbackStorage_{};
    std::size_t iterationStorage_{};
    std::vector<std::size_t> loopScopeBases_;
    std::size_t loopJumps_{};
    std::size_t unsafeDepth_{};
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics,
                                     AnalyzeOptions options) {
    Analyzer analyzer(program, diagnostics, options);
    return analyzer.run();
}

} // namespace foundation
