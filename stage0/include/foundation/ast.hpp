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

enum class AttributeTarget {
    Function,
    Struct,
    Service,
    Enum,
    Contract,
    Method,
    Constructor,
    Action,
    Field,
    Variant,
    Parameter,
};

struct AttributeArgument {
    std::optional<std::string> name;
    AstExpressionId value{};
    SourceSpan span;
};

struct AttributeApplication {
    std::string name;
    std::vector<AttributeArgument> arguments;
    SourceSpan span;
    bool parenthesized{true};
};

enum class UnaryOperator {
    Negate,
    Not,
    Dereference,
};

enum class OwnershipOperator {
    Own,
    View,
    Edit,
    Transfer,
    New,
};

enum class ParameterMode {
    Bootstrap,
    Read,
    Edit,
    Transfer,
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
    std::uint64_t magnitude{};
    bool negative{};
};

struct FloatingExpression {
    std::string text;
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
    std::vector<std::optional<std::string>> argumentNames;
    std::vector<std::optional<SourceSpan>> argumentNameSpans;
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
    std::vector<std::optional<std::string>> argumentNames;
    std::vector<std::optional<SourceSpan>> argumentNameSpans;
};

struct IndexExpression {
    AstExpressionId base{};
    AstExpressionId index{};
};

struct ReplaceExpression {
    AstExpressionId target{};
    AstExpressionId value{};
};

struct SpawnExpression {
    AstExpressionId call{};
};

struct MatchArm {
    bool wildcard{};
    std::string variant;
    std::optional<std::string> binding;
    std::optional<AstExpressionId> pattern;
    std::optional<AstExpressionId> guard;
    AstBlockId block{};
    std::optional<AstExpressionId> expression;
    SourceSpan span;
};

struct MatchExpression {
    AstExpressionId value{};
    std::vector<MatchArm> arms;
};

struct ConditionalExpression {
    AstExpressionId condition{};
    AstBlockId thenBlock{};
    AstExpressionId thenValue{};
    AstBlockId elseBlock{};
    AstExpressionId elseValue{};
};

struct FunctionExpression {
    AstFunctionId function{};
};

using ExpressionValue =
    std::variant<IntegerExpression, FloatingExpression, BooleanExpression, StringExpression,
                 ArrayExpression,
                 NameExpression, UnaryExpression, OwnershipExpression, BinaryExpression,
                 CallExpression, StructExpression, MemberExpression, IndexExpression,
                 ReplaceExpression, SpawnExpression, MatchExpression, ConditionalExpression,
                 FunctionExpression>;

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

struct StructPatternField {
    std::string field;
    std::string binding;
    SourceSpan span;
};

struct StructDestructureStatement {
    TypeSyntax type;
    std::vector<StructPatternField> fields;
    AstExpressionId initializer{};
};

struct AssignmentStatement {
    AstExpressionId target{};
    AstExpressionId value{};
};

struct ExpressionStatement {
    AstExpressionId expression{};
};

struct ResultElseStatement {
    AstExpressionId expression{};
    std::optional<std::string> errorBinding;
    AstBlockId elseBlock{};
};

struct ReturnStatement {
    std::optional<AstExpressionId> value;
    bool tail{};
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

struct ForStatement {
    std::optional<std::string> indexBinding;
    std::string valueBinding;
    bool editable{};
    AstExpressionId sequence{};
    AstBlockId body{};
};

struct BreakStatement {};

struct ContinueStatement {};

struct UnsafeStatement {
    AstBlockId body{};
    bool safetyProof{};
};

struct SelectOperationArm {
    std::optional<std::string> binding;
    AstExpressionId operation{};
    AstBlockId body{};
    SourceSpan span;
};

struct SelectTimeoutArm {
    std::uint64_t nanoseconds{};
    std::optional<AstExpressionId> duration;
    AstBlockId body{};
    SourceSpan span;
};

struct SelectStatement {
    std::vector<SelectOperationArm> operations;
    std::optional<SelectTimeoutArm> timeout;
    std::string errorBinding;
    AstBlockId errorBlock{};
};

using StatementValue =
    std::variant<VariableStatement, StructDestructureStatement, AssignmentStatement,
                 ExpressionStatement, ResultElseStatement, ReturnStatement, DiscardStatement,
                 IfStatement,
                 WhileStatement, ForStatement, BreakStatement, ContinueStatement,
                 SelectStatement, UnsafeStatement>;

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
    std::vector<AttributeApplication> attributes;
    ParameterMode mode{ParameterMode::Bootstrap};
    bool inferredType{};
};

