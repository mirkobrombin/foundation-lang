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
        model_.statementLocals.resize(program.statements.size());
        model_.statementElseLocals.resize(program.statements.size());
        model_.structs.resize(program.structs.size());
        model_.enums.resize(program.enums.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
        declareStructs();
        declareEnums();
        resolveStructs();
        resolveEnums();
        rejectTypeCycles();
        declareFunctions();
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            analyzeFunction(index);
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
            if (declaration.fields.empty()) {
                diagnostics_.error("FDN2029", "struct must declare at least one field",
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
                semantic.fieldTypes.push_back(type);
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
                children.push_back({substitute(fields[index], type.arguments),
                                    program_.structs[type.declaration].fields[index].span});
            }
        } else if (type.kind == TypeKind::Enum && type.declaration < model_.enums.size()) {
            const auto &variants = model_.enums[type.declaration].payloadTypes;
            for (std::size_t index = 0; index < variants.size(); ++index) {
                if (variants[index].has_value()) {
                    children.push_back({substitute(*variants[index], type.arguments),
                                        program_.enums[type.declaration].variants[index].span});
                }
            }
        }
        return children;
    }

    void declareFunctions() {
        bool foundMain = false;
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            setTypeParameters(function.typeParameters, function.span);
            semantic.typeParameterCount = function.typeParameters.size();
            semantic.returnType = resolveType(function.returnType);
            for (const auto &parameter : function.parameters) {
                const auto type = resolveType(parameter.type);
                if (type == voidType) {
                    diagnostics_.error("FDN2016", "parameter cannot have type void",
                                       parameter.span);
                }
                semantic.parameterTypes.push_back(type);
            }

            if (!functions_.emplace(function.name, index).second) {
                diagnostics_.error("FDN2001", "duplicate function " + function.name,
                                   function.span);
            }
            if (function.name == "print") {
                diagnostics_.error("FDN2018", "print is a reserved builtin", function.span);
            }
            if (function.name == "panic") {
                diagnostics_.error("FDN2018", "panic is a reserved builtin", function.span);
            }
            signatures_.push_back({semantic.returnType, semantic.parameterTypes});

            if (function.name != "main") {
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

    void analyzeFunction(std::size_t index) {
        currentFunction_ = index;
        scopes_.clear();
        scopes_.emplace_back();
        resultOutstanding_.clear();
        resultExitReported_.clear();
        localSpans_.clear();

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
            model_.statementLocals[id] =
                addLocal(variable->name, declared, variable->mutableBinding, statement.span);
            return false;
        }
        if (const auto *assignment = std::get_if<AssignmentStatement>(&statement.value)) {
            const auto local = findLocal(assignment->name, statement.span);
            const auto expected = local.has_value()
                                      ? std::optional<Type>{model_.functions[currentFunction_]
                                                                .locals[*local]
                                                                .type}
                                      : std::nullopt;
            const auto value = analyzeExpression(assignment->value, expected);
            if (local.has_value()) {
                model_.statementLocals[id] = *local;
                const auto &declaration = model_.functions[currentFunction_].locals[*local];
                if (!declaration.mutableBinding) {
                    diagnostics_.error("FDN2013", "cannot assign to immutable binding " +
                                                        assignment->name,
                                       statement.span);
                }
                requireSame(declaration.type, value, statement.span, "assignment");
                if (isResult(declaration.type)) {
                    if (resultOutstanding_[*local]) {
                        diagnostics_.error("FDN2052", "assignment replaces an unhandled Result",
                                           statement.span);
                    }
                    resultOutstanding_[*local] = true;
                }
            }
            return false;
        }
        if (const auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            const auto type = analyzeExpression(expression->expression);
            if (isResult(type)) {
                diagnostics_.error("FDN2051", "Result value must be handled or discarded",
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
                return true;
            }
            const auto value = analyzeExpression(*returned->value, expected);
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
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto thenState = outstandingPrefix(before.size());
            restoreOutstanding(before);
            auto elseReturns = false;
            if (branch->elseBlock.has_value()) {
                elseReturns = analyzeBlock(*branch->elseBlock, true);
            }
            const auto elseState = outstandingPrefix(before.size());
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
            return thenReturns && elseReturns;
        }

        const auto &loop = std::get<WhileStatement>(statement.value);
        requireSame(boolType, analyzeExpression(loop.condition), statement.span,
                    "while condition");
        const auto before = resultOutstanding_;
        static_cast<void>(analyzeBlock(loop.body, true));
        const auto bodyState = outstandingPrefix(before.size());
        std::vector<bool> merged(before.size());
        for (std::size_t local = 0; local < before.size(); ++local) {
            merged[local] = before[local] || bodyState[local];
        }
        restoreOutstanding(merged);
        return false;
    }

    Type analyzeExpression(AstExpressionId id, std::optional<Type> expected = std::nullopt) {
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
        } else if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            const auto local = findLocal(name->name, expression.span);
            if (local.has_value()) {
                model_.expressionLocals[id] = *local;
                type = model_.functions[currentFunction_].locals[*local].type;
                if (isResult(type)) {
                    resultOutstanding_[*local] = false;
                }
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            type = analyzeUnary(*unary, expression.span);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
            type = analyzeBinary(*binary, expression.span);
        } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
            type = analyzeCall(id, *call, expression.span);
        } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            type = analyzeStruct(id, *literal, expression.span);
        } else if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            type = analyzeMember(id, *member, expected, expression.span);
        } else {
            type = analyzeMatch(id, std::get<MatchExpression>(expression.value), expected,
                                expression.span);
        }
        model_.expressionTypes[id] = type;
        return type;
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

    Type analyzeBinary(const BinaryExpression &binary, SourceSpan span) {
        const auto left = analyzeExpression(binary.left);
        const auto right = analyzeExpression(binary.right);
        switch (binary.operation) {
        case BinaryOperator::Add:
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
            if (left.kind == TypeKind::String || right.kind == TypeKind::String ||
                left.kind == TypeKind::Parameter || right.kind == TypeKind::Parameter ||
                left.kind == TypeKind::Struct || right.kind == TypeKind::Struct ||
                left.kind == TypeKind::Enum || right.kind == TypeKind::Enum) {
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
        std::vector<Type> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(analyzeExpression(argument));
        }
        std::vector<Type> explicitTypes;
        explicitTypes.reserve(call.typeArguments.size());
        for (const auto &argument : call.typeArguments) {
            explicitTypes.push_back(resolveType(argument));
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
            model_.callTargets[id] = CallTarget{CallTargetKind::Print, 0, {}};
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
            model_.callTargets[id] = CallTarget{CallTargetKind::Panic, 0, {}};
            return voidType;
        }

        const auto found = functions_.find(call.callee);
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
        std::vector<std::optional<Type>> inferred(program_.functions[function].typeParameters.size());
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
        for (std::size_t index = 0; index < count; ++index) {
            requireSame(substitute(signature.parameters[index], typeArguments), arguments[index],
                        span, "function argument");
        }
        model_.callTargets[id] =
            CallTarget{CallTargetKind::Function, function, typeArguments};
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
            const auto value = analyzeExpression(initializer.value);
            const auto field = findField(type, initializer.name);
            if (!field.has_value()) {
                diagnostics_.error("FDN2025", "unknown field " + initializer.name,
                                   initializer.span);
                continue;
            }
            if (initialized[*field]) {
                diagnostics_.error("FDN2026", "duplicate field initializer " + initializer.name,
                                   initializer.span);
                continue;
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
                       std::optional<Type> expected, SourceSpan span) {
        if (!member.base.has_value()) {
            return analyzeEnum(id, member, std::nullopt, expected, span);
        }

        const auto &baseExpression = program_.expressions[*member.base];
        if (const auto *name = std::get_if<NameExpression>(&baseExpression.value);
            name != nullptr && !lookupLocal(name->name).has_value() &&
            enums_.contains(name->name)) {
            return analyzeEnum(id, member,
                               TypeSyntax{name->name, name->typeArguments, baseExpression.span},
                               expected, span);
        }

        if (member.invoked) {
            if (member.payload.has_value()) {
                static_cast<void>(analyzeExpression(*member.payload));
            }
            diagnostics_.error("FDN2050", "member functions are not available", span);
            return invalidType;
        }

        const auto base = analyzeExpression(*member.base);
        if (base.kind == TypeKind::Invalid) {
            return invalidType;
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
        model_.expressionFields[id] = *resolved;
        return substitute(model_.structs[base.declaration].fieldTypes[*resolved], base.arguments);
    }

    Type analyzeEnum(AstExpressionId id, const MemberExpression &constructor,
                     std::optional<TypeSyntax> explicitType, std::optional<Type> contextualType,
                     SourceSpan span) {
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
            if (constructor.payload.has_value()) {
                static_cast<void>(analyzeExpression(*constructor.payload));
            }
            diagnostics_.error("FDN2034", "cannot resolve enum for ." + constructor.member,
                               span);
            return invalidType;
        }
        const auto variant = findVariant(*enumType, constructor.member);
        if (!variant.has_value()) {
            if (constructor.payload.has_value()) {
                static_cast<void>(analyzeExpression(*constructor.payload));
            }
            diagnostics_.error("FDN2035", "unknown variant " + constructor.member, span);
            return invalidType;
        }

        const auto &declaration = program_.enums[*enumType];
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
        if (payloadPattern.has_value() && !constructor.payload.has_value()) {
            diagnostics_.error("FDN2036", "variant requires a payload", span);
        } else if (!payloadPattern.has_value() && constructor.payload.has_value()) {
            static_cast<void>(analyzeExpression(*constructor.payload));
            diagnostics_.error("FDN2036", "unit variant does not accept a payload", span);
        } else if (payloadPattern.has_value()) {
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
            payloadType = analyzeExpression(*constructor.payload, expectedPayload);
            inferType(*payloadPattern, *payloadType, inferred, span, "variant payload");
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
        std::vector<bool> covered(declaration.variants.size());
        std::vector<FirVariantId> variants;
        std::vector<std::optional<FirLocalId>> bindings;
        std::vector<std::vector<bool>> armStates;
        auto result = invalidType;
        for (const auto &arm : match.arms) {
            restoreOutstanding(beforeArms);
            auto variant = findVariant(enumType, arm.variant);
            if (!variant.has_value()) {
                diagnostics_.error("FDN2035", "unknown variant " + arm.variant, arm.span);
            } else if (covered[*variant]) {
                diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                   arm.span);
            } else {
                covered[*variant] = true;
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
            reportScope(scopes_.back());
            scopes_.pop_back();
            armStates.push_back(outstandingPrefix(beforeArms.size()));
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
        for (std::size_t variant = 0; variant < covered.size(); ++variant) {
            if (!covered[variant]) {
                diagnostics_.error("FDN2040",
                                   "match does not cover " + declaration.variants[variant].name,
                                   span);
            }
        }
        model_.matchTargets[id] = MatchTarget{value, std::move(variants), std::move(bindings)};
        return result;
    }

    void setTypeParameters(const std::vector<std::string> &parameters, SourceSpan span) {
        typeParameters_.clear();
        currentTypeParameterNames_ = parameters;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto &name = parameters[index];
            if (isBuiltinType(name) || structs_.contains(name) || enums_.contains(name) ||
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
        } else if (const auto parameter = typeParameters_.find(syntax.name);
                   parameter != typeParameters_.end()) {
            base = Type{TypeKind::Parameter, parameter->second};
        } else if (const auto structFound = structs_.find(syntax.name);
                   structFound != structs_.end()) {
            base = Type{TypeKind::Struct, structFound->second};
        } else if (const auto enumFound = enums_.find(syntax.name);
                   enumFound != enums_.end()) {
            base = Type{TypeKind::Enum, enumFound->second};
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
        }
        if (arguments.size() != expected) {
            diagnostics_.error("FDN2043", "wrong type argument count for " + syntax.name,
                               syntax.span);
        }
        base->arguments = std::move(arguments);
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
        if ((pattern.kind == TypeKind::Struct || pattern.kind == TypeKind::Enum) &&
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
               name == "Option" || name == "Result";
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
        scope.emplace(name, id);
        return id;
    }

    bool isResult(const Type &type) const {
        return type.kind == TypeKind::Enum && type.declaration < program_.enums.size() &&
               program_.enums[type.declaration].builtin == BuiltinEnumKind::Result;
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
        diagnostics_.error("FDN2004", "unknown binding " + name, span);
        return std::nullopt;
    }

    std::string displayType(const Type &type) const {
        if (type.kind == TypeKind::Parameter &&
            type.declaration < currentTypeParameterNames_.size()) {
            return currentTypeParameterNames_[type.declaration];
        }
        std::string name;
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            name = program_.structs[type.declaration].name;
        } else if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            name = program_.enums[type.declaration].name;
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
    std::unordered_map<std::string, FirFunctionId> functions_;
    std::unordered_map<std::string, std::size_t> typeParameters_;
    std::vector<std::string> currentTypeParameterNames_;
    std::vector<FunctionSignature> signatures_;
    std::vector<GenericCall> genericCalls_;
    std::vector<std::unordered_map<std::string, FirLocalId>> scopes_;
    std::vector<bool> resultOutstanding_;
    std::vector<bool> resultExitReported_;
    std::vector<SourceSpan> localSpans_;
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

} // namespace foundation
