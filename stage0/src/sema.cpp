#include "foundation/sema.hpp"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace foundation {

namespace {

struct FunctionSignature {
    Type returnType{invalidType};
    std::vector<Type> parameters;
};

class Analyzer {
  public:
    Analyzer(const Program &program, Diagnostics &diagnostics)
        : program_(program), diagnostics_(diagnostics) {
        model_.expressionTypes.resize(program.expressions.size(), invalidType);
        model_.expressionLocals.resize(program.expressions.size());
        model_.callTargets.resize(program.expressions.size());
        model_.structTargets.resize(program.expressions.size());
        model_.expressionFields.resize(program.expressions.size());
        model_.statementLocals.resize(program.statements.size());
        model_.structs.resize(program.structs.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
        declareStructs();
        resolveStructs();
        rejectStructCycles();
        declareFunctions();
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            analyzeFunction(index);
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

    void resolveStructs() {
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &declaration = program_.structs[index];
            auto &semantic = model_.structs[index];
            std::unordered_map<std::string, std::size_t> fields;
            semantic.fieldTypes.reserve(declaration.fields.size());
            for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                const auto &source = declaration.fields[field];
                if (!fields.emplace(source.name, field).second) {
                    diagnostics_.error("FDN2021", "duplicate field " + source.name,
                                       source.span);
                }
                const auto type = resolveType(source.typeName, source.span);
                if (type == voidType) {
                    diagnostics_.error("FDN2022", "struct field cannot have type void",
                                       source.span);
                }
                semantic.fieldTypes.push_back(type);
            }
        }
    }

    void rejectStructCycles() {
        std::vector<std::size_t> dependencies(program_.structs.size());
        std::vector<std::vector<FirStructId>> dependents(program_.structs.size());
        for (std::size_t type = 0; type < model_.structs.size(); ++type) {
            for (const auto field : model_.structs[type].fieldTypes) {
                if (field.kind == TypeKind::Struct &&
                    field.declaration < program_.structs.size()) {
                    ++dependencies[type];
                    dependents[field.declaration].push_back(type);
                }
            }
        }

        std::vector<FirStructId> ready;
        for (std::size_t type = 0; type < dependencies.size(); ++type) {
            if (dependencies[type] == 0) {
                ready.push_back(type);
            }
        }
        for (std::size_t current = 0; current < ready.size(); ++current) {
            for (const auto dependent : dependents[ready[current]]) {
                if (--dependencies[dependent] == 0) {
                    ready.push_back(dependent);
                }
            }
        }
        if (ready.size() == program_.structs.size()) {
            return;
        }

        for (std::size_t type = 0; type < dependencies.size(); ++type) {
            if (dependencies[type] == 0) {
                continue;
            }
            const auto &fields = model_.structs[type].fieldTypes;
            for (std::size_t field = 0; field < fields.size(); ++field) {
                if (fields[field].kind == TypeKind::Struct &&
                    dependencies[fields[field].declaration] != 0) {
                    diagnostics_.error("FDN2023", "recursive value struct is not allowed",
                                       program_.structs[type].fields[field].span);
                    return;
                }
            }
        }
    }

    void declareFunctions() {
        bool foundMain = false;
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            semantic.returnType = resolveType(function.returnType, function.span);
            for (const auto &parameter : function.parameters) {
                const auto type = resolveType(parameter.typeName, parameter.span);
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
            signatures_.push_back({semantic.returnType, semantic.parameterTypes});

            if (function.name != "main") {
                continue;
            }
            if (!foundMain) {
                model_.main = index;
            }
            foundMain = true;
            if (!function.parameters.empty() || semantic.returnType != i32Type) {
                diagnostics_.error("FDN2007", "main must have signature fn main() -> i32",
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

        const auto &function = program_.functions[index];
        auto &semantic = model_.functions[index];
        for (std::size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
            const auto &parameter = function.parameters[parameterIndex];
            const auto local = addLocal(parameter.name, semantic.parameterTypes[parameterIndex],
                                        false, parameter.span);
            semantic.parameters.push_back(local);
        }

        const auto returns = analyzeBlock(function.body, false);
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
            scopes_.pop_back();
        }
        return returns;
    }

    bool analyzeStatement(AstStatementId id) {
        const auto &statement = program_.statements[id];
        if (const auto *variable = std::get_if<VariableStatement>(&statement.value)) {
            const auto initializer = analyzeExpression(variable->initializer);
            auto declared = initializer;
            if (variable->typeName.has_value()) {
                declared = resolveType(*variable->typeName, statement.span);
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
            const auto value = analyzeExpression(assignment->value);
            if (local.has_value()) {
                model_.statementLocals[id] = *local;
                const auto &declaration = model_.functions[currentFunction_].locals[*local];
                if (!declaration.mutableBinding) {
                    diagnostics_.error("FDN2013", "cannot assign to immutable binding " +
                                                        assignment->name,
                                       statement.span);
                }
                requireSame(declaration.type, value, statement.span, "assignment");
            }
            return false;
        }
        if (const auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            static_cast<void>(analyzeExpression(expression->expression));
            return false;
        }
        if (const auto *returned = std::get_if<ReturnStatement>(&statement.value)) {
            const auto expected = model_.functions[currentFunction_].returnType;
            if (!returned->value.has_value()) {
                if (expected != voidType) {
                    diagnostics_.error("FDN2014", "non-void function must return a value",
                                       statement.span);
                }
                return true;
            }
            const auto value = analyzeExpression(*returned->value);
            if (expected == voidType) {
                diagnostics_.error("FDN2015", "void function must use a bare return",
                                   statement.span);
                return true;
            }
            requireSame(expected, value, statement.span, "return");
            return true;
        }
        if (const auto *branch = std::get_if<IfStatement>(&statement.value)) {
            requireSame(boolType, analyzeExpression(branch->condition), statement.span,
                        "if condition");
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto elseReturns =
                branch->elseBlock.has_value() && analyzeBlock(*branch->elseBlock, true);
            return thenReturns && elseReturns;
        }

        const auto &loop = std::get<WhileStatement>(statement.value);
        requireSame(boolType, analyzeExpression(loop.condition), statement.span,
                    "while condition");
        static_cast<void>(analyzeBlock(loop.body, true));
        return false;
    }

    Type analyzeExpression(AstExpressionId id) {
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
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            type = analyzeUnary(*unary, expression.span);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
            type = analyzeBinary(*binary, expression.span);
        } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
            type = analyzeCall(id, *call, expression.span);
        } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            type = analyzeStruct(id, *literal, expression.span);
        } else {
            type = analyzeField(id, std::get<FieldExpression>(expression.value), expression.span);
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
                left.kind == TypeKind::Struct || right.kind == TypeKind::Struct) {
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

        if (call.callee == "print") {
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "print expects one argument", span);
            } else {
                requireSame(stringType, arguments.front(), span, "print argument");
            }
            model_.callTargets[id] = CallTarget{CallTargetKind::Print, 0};
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
        const auto count = std::min(arguments.size(), signature.parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            requireSame(signature.parameters[index], arguments[index], span,
                        "function argument");
        }
        model_.callTargets[id] = CallTarget{CallTargetKind::Function, function};
        return signature.returnType;
    }

