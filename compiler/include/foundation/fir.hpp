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
using FirStructId = std::size_t;
using FirFieldId = std::size_t;
using FirEnumId = std::size_t;
using FirVariantId = std::size_t;
using FirContractId = std::size_t;
using FirAttributeId = std::size_t;

enum class FirAttributeTarget {
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

enum class FirAttributeValueKind {
    Integer,
    Floating,
    Boolean,
    String,
    Enum,
    Array,
    Struct,
};

struct FirAttributeValue {
    FirAttributeValueKind kind{FirAttributeValueKind::Integer};
    Type type{invalidType};
    std::uint64_t magnitude{};
    bool negative{};
    bool boolean{};
    std::string text;
    FirVariantId variant{};
    std::vector<std::string> members;
    std::vector<FirAttributeValue> children;
};

struct FirAttributeArgument {
    std::string name;
    FirAttributeValue value;
};

struct FirAttributeUse {
    FirAttributeId declaration{};
    std::vector<FirAttributeArgument> arguments;
};

struct FirAttributeParameter {
    std::string name;
    Type type{invalidType};
};

struct FirAttributeDeclaration {
    std::string name;
    std::vector<FirAttributeParameter> parameters;
    std::vector<FirAttributeTarget> targets;
    bool repeatable{};
    bool exported{};
};

enum class FirCaptureMode {
    Copy,
    Own,
    View,
    Edit,
};

enum class FirUnaryOperator {
    Negate,
    Not,
    BitwiseNot,
    Empty,
    Dereference,
};

enum class FirOwnershipOperator {
    Own,
    View,
    Edit,
};

enum class FirReceiverKind {
    View,
    Edit,
    Own,
};

enum class FirBinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    ShiftLeft,
    ShiftRight,
    BitwiseAnd,
    BitwiseXor,
    BitwiseOr,
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
    FunctionValue,
    Contract,
    Constrained,
    Print,
    Panic,
    Len,
    Null,
    IsNull,
    CString,
    SizeOf,
    PointerCast,
    NumericConversion,
};

struct FirIntegerExpression {
    std::uint64_t magnitude{};
    bool negative{};
};

struct FirFloatingExpression {
    std::string text;
};

struct FirBooleanExpression {
    bool value{};
};

struct FirStringExpression {
    std::string value;
};

struct FirArrayExpression {
    std::vector<FirExpressionId> elements;
};

struct FirLocalExpression {
    FirLocalId local{};
};

struct FirReadExpression {
    FirLocalId local{};
};

struct FirMoveExpression {
    FirLocalId local{};
};

struct FirFunctionValueExpression {
    FirFunctionId function{};
    std::vector<Type> typeArguments;
};

struct FirClosureCapture {
    FirLocalId local{};
    FirCaptureMode mode{FirCaptureMode::Copy};
};

struct FirClosureExpression {
    FirFunctionId function{};
    std::vector<FirClosureCapture> captures;
};

struct FirUnaryExpression {
    FirUnaryOperator operation{FirUnaryOperator::Negate};
    FirExpressionId operand{};
};

struct FirOwnershipExpression {
    FirOwnershipOperator operation{FirOwnershipOperator::Own};
    FirExpressionId operand{};
    bool implicitRead{};
};

struct FirBinaryExpression {
    FirExpressionId left{};
    FirBinaryOperator operation{FirBinaryOperator::Add};
    FirExpressionId right{};
};

struct FirCallExpression {
    FirCallKind kind{FirCallKind::Function};
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    std::vector<FirExpressionId> arguments;
    std::vector<bool> argumentDrops;
    FirContractId contract{};
    std::size_t method{};
    FirLocalId local{};
    std::vector<std::size_t> argumentParameters;
    Type constrainedType{invalidType};
};

struct FirContractMethodTarget {
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    bool contractDefault{};
    Type defaultContract{invalidType};
    std::vector<FirFieldId> delegatePath;
};

struct FirContractExpression {
    FirExpressionId value{};
    Type concreteType{invalidType};
    Type contractType{invalidType};
    std::vector<FirContractMethodTarget> methods;
};

struct FirStructFieldValue {
    FirFieldId field{};
    FirExpressionId value{};
};

struct FirStructExpression {
    Type type{invalidType};
    std::vector<FirStructFieldValue> fields;
};

struct FirFieldExpression {
    FirExpressionId base{};
    FirFieldId field{};
};

struct FirIndexExpression {
    FirExpressionId base{};
    FirExpressionId index{};
};

struct FirRawPointerExpression {
    FirExpressionId base{};
};

struct FirReplaceExpression {
    FirExpressionId target{};
    FirExpressionId value{};
};

struct FirSpawnExpression {
    FirExpressionId call{};
};

struct FirTaskWaitExpression {
    FirExpressionId task{};
};

struct FirBlockingCallExpression {
    FirFunctionId function{};
    std::vector<FirExpressionId> arguments;
    std::vector<FirLocalId> argumentStorages;
    std::optional<FirLocalId> resultStorage;
    std::vector<std::size_t> argumentParameters;
};

