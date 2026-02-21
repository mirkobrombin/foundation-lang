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
    Enum,
    Contract,
    Method,
    Field,
    Variant,
    Parameter,
};

enum class FirAttributeValueKind {
    Integer,
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
    Print,
    Panic,
    Len,
};

struct FirIntegerExpression {
    std::uint64_t magnitude{};
    bool negative{};
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
};

struct FirCallbackCallExpression {
    FirFunctionId function{};
    std::vector<FirExpressionId> arguments;
    std::vector<FirLocalId> argumentStorages;
    FirLocalId resultStorage{};
};

struct FirChannelExpression {
    Type payload{invalidType};
    FirExpressionId capacity{};
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
    FirVariantId variant{};
    std::optional<FirLocalId> binding;
    FirExpressionId expression{};
    std::vector<FirLocalId> drops;
};

struct FirMatchExpression {
    FirExpressionId value{};
    Type type{invalidType};
    std::vector<FirMatchArm> arms;
};

using FirExpressionValue =
    std::variant<FirIntegerExpression, FirBooleanExpression, FirStringExpression,
                 FirArrayExpression, FirLocalExpression, FirMoveExpression, FirUnaryExpression,
                 FirFunctionValueExpression, FirClosureExpression, FirOwnershipExpression,
                 FirBinaryExpression, FirCallExpression,
                 FirContractExpression, FirStructExpression, FirFieldExpression,
                 FirIndexExpression, FirReplaceExpression, FirEnumExpression,
                 FirSpawnExpression, FirTaskWaitExpression, FirBlockingCallExpression,
                 FirCallbackCallExpression,
                 FirChannelExpression,
                 FirChannelSendExpression, FirChannelReceiveExpression,
                 FirMatchExpression>;

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

struct FirAssignmentStatement {
    FirExpressionId target{};
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
    std::variant<FirVariableStatement, FirLetElseStatement, FirStructDestructureStatement,
                 FirAssignmentStatement, FirExpressionStatement, FirDiscardStatement,
                 FirReturnStatement, FirIfStatement, FirWhileStatement,
                 FirSelectStatement>;

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
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
    bool task{};
    bool blocking{};
    bool callback{};
    std::optional<std::string> callbackCancelSymbol;
};

struct FirStructField {
    std::string name;
    Type type{invalidType};
    bool exported{};
    std::vector<FirAttributeUse> attributes;
};

struct FirStruct {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirStructField> fields;
    bool exported{};
    std::optional<FirFunctionId> dropFunction;
    std::vector<FirAttributeUse> attributes;
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
};

struct FirContractMethod {
    FirReceiverKind receiver{FirReceiverKind::View};
    std::string name;
    Type returnType{invalidType};
    std::vector<Type> parameters;
    std::vector<std::string> parameterNames;
    bool exported{};
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
};

struct FirContract {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirContractMethod> methods;
    bool exported{};
    std::vector<FirAttributeUse> attributes;
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
