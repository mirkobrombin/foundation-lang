#include "foundation/sema.hpp"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace foundation {

namespace {

struct FunctionSignature {
    TypeKind returnType{TypeKind::Invalid};
    std::vector<TypeKind> parameters;
};

class Analyzer {
  public:
    Analyzer(const Program &program, Diagnostics &diagnostics)
        : program_(program), diagnostics_(diagnostics) {
        model_.expressionTypes.resize(program.expressions.size(), TypeKind::Invalid);
        model_.expressionLocals.resize(program.expressions.size());
        model_.callTargets.resize(program.expressions.size());
        model_.statementLocals.resize(program.statements.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
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
    void declareFunctions() {
        bool foundMain = false;
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            semantic.returnType = resolveType(function.returnType, function.span);
            for (const auto &parameter : function.parameters) {
                const auto type = resolveType(parameter.typeName, parameter.span);
                if (type == TypeKind::Void) {
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
            if (!function.parameters.empty() || semantic.returnType != TypeKind::I32) {
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
        if (semantic.returnType != TypeKind::Void && !returns) {
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
            if (declared == TypeKind::Void) {
                diagnostics_.error("FDN2016", "binding initializer cannot be void", statement.span);
                declared = TypeKind::Invalid;
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
                if (expected != TypeKind::Void) {
                    diagnostics_.error("FDN2014", "non-void function must return a value",
                                       statement.span);
                }
                return true;
            }
            const auto value = analyzeExpression(*returned->value);
            if (expected == TypeKind::Void) {
                diagnostics_.error("FDN2015", "void function must use a bare return",
                                   statement.span);
                return true;
            }
            requireSame(expected, value, statement.span, "return");
            return true;
        }
        if (const auto *branch = std::get_if<IfStatement>(&statement.value)) {
            requireSame(TypeKind::Bool, analyzeExpression(branch->condition), statement.span,
                        "if condition");
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto elseReturns =
                branch->elseBlock.has_value() && analyzeBlock(*branch->elseBlock, true);
            return thenReturns && elseReturns;
        }

        const auto &loop = std::get<WhileStatement>(statement.value);
        requireSame(TypeKind::Bool, analyzeExpression(loop.condition), statement.span,
                    "while condition");
        static_cast<void>(analyzeBlock(loop.body, true));
        return false;
    }

    TypeKind analyzeExpression(AstExpressionId id) {
        const auto &expression = program_.expressions[id];
        auto type = TypeKind::Invalid;
        if (const auto *integer = std::get_if<IntegerExpression>(&expression.value)) {
            if (integer->value < INT32_MIN || integer->value > INT32_MAX) {
                diagnostics_.error("FDN2005", "integer literal does not fit i32", expression.span);
            }
            type = TypeKind::I32;
        } else if (std::holds_alternative<BooleanExpression>(expression.value)) {
            type = TypeKind::Bool;
        } else if (std::holds_alternative<StringExpression>(expression.value)) {
            type = TypeKind::String;
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
        } else {
            type = analyzeCall(id, std::get<CallExpression>(expression.value), expression.span);
        }
        model_.expressionTypes[id] = type;
        return type;
    }

    TypeKind analyzeUnary(const UnaryExpression &unary, SourceSpan span) {
        const auto operand = analyzeExpression(unary.operand);
        if (unary.operation == UnaryOperator::Negate) {
            requireSame(TypeKind::I32, operand, span, "unary -");
            return TypeKind::I32;
        }
        requireSame(TypeKind::Bool, operand, span, "unary !");
        return TypeKind::Bool;
    }

    TypeKind analyzeBinary(const BinaryExpression &binary, SourceSpan span) {
        const auto left = analyzeExpression(binary.left);
        const auto right = analyzeExpression(binary.right);
        switch (binary.operation) {
        case BinaryOperator::Add:
        case BinaryOperator::Subtract:
        case BinaryOperator::Multiply:
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:
            requireSame(TypeKind::I32, left, span, "arithmetic operand");
            requireSame(TypeKind::I32, right, span, "arithmetic operand");
            return TypeKind::I32;
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual:
            requireSame(TypeKind::I32, left, span, "comparison operand");
            requireSame(TypeKind::I32, right, span, "comparison operand");
            return TypeKind::Bool;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            if (left == TypeKind::String || right == TypeKind::String) {
                diagnostics_.error("FDN2012", "String equality is not available in this stage",
                                   span);
            } else {
                requireSame(left, right, span, "equality operand");
                if (left == TypeKind::Void) {
                    diagnostics_.error("FDN2012", "void values cannot be compared", span);
                }
            }
            return TypeKind::Bool;
        case BinaryOperator::And:
        case BinaryOperator::Or:
            requireSame(TypeKind::Bool, left, span, "logical operand");
            requireSame(TypeKind::Bool, right, span, "logical operand");
            return TypeKind::Bool;
        }
        return TypeKind::Invalid;
    }

    TypeKind analyzeCall(AstExpressionId id, const CallExpression &call, SourceSpan span) {
        std::vector<TypeKind> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(analyzeExpression(argument));
        }

        if (call.callee == "print") {
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "print expects one argument", span);
            } else {
                requireSame(TypeKind::String, arguments.front(), span, "print argument");
            }
            model_.callTargets[id] = CallTarget{CallTargetKind::Print, 0};
            return TypeKind::Void;
        }

        const auto found = functions_.find(call.callee);
        if (found == functions_.end()) {
            diagnostics_.error("FDN2009", "unknown function " + call.callee, span);
            return TypeKind::Invalid;
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

    TypeKind resolveType(std::string_view name, SourceSpan span) {
        if (name == "void") {
            return TypeKind::Void;
        }
        if (name == "i32") {
            return TypeKind::I32;
        }
        if (name == "bool") {
            return TypeKind::Bool;
        }
        if (name == "String") {
            return TypeKind::String;
        }
        diagnostics_.error("FDN2002", "unknown type " + std::string(name), span);
        return TypeKind::Invalid;
    }

    FirLocalId addLocal(const std::string &name, TypeKind type, bool mutableBinding,
                        SourceSpan span) {
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

    void requireSame(TypeKind expected, TypeKind actual, SourceSpan span,
                     std::string_view context) {
        if (expected == TypeKind::Invalid || actual == TypeKind::Invalid || expected == actual) {
            return;
        }
        diagnostics_.error("FDN2011",
                           std::string(context) + " expects " + typeName(expected) + ", got " +
                               typeName(actual),
                           span);
    }

    const Program &program_;
    Diagnostics &diagnostics_;
    SemanticModel model_;
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