struct FirCallbackCallExpression {
    FirFunctionId function{};
    std::vector<FirExpressionId> arguments;
    std::vector<FirLocalId> argumentStorages;
    FirLocalId resultStorage{};
    std::vector<std::size_t> argumentParameters;
};

struct FirChannelExpression {
    Type payload{invalidType};
    FirExpressionId capacity{};
};

struct FirChannelSenderCloneExpression {
    FirExpressionId sender{};
};

struct FirChannelSendExpression {
    FirLocalId sender{};
    std::optional<FirExpressionId> value;
    std::optional<FirLocalId> valueStorage;
    FirLocalId resultStorage{};
};

struct FirChannelReceiveExpression {
    FirLocalId receiver{};
    FirLocalId resultStorage{};
};

struct FirEnumExpression {
    Type type{invalidType};
    FirVariantId variant{};
    std::optional<FirExpressionId> payload;
};

struct FirMatchArm {
    bool wildcard{};
    FirVariantId variant{};
    std::optional<FirLocalId> binding;
    std::optional<FirLocalId> guardBinding;
    std::optional<FirExpressionId> pattern;
    std::optional<FirExpressionId> guard;
    FirBlockId block{};
    std::optional<FirExpressionId> expression;
    std::vector<FirLocalId> drops;
};

struct FirMatchExpression {
    FirExpressionId value{};
    Type type{invalidType};
    std::vector<FirMatchArm> arms;
    std::optional<FirLocalId> valueStorage;
};

struct FirConditionalExpression {
    FirExpressionId condition{};
    FirBlockId thenBlock{};
    FirExpressionId thenValue{};
    FirBlockId elseBlock{};
    FirExpressionId elseValue{};
};

using FirExpressionValue =
    std::variant<FirIntegerExpression, FirFloatingExpression, FirBooleanExpression,
                 FirStringExpression,
                 FirArrayExpression, FirLocalExpression, FirReadExpression, FirMoveExpression,
                 FirUnaryExpression,
                 FirFunctionValueExpression, FirClosureExpression, FirOwnershipExpression,
                 FirBinaryExpression, FirCallExpression,
                 FirContractExpression, FirStructExpression, FirFieldExpression,
                 FirIndexExpression, FirRawPointerExpression, FirReplaceExpression,
                 FirEnumExpression,
                 FirSpawnExpression, FirTaskWaitExpression, FirBlockingCallExpression,
                 FirCallbackCallExpression,
                 FirChannelExpression,
                 FirChannelSenderCloneExpression,
                 FirChannelSendExpression, FirChannelReceiveExpression,
                 FirMatchExpression, FirConditionalExpression>;

struct FirExpression {
    FirExpressionValue value;
    Type type{invalidType};
    SourceSpan span;
};

struct FirVariableStatement {
    FirLocalId local{};
    FirExpressionId initializer{};
};

struct FirStructBinding {
    FirFieldId field{};
    FirLocalId local{};
};

struct FirStructDestructureStatement {
    FirExpressionId initializer{};
    Type type{invalidType};
    bool owned{};
    std::vector<FirStructBinding> bindings;
};

struct FirLetElseStatement {
    FirLocalId local{};
    FirExpressionId initializer{};
    FirLocalId errorLocal{};
    FirBlockId elseBlock{};
};

struct FirResultElseStatement {
    FirExpressionId expression{};
    FirLocalId errorLocal{};
    FirBlockId elseBlock{};
};

enum class FirAssignmentOperator {
    Assign,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    ShiftLeft,
    ShiftRight,
};

struct FirAssignmentStatement {
    FirExpressionId target{};
    FirAssignmentOperator operation{FirAssignmentOperator::Assign};
    FirExpressionId value{};
};

struct FirExpressionStatement {
    FirExpressionId expression{};
};

struct FirDiscardStatement {
    FirExpressionId expression{};
};

