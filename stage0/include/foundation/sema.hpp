#ifndef FOUNDATION_SEMA_HPP
#define FOUNDATION_SEMA_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"
#include "foundation/type.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace foundation {

enum class CallTargetKind {
    Function,
    FunctionValue,
    Method,
    ContractMethod,
    Print,
    Panic,
};

struct CallTarget {
    CallTargetKind kind{CallTargetKind::Function};
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    std::vector<bool> argumentDrops;
    std::optional<AstExpressionId> receiver;
    Type receiverType{invalidType};
    std::size_t contract{};
    std::size_t method{};
    FirLocalId local{};
    struct ContractConversion {
        Type concreteType{invalidType};
        Type contractType{invalidType};
        Type targetType{invalidType};
        std::vector<FirFunctionId> methods;
    };
    std::vector<std::optional<ContractConversion>> argumentConversions;
};

struct FunctionValueTarget {
    FirFunctionId function{};
    std::vector<Type> typeArguments;
};

struct ClosureTarget {
    FirFunctionId function{};
    std::vector<FirLocalId> captures;
    std::vector<CaptureMode> modes;
    bool borrowed{};
};

struct SemanticLocal {
    std::string name;
    Type type{invalidType};
    bool mutableBinding{};
    bool capture{};
    CaptureMode captureMode{CaptureMode::Copy};
    bool borrowedClosure{};
};

struct SemanticFunction {
    std::size_t typeParameterCount{};
    Type returnType{invalidType};
    std::vector<Type> parameterTypes;
    std::vector<FirLocalId> parameters;
    std::vector<SemanticLocal> locals;
};

struct SemanticStruct {
    std::size_t typeParameterCount{};
    std::vector<Type> fieldTypes;
    std::vector<Type> implementations;
};

struct SemanticEnum {
    std::size_t typeParameterCount{};
    std::vector<std::optional<Type>> payloadTypes;
};

struct SemanticContractMethod {
    ReceiverKind receiver{ReceiverKind::View};
    Type returnType{invalidType};
    std::vector<Type> parameterTypes;
};

struct SemanticContract {
    std::size_t typeParameterCount{};
    std::vector<SemanticContractMethod> methods;
};

struct StructLiteralTarget {
    Type type{invalidType};
    std::vector<FirFieldId> fields;
};

struct StructDestructureTarget {
    Type type{invalidType};
    bool owned{};
    std::vector<FirFieldId> fields;
    std::vector<FirLocalId> bindings;
};

struct EnumTarget {
    Type type{invalidType};
    FirVariantId variant{};
};

struct MatchTarget {
    Type type{invalidType};
    std::vector<FirVariantId> variants;
    std::vector<std::optional<FirLocalId>> bindings;
    std::vector<std::vector<FirLocalId>> drops;
};

struct OwnershipTarget {
    OwnershipOperator operation{OwnershipOperator::Own};
    std::optional<FirLocalId> local;
};

struct SemanticModel {
    std::vector<Type> expressionTypes;
    std::vector<std::optional<FirLocalId>> expressionLocals;
    std::vector<std::optional<CallTarget>> callTargets;
    std::vector<std::optional<StructLiteralTarget>> structTargets;
    std::vector<std::optional<FirFieldId>> expressionFields;
    std::vector<std::optional<EnumTarget>> enumTargets;
    std::vector<std::optional<MatchTarget>> matchTargets;
    std::vector<std::optional<OwnershipTarget>> ownershipTargets;
    std::vector<std::optional<FunctionValueTarget>> functionValueTargets;
    std::vector<std::optional<ClosureTarget>> closureTargets;
    std::vector<bool> expressionBorrowedClosures;
    std::vector<bool> expressionMoves;
    std::vector<std::optional<FirLocalId>> statementLocals;
    std::vector<std::optional<FirLocalId>> statementElseLocals;
    std::vector<std::optional<StructDestructureTarget>> statementStructTargets;
    std::vector<std::vector<FirLocalId>> statementDrops;
    std::vector<std::vector<FirLocalId>> blockDrops;
    std::vector<SemanticStruct> structs;
    std::vector<SemanticEnum> enums;
    std::vector<SemanticContract> contracts;
    std::vector<SemanticFunction> functions;
    FirFunctionId main{};
};

[[nodiscard]] std::optional<SemanticModel> analyze(const Program &program,
                                                   Diagnostics &diagnostics);

} // namespace foundation

#endif
