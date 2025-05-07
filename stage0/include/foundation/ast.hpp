#ifndef FOUNDATION_AST_HPP
#define FOUNDATION_AST_HPP

#include "foundation/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace foundation {

using AstExpressionId = std::size_t;
using AstStatementId = std::size_t;
using AstBlockId = std::size_t;

enum class UnaryOperator {
    Negate,
    Not,
};

enum class BinaryOperator {
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

struct IntegerExpression {
    std::int64_t value{};
};

struct BooleanExpression {
    bool value{};
};

struct StringExpression {
    std::string value;
};

struct NameExpression {
    std::string name;
};

struct UnaryExpression {
    UnaryOperator operation{UnaryOperator::Negate};
    AstExpressionId operand{};
};

struct BinaryExpression {
    AstExpressionId left{};
    BinaryOperator operation{BinaryOperator::Add};
    AstExpressionId right{};
};

struct CallExpression {
    std::string callee;
    std::vector<AstExpressionId> arguments;
};

struct StructFieldInitializer {
    std::string name;
    AstExpressionId value{};
    SourceSpan span;
};

struct StructExpression {
    std::string typeName;
    std::vector<StructFieldInitializer> fields;
};

struct FieldExpression {
    AstExpressionId base{};
    std::string field;
};

using ExpressionValue =
    std::variant<IntegerExpression, BooleanExpression, StringExpression, NameExpression,
                 UnaryExpression, BinaryExpression, CallExpression, StructExpression,
                 FieldExpression>;

struct Expression {
    ExpressionValue value;
    SourceSpan span;
};

struct VariableStatement {
    bool mutableBinding{};
    std::string name;
    std::optional<std::string> typeName;
    AstExpressionId initializer{};
};

struct AssignmentStatement {
    std::string name;
    AstExpressionId value{};
};

struct ExpressionStatement {
    AstExpressionId expression{};
};

struct ReturnStatement {
    std::optional<AstExpressionId> value;
};

struct IfStatement {
    AstExpressionId condition{};
    AstBlockId thenBlock{};
    std::optional<AstBlockId> elseBlock;
};

struct WhileStatement {
    AstExpressionId condition{};
    AstBlockId body{};
};

using StatementValue =
    std::variant<VariableStatement, AssignmentStatement, ExpressionStatement, ReturnStatement,
                 IfStatement, WhileStatement>;

struct Statement {
    StatementValue value;
    SourceSpan span;
};

struct Block {
    std::vector<AstStatementId> statements;
    SourceSpan span;
};

struct Parameter {
    std::string name;
    std::string typeName;
    SourceSpan span;
};

struct Function {
    std::string name;
    std::vector<Parameter> parameters;
    std::string returnType;
    AstBlockId body{};
    SourceSpan span;
};

struct StructField {
    std::string name;
    std::string typeName;
    SourceSpan span;
};

struct StructDeclaration {
    std::string name;
    std::vector<StructField> fields;
    SourceSpan span;
};

struct Program {
    std::vector<Expression> expressions;
    std::vector<Statement> statements;
    std::vector<Block> blocks;
    std::vector<StructDeclaration> structs;
    std::vector<Function> functions;
};

} // namespace foundation

#endif
