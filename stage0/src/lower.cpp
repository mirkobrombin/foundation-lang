#include "foundation/lower.hpp"

#include <exception>
#include <optional>
#include <utility>

namespace foundation {

namespace {

template <typename T> const T &required(const std::optional<T> &value) {
    if (!value.has_value()) {
        std::terminate();
    }
    return *value;
}

FirUnaryOperator lowerUnary(UnaryOperator operation) {
    switch (operation) {
    case UnaryOperator::Negate:
        return FirUnaryOperator::Negate;
    case UnaryOperator::Not:
        return FirUnaryOperator::Not;
    }
    std::terminate();
}

FirBinaryOperator lowerBinary(BinaryOperator operation) {
    switch (operation) {
    case BinaryOperator::Add:
        return FirBinaryOperator::Add;
    case BinaryOperator::Subtract:
        return FirBinaryOperator::Subtract;
    case BinaryOperator::Multiply:
        return FirBinaryOperator::Multiply;
    case BinaryOperator::Divide:
        return FirBinaryOperator::Divide;
    case BinaryOperator::Remainder:
        return FirBinaryOperator::Remainder;
    case BinaryOperator::Equal:
        return FirBinaryOperator::Equal;
    case BinaryOperator::NotEqual:
        return FirBinaryOperator::NotEqual;
    case BinaryOperator::Less:
        return FirBinaryOperator::Less;
    case BinaryOperator::LessEqual:
        return FirBinaryOperator::LessEqual;
    case BinaryOperator::Greater:
        return FirBinaryOperator::Greater;
    case BinaryOperator::GreaterEqual:
        return FirBinaryOperator::GreaterEqual;
    case BinaryOperator::And:
        return FirBinaryOperator::And;
    case BinaryOperator::Or:
        return FirBinaryOperator::Or;
    }
    std::terminate();
}

class Lowerer {
  public:
    Lowerer(const Program &program, const SemanticModel &model)
        : program_(program), model_(model) {}

    FirProgram run() {
        FirProgram result;
        result.main = model_.main;
        result.structs.reserve(program_.structs.size());
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            FirStruct type;
            type.name = program_.structs[index].name;
            type.fields.reserve(program_.structs[index].fields.size());
            for (std::size_t field = 0; field < program_.structs[index].fields.size(); ++field) {
                type.fields.push_back({program_.structs[index].fields[field].name,
                                       model_.structs[index].fieldTypes[field]});
            }
            result.structs.push_back(std::move(type));
        }
        result.functions.reserve(program_.functions.size());
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            result.functions.push_back(lowerFunction(index));
        }
        return result;
    }

  private:
    FirFunction lowerFunction(FirFunctionId id) {
        expressionMap_.assign(program_.expressions.size(), std::nullopt);
        statementMap_.assign(program_.statements.size(), std::nullopt);
        blockMap_.assign(program_.blocks.size(), std::nullopt);

        const auto &source = program_.functions[id];
        const auto &semantic = model_.functions[id];
        FirFunction function;
        function.name = source.name;
        function.returnType = semantic.returnType;
        function.parameters = semantic.parameters;
        function.locals.reserve(semantic.locals.size());
        for (const auto &local : semantic.locals) {
            function.locals.push_back({local.name, local.type, local.mutableBinding});
        }
        current_ = &function;
        function.body = lowerBlock(source.body);
        current_ = nullptr;
        return function;
    }

    FirBlockId lowerBlock(AstBlockId id) {
        if (blockMap_[id].has_value()) {
            return *blockMap_[id];
        }
        const auto lowered = current_->blocks.size();
        current_->blocks.emplace_back();
        blockMap_[id] = lowered;

        std::vector<FirStatementId> statements;
        statements.reserve(program_.blocks[id].statements.size());
        for (const auto statement : program_.blocks[id].statements) {
            statements.push_back(lowerStatement(statement));
        }
        current_->blocks[lowered].statements = std::move(statements);
        return lowered;
    }

