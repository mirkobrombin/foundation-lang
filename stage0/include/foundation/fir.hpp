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
    Contract,
    Print,
    Panic,
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

struct FirArrayExpression {
    std::vector<FirExpressionId> elements;
};

struct FirLocalExpression {
    FirLocalId local{};
};

struct FirMoveExpression {
    FirLocalId local{};
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
};

struct FirContractExpression {
    FirExpressionId value{};
    Type concreteType{invalidType};
    Type contractType{invalidType};
    std::vector<FirFunctionId> methods;
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
                 FirOwnershipExpression, FirBinaryExpression, FirCallExpression,
                 FirContractExpression, FirStructExpression, FirFieldExpression,
                 FirIndexExpression, FirEnumExpression, FirMatchExpression>;

struct FirExpression {
    FirExpressionValue value;
    Type type{invalidType};
    SourceSpan span;
};

struct FirVariableStatement {
    FirLocalId local{};
    FirExpressionId initializer{};
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

using FirStatementValue =
    std::variant<FirVariableStatement, FirLetElseStatement, FirAssignmentStatement,
                 FirExpressionStatement, FirDiscardStatement, FirReturnStatement, FirIfStatement,
                 FirWhileStatement>;

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
};

struct FirStructField {
    std::string name;
    Type type{invalidType};
    bool exported{};
};

struct FirStruct {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirStructField> fields;
    bool exported{};
};

struct FirEnumVariant {
    std::string name;
    std::optional<Type> payload;
    bool exported{};
};

struct FirEnum {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirEnumVariant> variants;
    bool exported{};
    bool builtin{};
};

struct FirContractMethod {
    FirReceiverKind receiver{FirReceiverKind::View};
    std::string name;
    Type returnType{invalidType};
    std::vector<Type> parameters;
    bool exported{};
};

struct FirContract {
    std::string name;
    std::size_t typeParameterCount{};
    std::vector<FirContractMethod> methods;
    bool exported{};
};

struct FirProgram {
    std::vector<FirStruct> structs;
    std::vector<FirEnum> enums;
    std::vector<FirContract> contracts;
    std::vector<FirFunction> functions;
    FirFunctionId main{};
};

} // namespace foundation

#endif
