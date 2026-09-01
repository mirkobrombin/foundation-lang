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
    ConstrainedMethod,
    Print,
    Panic,
    Len,
    Null,
    IsNull,
    CString,
    SizeOf,
    PointerCast,
    Channel,
    NumericConversion,
};

struct CallTarget {
    CallTargetKind kind{CallTargetKind::Function};
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    std::vector<bool> argumentDrops;
    std::optional<AstExpressionId> receiver;
    Type receiverType{invalidType};
    Type constrainedType{invalidType};
    std::size_t contract{};
    std::size_t method{};
    FirLocalId local{};
    struct ContractMethodTarget {
        FirFunctionId function{};
        std::vector<Type> typeArguments;
        bool contractDefault{};
        Type defaultContract{invalidType};
        std::vector<FirFieldId> delegatePath;
    };
    struct ContractConversion {
        Type sourceType{invalidType};
        Type concreteType{invalidType};
        Type contractType{invalidType};
        Type targetType{invalidType};
        std::vector<ContractMethodTarget> methods;
    };
    std::optional<ContractConversion> receiverConversion;
    std::vector<std::optional<Type>> argumentBorrows;
    std::vector<std::optional<ContractConversion>> argumentConversions;
    std::vector<std::size_t> argumentParameters;
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
    bool readBinding{};
};

struct SemanticWorkflowStep {
    std::string name;
    FirFunctionId function{};
    std::vector<Type> typeArguments;
    std::size_t attempts{1};
    std::optional<FirFunctionId> compensation;
    std::vector<Type> compensationTypeArguments;
};

struct SemanticWorkflowFunction {
    WorkflowKind kind{WorkflowKind::Pipeline};
    Type inputType{invalidType};
    Type successType{invalidType};
    Type errorType{invalidType};
    Type failureType{invalidType};
    Type failureDetailsType{invalidType};
    std::vector<SemanticWorkflowStep> steps;
};

struct SemanticFunction {
    std::size_t typeParameterCount{};
    std::vector<std::optional<Type>> typeParameterConstraints;
    Type returnType{invalidType};
    std::vector<Type> parameterTypes;
    std::vector<FirLocalId> parameters;
    std::vector<SemanticLocal> locals;
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
    std::optional<std::string> callbackCancelSymbol;
    std::optional<SemanticWorkflowFunction> workflow;
};

struct SemanticStruct {
    std::size_t typeParameterCount{};
    std::vector<Type> fieldTypes;
    std::vector<Type> implementations;
    std::vector<std::optional<FirFieldId>> implementationDelegates;
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> fieldAttributes;
};

struct SemanticEnum {
    std::size_t typeParameterCount{};
    std::vector<std::optional<Type>> payloadTypes;
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> variantAttributes;
};

struct SemanticContractMethod {
    std::string name;
    ReceiverKind receiver{ReceiverKind::View};
    Type returnType{invalidType};
    std::vector<Type> parameterTypes;
    std::vector<std::string> parameterNames;
    std::vector<ParameterMode> parameterModes;
    bool exported{};
    SourceSpan span;
    std::size_t originContract{};
    std::vector<Type> originArguments;
    std::optional<AstFunctionId> defaultFunction;
    std::vector<FirAttributeUse> attributes;
    std::vector<std::vector<FirAttributeUse>> parameterAttributes;
};

struct SemanticContract {
    std::size_t typeParameterCount{};
    std::vector<Type> parents;
    std::vector<SemanticContractMethod> methods;
    std::vector<FirAttributeUse> attributes;
};

struct StructLiteralTarget {
    struct DefaultField {
        FirFieldId field{};
        FirFunctionId function{};
    };

    Type type{invalidType};
    std::vector<FirFieldId> fields;
    std::vector<DefaultField> defaults;
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
    std::vector<std::optional<FirLocalId>> guardBindings;
    std::vector<std::vector<FirLocalId>> drops;
};

struct OwnershipTarget {
    OwnershipOperator operation{OwnershipOperator::Own};
    std::optional<FirLocalId> local;
};

struct TaskWaitTarget {
    AstExpressionId task{};
};

struct BlockingCallTarget {
    FirFunctionId function{};
    std::vector<FirLocalId> argumentStorages;
    std::optional<FirLocalId> resultStorage;
};

struct CallbackCallTarget {
    FirFunctionId function{};
    std::vector<FirLocalId> argumentStorages;
    FirLocalId resultStorage{};
};

enum class ChannelOperationKind {
    Send,
    Receive,
};

struct ChannelOperationTarget {
    ChannelOperationKind kind{ChannelOperationKind::Send};
    FirLocalId endpoint{};
    std::optional<AstExpressionId> value;
    std::optional<FirLocalId> valueStorage;
    FirLocalId resultStorage{};
};

struct SelectTarget {
    std::vector<std::optional<FirLocalId>> bindings;
    FirLocalId errorLocal{};
    FirLocalId deadlineStorage{};
};

struct ForTarget {
    FirLocalId sequenceStorage{};
    FirLocalId index{};
    FirLocalId value{};
    Type sequenceType{invalidType};
    bool editable{};
    bool iterator{};
    FirFunctionId nextFunction{};
    std::vector<Type> nextTypeArguments;
    Type nextResultType{invalidType};
    bool ownsSequence{};
};

struct SemanticModel {
    std::vector<Type> expressionTypes;
    std::vector<bool> expressionReads;
    std::vector<std::optional<CallTarget::ContractConversion>> expressionContractConversions;
    std::vector<std::optional<FirLocalId>> expressionLocals;
    std::vector<std::optional<CallTarget>> callTargets;
    std::vector<bool> emptyTests;
    std::vector<std::optional<StructLiteralTarget>> structTargets;
    std::vector<std::optional<FirFieldId>> expressionFields;
    std::vector<std::optional<EnumTarget>> enumTargets;
    std::vector<std::optional<MatchTarget>> matchTargets;
    std::vector<std::optional<OwnershipTarget>> ownershipTargets;
    std::vector<std::optional<FunctionValueTarget>> functionValueTargets;
    std::vector<std::optional<ClosureTarget>> closureTargets;
    std::vector<std::optional<TaskWaitTarget>> taskWaitTargets;
    std::vector<std::optional<BlockingCallTarget>> blockingCallTargets;
    std::vector<std::optional<CallbackCallTarget>> callbackCallTargets;
    std::vector<std::optional<ChannelOperationTarget>> channelOperationTargets;
    std::vector<bool> channelSenderClones;
    std::vector<std::optional<SelectTarget>> selectTargets;
    std::vector<std::optional<ForTarget>> forTargets;
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
    std::vector<FirAttributeDeclaration> attributeDeclarations;
    std::vector<SemanticFunction> functions;
    FirFunctionId main{};
};

struct AnalyzeOptions {
    bool requireMain{true};
    bool retainInvalidModel{};
};

[[nodiscard]] std::optional<SemanticModel> analyze(const Program &program,
                                                   Diagnostics &diagnostics,
                                                   AnalyzeOptions options = {});

} // namespace foundation

#endif