    Type analyzeStruct(AstExpressionId id, const StructExpression &literal, SourceSpan span) {
        const auto found = structs_.find(literal.typeName);
        if (found == structs_.end()) {
            for (const auto &field : literal.fields) {
                static_cast<void>(analyzeExpression(field.value));
            }
            diagnostics_.error("FDN2024", "unknown struct " + literal.typeName, span);
            return invalidType;
        }

        const auto type = found->second;
        const auto &declaration = program_.structs[type];
        const auto &semantic = model_.structs[type];
        std::vector<bool> initialized(declaration.fields.size());
        std::vector<FirFieldId> fields;
        fields.reserve(literal.fields.size());
        for (const auto &initializer : literal.fields) {
            const auto value = analyzeExpression(initializer.value);
            const auto field = findField(type, initializer.name);
            if (!field.has_value()) {
                diagnostics_.error("FDN2025", "unknown field " + initializer.name,
                                   initializer.span);
                continue;
            }
            fields.push_back(*field);
            if (initialized[*field]) {
                diagnostics_.error("FDN2026", "duplicate field initializer " + initializer.name,
                                   initializer.span);
                continue;
            }
            initialized[*field] = true;
            requireSame(semantic.fieldTypes[*field], value, initializer.span,
                        "field initializer");
        }
        for (std::size_t field = 0; field < initialized.size(); ++field) {
            if (!initialized[field]) {
                diagnostics_.error("FDN2027", "missing field " + declaration.fields[field].name,
                                   span);
            }
        }
        model_.structTargets[id] = StructLiteralTarget{type, std::move(fields)};
        return Type{TypeKind::Struct, type};
    }

    Type analyzeField(AstExpressionId id, const FieldExpression &field, SourceSpan span) {
        const auto base = analyzeExpression(field.base);
        if (base.kind == TypeKind::Invalid) {
            return invalidType;
        }
        if (base.kind != TypeKind::Struct || base.declaration >= program_.structs.size()) {
            diagnostics_.error("FDN2028", "field access requires a struct value", span);
            return invalidType;
        }
        const auto resolved = findField(base.declaration, field.field);
        if (!resolved.has_value()) {
            diagnostics_.error("FDN2025", "unknown field " + field.field, span);
            return invalidType;
        }
        model_.expressionFields[id] = *resolved;
        return model_.structs[base.declaration].fieldTypes[*resolved];
    }

    Type resolveType(std::string_view name, SourceSpan span) {
        if (name == "void") {
            return voidType;
        }
        if (name == "i32") {
            return i32Type;
        }
        if (name == "bool") {
            return boolType;
        }
        if (name == "String") {
            return stringType;
        }
        const auto found = structs_.find(std::string(name));
        if (found != structs_.end()) {
            return Type{TypeKind::Struct, found->second};
        }
        diagnostics_.error("FDN2002", "unknown type " + std::string(name), span);
        return invalidType;
    }

    bool isBuiltinType(std::string_view name) const {
        return name == "void" || name == "i32" || name == "bool" || name == "String";
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

    FirLocalId addLocal(const std::string &name, Type type, bool mutableBinding, SourceSpan span) {
        auto &scope = scopes_.back();
        if (scope.contains(name)) {
            diagnostics_.error("FDN2003", "duplicate binding " + name, span);
            return scope[name];
        }

        auto &locals = model_.functions[currentFunction_].locals;
        const auto id = locals.size();
        locals.push_back({name, type, mutableBinding});
        scope.emplace(name, id);
        return id;
    }

    std::optional<FirLocalId> findLocal(const std::string &name, SourceSpan span) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) {
                return found->second;
            }
        }
        diagnostics_.error("FDN2004", "unknown binding " + name, span);
        return std::nullopt;
    }

    std::string displayType(Type type) const {
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            return program_.structs[type.declaration].name;
        }
        return typeName(type);
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
    std::unordered_map<std::string, FirFunctionId> functions_;
    std::vector<FunctionSignature> signatures_;
    std::vector<std::unordered_map<std::string, FirLocalId>> scopes_;
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

} // namespace foundation