    FirStatementId lowerStatement(AstStatementId id) {
        if (statementMap_[id].has_value()) {
            return *statementMap_[id];
        }

        const auto &source = program_.statements[id];
        FirStatementValue value;
        if (const auto *variable = std::get_if<VariableStatement>(&source.value)) {
            value = FirVariableStatement{required(model_.statementLocals[id]),
                                         lowerExpression(variable->initializer)};
        } else if (const auto *assignment = std::get_if<AssignmentStatement>(&source.value)) {
            value = FirAssignmentStatement{required(model_.statementLocals[id]),
                                           lowerExpression(assignment->value)};
        } else if (const auto *expression = std::get_if<ExpressionStatement>(&source.value)) {
            value = FirExpressionStatement{lowerExpression(expression->expression)};
        } else if (const auto *returned = std::get_if<ReturnStatement>(&source.value)) {
            std::optional<FirExpressionId> result;
            if (returned->value.has_value()) {
                result = lowerExpression(*returned->value);
            }
            value = FirReturnStatement{result};
        } else if (const auto *branch = std::get_if<IfStatement>(&source.value)) {
            const auto condition = lowerExpression(branch->condition);
            const auto thenBlock = lowerBlock(branch->thenBlock);
            std::optional<FirBlockId> elseBlock;
            if (branch->elseBlock.has_value()) {
                elseBlock = lowerBlock(*branch->elseBlock);
            }
            value = FirIfStatement{condition, thenBlock, elseBlock};
        } else {
            const auto &loop = std::get<WhileStatement>(source.value);
            value = FirWhileStatement{lowerExpression(loop.condition), lowerBlock(loop.body)};
        }

        const auto lowered = current_->statements.size();
        current_->statements.push_back({std::move(value), source.span});
        statementMap_[id] = lowered;
        return lowered;
    }

    FirExpressionId lowerExpression(AstExpressionId id) {
        if (expressionMap_[id].has_value()) {
            return *expressionMap_[id];
        }

        const auto &source = program_.expressions[id];
        FirExpressionValue value;
        if (const auto *integer = std::get_if<IntegerExpression>(&source.value)) {
            value = FirIntegerExpression{static_cast<std::int32_t>(integer->value)};
        } else if (const auto *boolean = std::get_if<BooleanExpression>(&source.value)) {
            value = FirBooleanExpression{boolean->value};
        } else if (const auto *string = std::get_if<StringExpression>(&source.value)) {
            value = FirStringExpression{string->value};
        } else if (std::holds_alternative<NameExpression>(source.value)) {
            value = FirLocalExpression{required(model_.expressionLocals[id])};
        } else if (const auto *unary = std::get_if<UnaryExpression>(&source.value)) {
            value = FirUnaryExpression{lowerUnary(unary->operation),
                                       lowerExpression(unary->operand)};
        } else if (const auto *binary = std::get_if<BinaryExpression>(&source.value)) {
            value = FirBinaryExpression{lowerExpression(binary->left),
                                        lowerBinary(binary->operation),
                                        lowerExpression(binary->right)};
        } else if (const auto *call = std::get_if<CallExpression>(&source.value)) {
            const auto &target = required(model_.callTargets[id]);
            std::vector<FirExpressionId> arguments;
            arguments.reserve(call->arguments.size());
            for (const auto argument : call->arguments) {
                arguments.push_back(lowerExpression(argument));
            }
            const auto kind = target.kind == CallTargetKind::Print ? FirCallKind::Print
                                                                   : FirCallKind::Function;
            value = FirCallExpression{kind, target.function, std::move(arguments)};
        } else if (const auto *literal = std::get_if<StructExpression>(&source.value)) {
            const auto &target = required(model_.structTargets[id]);
            std::vector<FirStructFieldValue> fields;
            fields.reserve(literal->fields.size());
            for (std::size_t index = 0; index < literal->fields.size(); ++index) {
                fields.push_back(
                    {target.fields[index], lowerExpression(literal->fields[index].value)});
            }
            value = FirStructExpression{target.type, std::move(fields)};
        } else {
            const auto &field = std::get<FieldExpression>(source.value);
            value = FirFieldExpression{lowerExpression(field.base),
                                       required(model_.expressionFields[id])};
        }

        const auto lowered = current_->expressions.size();
        current_->expressions.push_back(
            {std::move(value), model_.expressionTypes[id], source.span});
        expressionMap_[id] = lowered;
        return lowered;
    }

    const Program &program_;
    const SemanticModel &model_;
    FirFunction *current_{};
    std::vector<std::optional<FirExpressionId>> expressionMap_;
    std::vector<std::optional<FirStatementId>> statementMap_;
    std::vector<std::optional<FirBlockId>> blockMap_;
};

} // namespace

FirProgram lower(const Program &program, const SemanticModel &model) {
    Lowerer lowerer(program, model);
    return lowerer.run();
}

} // namespace foundation