struct FirReturnStatement {
    std::optional<FirExpressionId> value;
    std::vector<FirLocalId> drops;
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

struct FirForStatement {
    FirExpressionId sequence{};
    FirLocalId sequenceStorage{};
    FirLocalId index{};
    FirLocalId value{};
    FirBlockId body{};
    std::optional<FirExpressionId> next;
    bool ownsSequence{};
};

struct FirBreakStatement {
    std::vector<FirLocalId> drops;
};

struct FirContinueStatement {
    std::vector<FirLocalId> drops;
};

struct FirUnsafeStatement {
    FirBlockId body{};
};

struct FirSelectOperationArm {
    bool send{};
    FirLocalId endpoint{};
    std::optional<FirExpressionId> value;
    std::optional<FirLocalId> valueStorage;
    FirLocalId resultStorage{};
    std::optional<FirLocalId> binding;
    FirBlockId body{};
    SourceSpan span;
};

struct FirSelectTimeoutArm {
    std::uint64_t nanoseconds{};
    std::optional<FirExpressionId> duration;
    FirBlockId body{};
};

struct FirSelectStatement {
    std::vector<FirSelectOperationArm> operations;
    std::optional<FirSelectTimeoutArm> timeout;
    FirLocalId errorLocal{};
    FirBlockId errorBlock{};
    FirLocalId deadlineStorage{};
};

using FirStatementValue =
    std::variant<FirVariableStatement, FirLetElseStatement, FirResultElseStatement,
                 FirStructDestructureStatement,
                 FirAssignmentStatement, FirExpressionStatement, FirDiscardStatement,
                 FirReturnStatement, FirIfStatement, FirWhileStatement,
                 FirForStatement, FirBreakStatement, FirContinueStatement,
                 FirSelectStatement, FirUnsafeStatement>;

struct FirStatement {
    FirStatementValue value;
    SourceSpan span;
};

struct FirBlock {
    std::vector<FirStatementId> statements;
    std::vector<FirLocalId> drops;
};

struct FirLocal {
    std::string name;
    Type type{invalidType};
    bool mutableBinding{};
    bool capture{};
    FirCaptureMode captureMode{FirCaptureMode::Copy};
    bool borrowedClosure{};
};

struct FirStateTransitionFunction {
    std::vector<FirVariantId> sourceVariants;
    FirVariantId destinationVariant{};
    std::optional<FirLocalId> destinationParameter;
    std::optional<std::uint64_t> timeoutNanoseconds;
};

struct FirStateTimeoutFunction {
    std::vector<FirVariantId> sourceVariants;
    std::uint64_t nanoseconds{};
};

enum class FirWorkflowKind {
    Pipeline,
    Saga,
};

struct FirWorkflowStep {
    std::string name;
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    std::size_t attempts{1};
    std::optional<FirFunctionId> compensation;
    std::vector<Type> compensationTypeArguments;
};

struct FirWorkflowFunction {
    FirWorkflowKind kind{FirWorkflowKind::Pipeline};
    Type inputType{invalidType};
    Type successType{invalidType};
    Type errorType{invalidType};
    Type failureType{invalidType};
    Type failureDetailsType{invalidType};
    std::vector<FirWorkflowStep> steps;
};

struct FirFunction {
    std::string name;
    std::string packageName;
    std::string sourcePath;
    FirFunctionId source{};
    SourceSpan sourceSpan;
    bool generic{};
    std::size_t typeParameterCount{};
    Type returnType{invalidType};
    std::vector<FirLocalId> parameters;
    std::vector<bool> readParameters;
    std::vector<FirLocal> locals;
    std::vector<FirExpression> expressions;
    std::vector<FirStatement> statements;
    std::vector<FirBlock> blocks;
    FirBlockId body{};
    bool exported{};
    bool diverges{};
    std::optional<std::string> cSymbol;
    bool hasBody{true};
    bool closure{};
    bool method{};
    std::optional<FirReceiverKind> receiver;
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
    bool task{};
    bool blocking{};
    bool callback{};
    std::optional<std::string> callbackCancelSymbol;
    std::optional<std::string> testName;
    bool action{};
    std::optional<FirStateTransitionFunction> stateTransition;
    std::optional<FirWorkflowFunction> workflow;
    std::optional<FirStateTimeoutFunction> stateTimeout;
    bool constructor{};
    std::vector<std::optional<Type>> typeParameterConstraints;
};

struct FirStructField {
    std::string name;
    Type type{invalidType};
    bool exported{};
    std::vector<FirAttributeUse> attributes;
    bool hasDefault{};
};

struct FirStruct {
    std::string name;
    std::string sourcePath;
    SourceSpan sourceSpan;
    std::size_t typeParameterCount{};
    std::vector<FirStructField> fields;
    std::vector<Type> implementations;
    bool exported{};
    std::optional<FirFunctionId> dropFunction;
    std::vector<FirAttributeUse> attributes;
    bool service{};
    std::vector<std::optional<FirFieldId>> implementationDelegates;
};

struct FirEnumVariant {
    std::string name;
    std::optional<Type> payload;
    bool exported{};
    std::vector<FirAttributeUse> attributes;
};

struct FirEnum {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirEnumVariant> variants;
    bool exported{};
    bool builtin{};
    std::vector<FirAttributeUse> attributes;
    bool stateMachine{};
};

struct FirContractMethod {
    FirReceiverKind receiver{FirReceiverKind::View};
    std::string name;
    Type returnType{invalidType};
    std::vector<Type> parameters;
    std::vector<bool> readParameters;
    std::vector<std::string> parameterNames;
    bool exported{};
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
    FirContractId originContract{};
    std::vector<Type> originArguments;
    std::optional<FirFunctionId> defaultFunction;
};

struct FirContract {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirContractMethod> methods;
    bool exported{};
    std::vector<FirAttributeUse> attributes;
    std::vector<Type> parents;
};

struct FirProgram {
    std::vector<FirStruct> structs;
    std::vector<FirEnum> enums;
    std::vector<FirContract> contracts;
    std::vector<FirAttributeDeclaration> attributeDeclarations;
    std::vector<FirFunction> functions;
    FirFunctionId main{};
};

} // namespace foundation

#endif
