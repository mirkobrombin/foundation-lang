#ifndef FOUNDATION_FIR_HPP
#define FOUNDATION_FIR_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/type.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace foundation {

using FirExpressionId = std::size_t;
using FirStatementId = std::size_t;
using FirBlockId = std::size_t;
using FirFunctionId = std::size_t;
using FirLocalId = std::size_t;

enum class FirUnaryOperator {
    Negate,
    Not,
};

enum class FirBinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
};

enum class FirCallKind {
    Function,
    Print,
};

struct FirIntegerExpression {
    std::int32_t value{};
};

struct FirBooleanExpression {
    bool value{};
};

struct FirStringExpression {
    std::string value;
};

struct FirLocalExpression {
    FirLocalId local{};
};

struct FirUnaryExpression {
    FirUnaryOperator operation{FirUnaryOperator::Negate};
    FirExpressionId operand{};
};

struct FirBinaryExpression {
    FirExpressionId left{};
    FirBinaryOperator operation{FirBinaryOperator::Add};
    FirExpressionId right{};
};

struct FirCallExpression {
    FirCallKind kind{FirCallKind::Function};
    FirFunctionId function{};
    std::vector<FirExpressionId> arguments;
};

using FirExpressionValue =
    std::variant<FirIntegerExpression, FirBooleanExpression, FirStringExpression,
                 FirLocalExpression, FirUnaryExpression, FirBinaryExpression, FirCallExpression>;

struct FirExpression {
    FirExpressionValue value;
    TypeKind type{TypeKind::Invalid};
    SourceSpan span;
};

struct FirVariableStatement {
    FirLocalId local{};
    FirExpressionId initializer{};
};

struct FirAssignmentStatement {
    FirLocalId local{};
    FirExpressionId value{};
};

struct FirExpressionStatement {
    FirExpressionId expression{};
};

struct FirReturnStatement {
    std::optional<FirExpressionId> value;
};

struct FirIfStatement {
    FirExpressionId condition{};
    FirBlockId thenBlock{};
    std::optional<FirBlockId> elseBlock;
};

struct FirWhileStatement {
    FirExpressionId condition{};
    FirBlockId body{};
};

using FirStatementValue =
    std::variant<FirVariableStatement, FirAssignmentStatement, FirExpressionStatement,
                 FirReturnStatement, FirIfStatement, FirWhileStatement>;

struct FirStatement {
    FirStatementValue value;
    SourceSpan span;
};

struct FirBlock {
    std::vector<FirStatementId> statements;
};

struct FirLocal {
    std::string name;
    TypeKind type{TypeKind::Invalid};
    bool mutableBinding{};
};

struct FirFunction {
    std::string name;
    TypeKind returnType{TypeKind::Invalid};
    std::vector<FirLocalId> parameters;
    std::vector<FirLocal> locals;
    std::vector<FirExpression> expressions;
    std::vector<FirStatement> statements;
    std::vector<FirBlock> blocks;
    FirBlockId body{};
};

struct FirProgram {
    std::vector<FirFunction> functions;
    FirFunctionId main{};
};

} // namespace foundation

#endif
