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
        model_.expressionMoves.resize(program.expressions.size());
        model_.statementLocals.resize(program.statements.size());
        model_.statementElseLocals.resize(program.statements.size());
        model_.statementDrops.resize(program.statements.size());
        model_.blockDrops.resize(program.blocks.size());
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

    bool containsBorrow(const Type &type) const {
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
                if (containsBorrow(type)) {
                    diagnostics_.error("FDN2062", "borrow cannot be stored in an enum payload",
                                       source.span);
                }
                if (containsBareSlice(type)) {
                    diagnostics_.error("FDN2080", "slice type requires view or edit", source.span);
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
                    child = child.arguments.front();
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
                        child = child.arguments.front();
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
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            setTypeParameters(function.typeParameters, function.span);
            semantic.typeParameterCount = function.typeParameters.size();
            semantic.returnType = resolveType(function.returnType);
            if (containsBorrow(semantic.returnType) ||
                containsBareSlice(semantic.returnType)) {
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
            model_.statementLocals[id] =
                addLocal(variable->name, declared, variable->mutableBinding, statement.span);
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
                requireSame(declaration.type, value, statement.span, "assignment");
                if (requiresDrop(declaration.type)) {
                    if (loanStates_[*local] != LoanState::None) {
                        diagnostics_.error("FDN2075", "cannot replace borrowed binding " +
                                                            declaration.name,
                                           statement.span);
                    }
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
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto thenState = outstandingPrefix(before.size());
            const auto thenMoves = movePrefix(movesBefore.size());
            restoreOutstanding(before);
            restoreMoves(movesBefore);
            auto elseReturns = false;
            if (branch->elseBlock.has_value()) {
                elseReturns = analyzeBlock(*branch->elseBlock, true);
            }
            const auto elseState = outstandingPrefix(before.size());
            const auto elseMoves = movePrefix(movesBefore.size());
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
            type = analyzeArray(*array, expected, expression.span);
        } else if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            const auto local = findLocal(name->name, expression.span);
            if (local.has_value()) {
                model_.expressionLocals[id] = *local;
                type = model_.functions[currentFunction_].locals[*local].type;
                if (requiresDrop(type)) {
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

    Type analyzeArray(const ArrayExpression &array, std::optional<Type> expected,
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
        for (std::size_t index = 1; index < array.elements.size(); ++index) {
            const auto current = analyzeExpression(array.elements[index], element);
            requireSame(element, current, program_.expressions[array.elements[index]].span,
                        "array element");
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
            base = base.arguments.front();
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
            model_.callTargets[id] =
                CallTarget{CallTargetKind::Print, 0, {}, std::move(drops)};
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
            model_.callTargets[id] = CallTarget{CallTargetKind::Panic, 0, {}, {}};
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
        for (std::size_t index = 0; index < count; ++index) {
            requireSame(substitute(signature.parameters[index], typeArguments), arguments[index],
                        span, "function argument");
        }
        model_.callTargets[id] =
            CallTarget{CallTargetKind::Function, function, typeArguments, {}};
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

        auto base = analyzeExpression(*member.base, std::nullopt, ExpressionUse::Inspect);
        if (base.kind == TypeKind::Invalid) {
            return invalidType;
        }
        if ((base.kind == TypeKind::Own || base.kind == TypeKind::View ||
             base.kind == TypeKind::Edit) &&
            base.arguments.size() == 1) {
            base = base.arguments.front();
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
        const auto result =
            substitute(model_.structs[base.declaration].fieldTypes[*resolved], base.arguments);
        if (use == ExpressionUse::Consume && requiresDrop(result)) {
            diagnostics_.error("FDN2078", "owned field cannot move independently", span);
        }
        return result;
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
        const auto movesBeforeArms = moveStates_;
        std::vector<bool> covered(declaration.variants.size());
        std::vector<FirVariantId> variants;
        std::vector<std::optional<FirLocalId>> bindings;
        std::vector<std::vector<FirLocalId>> drops;
        std::vector<std::vector<bool>> armStates;
        std::vector<std::vector<MoveState>> armMoveStates;
        auto result = invalidType;
        for (const auto &arm : match.arms) {
            restoreOutstanding(beforeArms);
            restoreMoves(movesBeforeArms);
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
            drops.push_back(scopeDrops(scopes_.back()));
            reportScope(scopes_.back());
            scopes_.pop_back();
            armStates.push_back(outstandingPrefix(beforeArms.size()));
            armMoveStates.push_back(movePrefix(movesBeforeArms.size()));
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
        for (std::size_t variant = 0; variant < covered.size(); ++variant) {
            if (!covered[variant]) {
                diagnostics_.error("FDN2040",
                                   "match does not cover " + declaration.variants[variant].name,
                                   span);
            }
        }
        model_.matchTargets[id] = MatchTarget{value, std::move(variants), std::move(bindings),
                                              std::move(drops)};
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
        if ((pattern.kind == TypeKind::Struct || pattern.kind == TypeKind::Enum ||
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
        diagnostics_.error("FDN2004", "unknown binding " + name, span);
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
    std::vector<MoveState> moveStates_;
    std::vector<LoanState> loanStates_;
    bool transientBorrowsAllowed_{};
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

} // namespace foundation