struct Capture {
    CaptureMode mode{CaptureMode::Copy};
    std::string name;
    SourceSpan span;
};

struct StateTransitionFunction {
    std::vector<std::size_t> sourceVariants;
    std::size_t destinationVariant{};
    std::optional<std::size_t> destinationParameter;
    std::optional<std::uint64_t> timeoutNanoseconds;
};

struct StateTimeoutFunction {
    std::vector<std::size_t> sourceVariants;
    std::uint64_t nanoseconds{};
};

enum class WorkflowKind {
    Pipeline,
    Saga,
};

struct WorkflowStep {
    std::string name;
    std::string function;
    SourceSpan span;
    SourceSpan functionSpan;
    std::size_t attempts{1};
    std::optional<std::string> compensation;
    std::optional<SourceSpan> compensationSpan;
};

struct WorkflowFunction {
    WorkflowKind kind{WorkflowKind::Pipeline};
    TypeSyntax successType;
    TypeSyntax errorType;
    std::vector<WorkflowStep> steps;
    std::optional<std::string> failureStruct;
    std::optional<std::string> failureEnum;
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
    std::vector<AttributeApplication> attributes;
    bool task{};
    bool blocking{};
    bool callback{};
    std::optional<std::string> testName{};
    std::optional<SourceSpan> testNameSpan{};
    bool action{};
    std::optional<StateTransitionFunction> stateTransition;
    std::optional<WorkflowFunction> workflow;
    bool inferredReturn{};
    std::vector<bool> transferableTypeParameters;
    std::optional<StateTimeoutFunction> stateTimeout;
    bool constructor{};
};

enum class StructKind {
    Struct,
    Service,
};

struct StructField {
    std::string name;
    TypeSyntax type;
    bool exported{};
    SourceSpan span;
    std::vector<AttributeApplication> attributes;
    std::optional<AstFunctionId> defaultFunction;
    std::optional<SourceSpan> defaultSpan;
};

struct StructImplementation {
    TypeSyntax contract;
    std::optional<std::string> delegate;
    SourceSpan span;
};

struct StructDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<StructImplementation> implementations;
    std::vector<StructField> fields;
    bool exported{};
    SourceSpan span;
    std::string packageName;
    std::vector<AttributeApplication> attributes;
    StructKind kind{StructKind::Struct};
    std::string sourcePath;
};

struct ContractMethod {
    std::string name;
    ReceiverKind receiver{ReceiverKind::View};
    std::vector<Parameter> parameters;
    TypeSyntax returnType;
    bool exported{};
    SourceSpan span;
    std::optional<AstFunctionId> defaultFunction;
    std::vector<AttributeApplication> attributes;
};

struct ContractDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<TypeSyntax> parents;
    std::vector<ContractMethod> methods;
    bool exported{};
    SourceSpan span;
    std::string packageName;
    std::vector<AttributeApplication> attributes;
};

struct EnumVariant {
    std::string name;
    std::optional<TypeSyntax> payloadType;
    bool exported{};
    SourceSpan span;
    std::vector<AttributeApplication> attributes;
    std::optional<std::string> payloadName;
    std::optional<SourceSpan> payloadNameSpan;
};

enum class BuiltinEnumKind {
    None,
    Option,
    Result,
    ChannelError,
    NumberError,
};

struct EnumDeclaration {
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<EnumVariant> variants;
    bool exported{};
    BuiltinEnumKind builtin{BuiltinEnumKind::None};
    SourceSpan span;
    std::string packageName;
    std::vector<AttributeApplication> attributes;
    bool stateMachine{};
    bool generated{};
};

struct AttributeDeclaration {
    std::string name;
    std::vector<Parameter> parameters;
    std::vector<AttributeTarget> targets;
    bool repeatable{};
    bool exported{};
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
    std::vector<AttributeDeclaration> attributeDeclarations;
    std::vector<Function> functions;
};

} // namespace foundation

#endif
