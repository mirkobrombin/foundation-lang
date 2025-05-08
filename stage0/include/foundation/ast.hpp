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
using AstFunctionId = std::size_t;

struct TypeSyntax {
    std::string name;
    std::vector<TypeSyntax> arguments;
    SourceSpan span;
    std::size_t arrayLength{};
};

enum class UnaryOperator {
    Negate,
    Not,
};

enum class OwnershipOperator {
    Own,
    View,
    Edit,
};

enum class CaptureMode {
    Copy,
    Own,
    View,
    Edit,
};

enum class ReceiverKind {
    View,
    Edit,
    Own,
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

struct ArrayExpression {
    std::vector<AstExpressionId> elements;
};

struct NameExpression {
    std::string name;
    std::vector<TypeSyntax> typeArguments;
};

struct UnaryExpression {
    UnaryOperator operation{UnaryOperator::Negate};
    AstExpressionId operand{};
};

struct OwnershipExpression {
    OwnershipOperator operation{OwnershipOperator::Own};
    AstExpressionId operand{};
};

struct BinaryExpression {
    AstExpressionId left{};
    BinaryOperator operation{BinaryOperator::Add};
    AstExpressionId right{};
};

struct CallExpression {
    std::string callee;
    std::vector<TypeSyntax> typeArguments;
    std::vector<AstExpressionId> arguments;
};

struct StructFieldInitializer {
    std::string name;
    AstExpressionId value{};
    SourceSpan span;
};

struct StructExpression {
    TypeSyntax type;
    std::vector<StructFieldInitializer> fields;
};

struct MemberExpression {
    std::optional<AstExpressionId> base;
    std::string member;
    std::vector<TypeSyntax> typeArguments;
    bool invoked{};
    std::vector<AstExpressionId> arguments;
};

struct IndexExpression {
    AstExpressionId base{};
    AstExpressionId index{};
};

struct MatchArm {
    std::string variant;
    std::optional<std::string> binding;
    AstExpressionId expression{};
    SourceSpan span;
};

struct MatchExpression {
    AstExpressionId value{};
    std::vector<MatchArm> arms;
};

struct FunctionExpression {
    AstFunctionId function{};
};

using ExpressionValue =
    std::variant<IntegerExpression, BooleanExpression, StringExpression, ArrayExpression,
                 NameExpression, UnaryExpression, OwnershipExpression, BinaryExpression,
                 CallExpression, StructExpression, MemberExpression, IndexExpression,
                 MatchExpression, FunctionExpression>;

struct Expression {
    ExpressionValue value;
    SourceSpan span;
};

struct VariableStatement {
    bool mutableBinding{};
    std::string name;
    std::optional<TypeSyntax> type;
    AstExpressionId initializer{};
    std::optional<std::string> elseBinding;
    std::optional<AstBlockId> elseBlock;
};

struct AssignmentStatement {
    AstExpressionId target{};
    AstExpressionId value{};
};

struct ExpressionStatement {
    AstExpressionId expression{};
};

struct ReturnStatement {
    std::optional<AstExpressionId> value;
};

struct DiscardStatement {
    AstExpressionId value{};
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
                 DiscardStatement, IfStatement, WhileStatement>;

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
    TypeSyntax type;
    SourceSpan span;
};

struct Capture {
    CaptureMode mode{CaptureMode::Copy};
    std::string name;
    SourceSpan span;
};

struct Function {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<Parameter> parameters;
    TypeSyntax returnType;
    AstBlockId body{};
    bool exported{};
    SourceSpan span;
    std::string packageName;
    std::string sourcePath;
    std::optional<ReceiverKind> receiver;
    std::string ownerType;
    std::optional<std::string> cSymbol;
    bool hasBody{true};
    bool closure{};
    std::vector<Capture> captures;
};

struct StructField {
    std::string name;
    TypeSyntax type;
    bool exported{};
    SourceSpan span;
};

struct StructDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<TypeSyntax> implementations;
    std::vector<StructField> fields;
    bool exported{};
    SourceSpan span;
    std::string packageName;
};

struct ContractMethod {
    std::string name;
    ReceiverKind receiver{ReceiverKind::View};
    std::vector<Parameter> parameters;
    TypeSyntax returnType;
    bool exported{};
    SourceSpan span;
};

struct ContractDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<ContractMethod> methods;
    bool exported{};
    SourceSpan span;
    std::string packageName;
};

struct EnumVariant {
    std::string name;
    std::optional<TypeSyntax> payloadType;
    bool exported{};
    SourceSpan span;
};

enum class BuiltinEnumKind {
    None,
    Option,
    Result,
};

struct EnumDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<EnumVariant> variants;
    bool exported{};
    BuiltinEnumKind builtin{BuiltinEnumKind::None};
    SourceSpan span;
    std::string packageName;
};

struct ImportDeclaration {
    std::string packageName;
    std::string alias;
    SourceSpan span;
};

struct Program {
    std::string packageName;
    bool hasPackageDeclaration{};
    std::vector<ImportDeclaration> imports;
    std::vector<Expression> expressions;
    std::vector<Statement> statements;
    std::vector<Block> blocks;
    std::vector<StructDeclaration> structs;
    std::vector<EnumDeclaration> enums;
    std::vector<ContractDeclaration> contracts;
    std::vector<Function> functions;
};

} // namespace foundation

#endif
