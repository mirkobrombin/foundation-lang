#include "foundation/lower.hpp"

#include <exception>
#include <optional>
#include <unordered_map>
#include <utility>

namespace foundation {

namespace {

template <typename T> const T &required(const std::optional<T> &value) {
    if (!value.has_value()) {
        std::terminate();
    }
    return *value;
}

Type substituteType(Type type, const std::vector<Type> &arguments) {
    if (type.kind == TypeKind::Parameter && type.declaration < arguments.size()) {
        return arguments[type.declaration];
    }
    for (auto &argument : type.arguments) {
        argument = substituteType(std::move(argument), arguments);
    }
    return type;
}

FirUnaryOperator lowerUnary(UnaryOperator operation) {
    switch (operation) {
    case UnaryOperator::Negate:
        return FirUnaryOperator::Negate;
    case UnaryOperator::Not:
        return FirUnaryOperator::Not;
    case UnaryOperator::Dereference:
        return FirUnaryOperator::Dereference;
    }
    std::terminate();
}

FirOwnershipOperator lowerOwnership(OwnershipOperator operation) {
    switch (operation) {
    case OwnershipOperator::Own:
        return FirOwnershipOperator::Own;
    case OwnershipOperator::View:
        return FirOwnershipOperator::View;
    case OwnershipOperator::Edit:
        return FirOwnershipOperator::Edit;
    case OwnershipOperator::New:
    case OwnershipOperator::Transfer:
        break;
    }
    std::terminate();
}

FirReceiverKind lowerReceiver(ReceiverKind receiver) {
    switch (receiver) {
    case ReceiverKind::View:
        return FirReceiverKind::View;
    case ReceiverKind::Edit:
        return FirReceiverKind::Edit;
    case ReceiverKind::Own:
        return FirReceiverKind::Own;
    }
    std::terminate();
}

FirCaptureMode lowerCapture(CaptureMode mode) {
    switch (mode) {
    case CaptureMode::Copy:
        return FirCaptureMode::Copy;
    case CaptureMode::Own:
        return FirCaptureMode::Own;
    case CaptureMode::View:
        return FirCaptureMode::View;
    case CaptureMode::Edit:
        return FirCaptureMode::Edit;
    }
    std::terminate();
}

std::vector<FirContractMethodTarget> lowerContractMethods(
    const std::vector<CallTarget::ContractMethodTarget> &methods) {
    std::vector<FirContractMethodTarget> result;
    result.reserve(methods.size());
    for (const auto &method : methods) {
        result.push_back({method.function, method.typeArguments, method.contractDefault,
                          method.defaultContract, method.delegatePath});
    }
    return result;
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
        result.attributeDeclarations = model_.attributeDeclarations;
        result.structs.reserve(program_.structs.size());
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            FirStruct type;
            type.name = program_.structs[index].name;
            type.sourcePath = program_.structs[index].sourcePath;
            type.sourceSpan = program_.structs[index].span;
            type.typeParameterCount = program_.structs[index].typeParameters.size();
            type.exported = program_.structs[index].exported;
            type.service = program_.structs[index].kind == StructKind::Service;
            type.attributes = model_.structs[index].attributes;
            type.implementations = model_.structs[index].implementations;
            type.fields.reserve(program_.structs[index].fields.size());
            for (std::size_t field = 0; field < program_.structs[index].fields.size(); ++field) {
                type.fields.push_back({program_.structs[index].fields[field].name,
                                       model_.structs[index].fieldTypes[field],
                                       program_.structs[index].fields[field].exported,
                                       model_.structs[index].fieldAttributes[field],
                                       program_.structs[index].fields[field]
                                           .defaultFunction.has_value()});
            }
            result.structs.push_back(std::move(type));
        }
        result.enums.reserve(program_.enums.size());
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            FirEnum type;
            type.name = program_.enums[index].name;
            type.typeParameterCount = program_.enums[index].typeParameters.size();
            type.exported = program_.enums[index].exported;
            type.builtin = program_.enums[index].builtin != BuiltinEnumKind::None;
            type.attributes = model_.enums[index].attributes;
            type.stateMachine = program_.enums[index].stateMachine;
            type.variants.reserve(program_.enums[index].variants.size());
            for (std::size_t variant = 0; variant < program_.enums[index].variants.size();
                 ++variant) {
                type.variants.push_back({program_.enums[index].variants[variant].name,
                                         model_.enums[index].payloadTypes[variant],
                                         program_.enums[index].variants[variant].exported,
                                         model_.enums[index].variantAttributes[variant]});
            }
            result.enums.push_back(std::move(type));
        }
        result.contracts.reserve(program_.contracts.size());
        for (std::size_t index = 0; index < program_.contracts.size(); ++index) {
            FirContract type;
            type.name = program_.contracts[index].name;
            type.typeParameterCount = program_.contracts[index].typeParameters.size();
            type.exported = program_.contracts[index].exported;
            type.attributes = model_.contracts[index].attributes;
            for (const auto &semantic : model_.contracts[index].methods) {
                type.methods.push_back({lowerReceiver(semantic.receiver), semantic.name,
                                        semantic.returnType, semantic.parameterTypes,
                                        [&semantic] {
                                            std::vector<bool> reads;
                                            reads.reserve(semantic.parameterModes.size());
                                            for (const auto mode : semantic.parameterModes) {
                                                reads.push_back(mode == ParameterMode::Read);
                                            }
                                            return reads;
                                        }(),
                                        semantic.parameterNames,
                                        semantic.exported, semantic.attributes,
                                        semantic.parameterAttributes});
            }
            result.contracts.push_back(std::move(type));
        }
        result.functions.reserve(program_.functions.size());
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            result.functions.push_back(lowerFunction(index));
        }
        std::unordered_map<std::string, FirStructId> structs;
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            structs.emplace(program_.structs[index].name, index);
        }
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            const auto separator = function.name.rfind('.');
            const auto methodName = separator == std::string::npos
                                        ? function.name
                                        : function.name.substr(separator + 1);
            if (!function.receiver.has_value() || function.action || methodName != "drop") {
                continue;
            }
            const auto owner = structs.find(function.ownerType);
            if (owner != structs.end()) {
                result.structs[owner->second].dropFunction = index;
            }
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
        function.packageName = source.packageName;
        function.sourcePath = source.sourcePath;
        function.source = id;
        function.sourceSpan = source.span;
        function.generic = !source.typeParameters.empty();
        function.typeParameterCount = source.typeParameters.size();
        function.exported = source.exported;
        function.cSymbol = source.cSymbol;
        function.hasBody = source.hasBody;
        function.closure = source.closure;
        function.method = !source.ownerType.empty();
        if (source.receiver.has_value()) {
            function.receiver = lowerReceiver(*source.receiver);
        }
        function.action = source.action;
        if (source.stateTransition.has_value()) {
            FirStateTransitionFunction transition;
            transition.sourceVariants = source.stateTransition->sourceVariants;
            transition.destinationVariant = source.stateTransition->destinationVariant;
            if (source.stateTransition->destinationParameter.has_value()) {
                const auto parameter = *source.stateTransition->destinationParameter;
                if (parameter < semantic.parameters.size()) {
                    transition.destinationParameter = semantic.parameters[parameter];
                }
            }
            function.stateTransition = std::move(transition);
        }
        if (semantic.workflow.has_value()) {
            const auto &sourceWorkflow = *semantic.workflow;
            FirWorkflowFunction workflow;
            workflow.kind = sourceWorkflow.kind == WorkflowKind::Pipeline
                                ? FirWorkflowKind::Pipeline
                                : FirWorkflowKind::Saga;
            workflow.inputType = sourceWorkflow.inputType;
            workflow.successType = sourceWorkflow.successType;
            workflow.errorType = sourceWorkflow.errorType;
            workflow.failureType = sourceWorkflow.failureType;
            workflow.failureDetailsType = sourceWorkflow.failureDetailsType;
            for (const auto &sourceStep : sourceWorkflow.steps) {
                workflow.steps.push_back(
                    {sourceStep.name, sourceStep.function, sourceStep.typeArguments,
                     sourceStep.attempts, sourceStep.compensation,
                     sourceStep.compensationTypeArguments});
            }
            function.workflow = std::move(workflow);
        }
        function.task = source.task;
        function.blocking = source.blocking;
        function.callback = source.callback;
        function.callbackCancelSymbol = semantic.callbackCancelSymbol;
        function.testName = source.testName;
        function.attributes = semantic.attributes;
        function.parameterAttributes = semantic.parameterAttributes;
        function.returnType = semantic.returnType;
        function.parameters = semantic.parameters;
        function.readParameters.reserve(source.parameters.size());
        for (const auto &parameter : source.parameters) {
            function.readParameters.push_back(parameter.mode == ParameterMode::Read);
        }
        function.locals.reserve(semantic.locals.size());
        for (const auto &local : semantic.locals) {
            function.locals.push_back({local.name, local.type, local.mutableBinding,
                                       local.capture, lowerCapture(local.captureMode),
                                       local.borrowedClosure});
        }
        if (!source.hasBody || source.stateTransition.has_value() ||
            source.workflow.has_value()) {
            return function;
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
        current_->blocks[lowered].drops = model_.blockDrops[id];
        return lowered;
    }

    FirStatementId lowerStatement(AstStatementId id) {
        if (statementMap_[id].has_value()) {
            return *statementMap_[id];
        }

        const auto &source = program_.statements[id];
        FirStatementValue value;
        if (const auto *variable = std::get_if<VariableStatement>(&source.value)) {
            if (variable->elseBlock.has_value()) {
                value = FirLetElseStatement{required(model_.statementLocals[id]),
                                            lowerExpression(variable->initializer),
                                            required(model_.statementElseLocals[id]),
                                            lowerBlock(*variable->elseBlock)};
            } else {
                value = FirVariableStatement{required(model_.statementLocals[id]),
                                             lowerExpression(variable->initializer)};
            }
        } else if (const auto *resultElse =
                       std::get_if<ResultElseStatement>(&source.value)) {
            value = FirResultElseStatement{lowerExpression(resultElse->expression),
                                           required(model_.statementElseLocals[id]),
                                           lowerBlock(resultElse->elseBlock)};
        } else if (const auto *destructure =
                       std::get_if<StructDestructureStatement>(&source.value)) {
            const auto &target = required(model_.statementStructTargets[id]);
            std::vector<FirStructBinding> bindings;
            bindings.reserve(target.fields.size());
            for (std::size_t index = 0; index < target.fields.size(); ++index) {
                bindings.push_back({target.fields[index], target.bindings[index]});
            }
            value = FirStructDestructureStatement{lowerExpression(destructure->initializer),
                                                  target.type, target.owned,
                                                  std::move(bindings)};
        } else if (const auto *assignment = std::get_if<AssignmentStatement>(&source.value)) {
            value = FirAssignmentStatement{lowerExpression(assignment->target),
                                           lowerExpression(assignment->value)};
        } else if (const auto *expression = std::get_if<ExpressionStatement>(&source.value)) {
            value = FirExpressionStatement{lowerExpression(expression->expression)};
        } else if (const auto *returned = std::get_if<ReturnStatement>(&source.value)) {
            std::optional<FirExpressionId> result;
            if (returned->value.has_value()) {
                result = lowerExpression(*returned->value);
            }
            value = FirReturnStatement{result, model_.statementDrops[id]};
        } else if (const auto *discarded = std::get_if<DiscardStatement>(&source.value)) {
            value = FirDiscardStatement{lowerExpression(discarded->value)};
        } else if (std::holds_alternative<BreakStatement>(source.value)) {
            value = FirBreakStatement{model_.statementDrops[id]};
        } else if (std::holds_alternative<ContinueStatement>(source.value)) {
            value = FirContinueStatement{model_.statementDrops[id]};
        } else if (const auto *branch = std::get_if<IfStatement>(&source.value)) {
            const auto condition = lowerExpression(branch->condition);
            const auto thenBlock = lowerBlock(branch->thenBlock);
            std::optional<FirBlockId> elseBlock;
            if (branch->elseBlock.has_value()) {
                elseBlock = lowerBlock(*branch->elseBlock);
            }
            value = FirIfStatement{condition, thenBlock, elseBlock};
        } else if (const auto *loop = std::get_if<ForStatement>(&source.value)) {
            const auto &target = required(model_.forTargets[id]);
            auto sequence = lowerExpression(loop->sequence);
            if (!target.iterator || !target.ownsSequence) {
                const auto borrowed = current_->expressions.size();
                current_->expressions.push_back(
                    {FirOwnershipExpression{
                         target.iterator || target.editable ? FirOwnershipOperator::Edit
                                                            : FirOwnershipOperator::View,
                         sequence},
                     target.sequenceType, source.span});
                sequence = borrowed;
            }

            std::optional<FirExpressionId> next;
            if (target.iterator) {
                auto receiver = current_->expressions.size();
                current_->expressions.push_back(
                    {FirLocalExpression{target.sequenceStorage}, target.sequenceType,
                     source.span});
                if (target.ownsSequence) {
                    const auto targetType = Type{TypeKind::Edit, 0, {target.sequenceType}};
                    const auto borrowed = current_->expressions.size();
                    current_->expressions.push_back(
                        {FirOwnershipExpression{FirOwnershipOperator::Edit, receiver},
                         targetType, source.span});
                    receiver = borrowed;
                }
                next = current_->expressions.size();
                current_->expressions.push_back(
                    {FirCallExpression{FirCallKind::Function, target.nextFunction,
                                       target.nextTypeArguments, {receiver}, {}, 0, 0, 0, {}},
                     target.nextResultType, source.span});
            }
            value = FirForStatement{sequence, target.sequenceStorage, target.index,
                                    target.value, lowerBlock(loop->body), next,
                                    target.ownsSequence};
        } else if (const auto *selection = std::get_if<SelectStatement>(&source.value)) {
            const auto &target = required(model_.selectTargets[id]);
            std::vector<FirSelectOperationArm> operations;
            operations.reserve(selection->operations.size());
            for (std::size_t index = 0; index < selection->operations.size(); ++index) {
                const auto &arm = selection->operations[index];
                const auto &operation = required(
                    model_.channelOperationTargets[arm.operation]);
                std::optional<FirExpressionId> argument;
                if (operation.value.has_value()) {
                    argument = lowerExpression(*operation.value);
                }
                operations.push_back(
                    {operation.kind == ChannelOperationKind::Send,
                     operation.endpoint,
                     argument,
                     operation.valueStorage,
                     operation.resultStorage,
                     target.bindings[index],
                     lowerBlock(arm.body),
                     arm.span});
            }
            std::optional<FirSelectTimeoutArm> timeout;
            if (selection->timeout.has_value()) {
                timeout = FirSelectTimeoutArm{selection->timeout->nanoseconds,
                                              lowerBlock(selection->timeout->body)};
            }
            value = FirSelectStatement{std::move(operations), std::move(timeout),
                                       target.errorLocal, lowerBlock(selection->errorBlock),
                                       target.deadlineStorage};
        } else if (const auto *unsafe = std::get_if<UnsafeStatement>(&source.value)) {
            value = FirUnsafeStatement{lowerBlock(unsafe->body)};
        } else {
            const auto &loop = std::get<WhileStatement>(source.value);
            value = FirWhileStatement{lowerExpression(loop.condition), lowerBlock(loop.body)};
        }

        const auto lowered = current_->statements.size();
        current_->statements.push_back({std::move(value), source.span});
        statementMap_[id] = lowered;
        return lowered;
    }

    FirExpressionId lowerCallArgument(AstExpressionId id, std::size_t index,
                                      const CallTarget &target, SourceSpan span) {
        auto argument = lowerExpression(id);
        if (index < target.argumentBorrows.size() &&
            target.argumentBorrows[index].has_value()) {
            const auto borrowed = current_->expressions.size();
            current_->expressions.push_back(
                {FirOwnershipExpression{FirOwnershipOperator::View, argument, true},
                 *target.argumentBorrows[index], span});
            argument = borrowed;
        }
        if (index < target.argumentConversions.size() &&
            target.argumentConversions[index].has_value()) {
            const auto &conversion = *target.argumentConversions[index];
            const auto converted = current_->expressions.size();
            current_->expressions.push_back(
                {FirContractExpression{
                     argument, conversion.concreteType, conversion.contractType,
                     lowerContractMethods(conversion.methods)},
                 conversion.targetType, span});
            argument = converted;
        }
        return argument;
    }

    FirExpressionId lowerExpression(AstExpressionId id) {
        if (expressionMap_[id].has_value()) {
            return *expressionMap_[id];
        }

        const auto &source = program_.expressions[id];
        FirExpressionValue value;
        if (const auto *integer = std::get_if<IntegerExpression>(&source.value)) {
            value = FirIntegerExpression{integer->magnitude, integer->negative};
        } else if (const auto *floating =
                       std::get_if<FloatingExpression>(&source.value)) {
            value = FirFloatingExpression{floating->text};
        } else if (const auto *boolean = std::get_if<BooleanExpression>(&source.value)) {
            value = FirBooleanExpression{boolean->value};
        } else if (const auto *string = std::get_if<StringExpression>(&source.value)) {
            value = FirStringExpression{string->value};
        } else if (const auto *array = std::get_if<ArrayExpression>(&source.value)) {
            std::vector<FirExpressionId> elements;
            elements.reserve(array->elements.size());
            for (const auto element : array->elements) {
                elements.push_back(lowerExpression(element));
            }
            value = FirArrayExpression{std::move(elements)};
        } else if (std::holds_alternative<NameExpression>(source.value)) {
            if (model_.functionValueTargets[id].has_value()) {
                const auto &target = *model_.functionValueTargets[id];
                value = FirFunctionValueExpression{target.function, target.typeArguments};
            } else {
                const auto local = required(model_.expressionLocals[id]);
                if (model_.expressionMoves[id]) {
                    value = FirMoveExpression{local};
                } else if (model_.expressionReads[id]) {
                    value = FirReadExpression{local};
                } else {
                    value = FirLocalExpression{local};
                }
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&source.value)) {
            if (model_.emptyTests[id]) {
                const auto &target = required(model_.callTargets[id]);
                if (target.kind == CallTargetKind::Len) {
                    value = FirUnaryExpression{FirUnaryOperator::Empty,
                                               lowerExpression(unary->operand)};
                } else {
                    auto receiver = lowerExpression(unary->operand);
                    if (target.kind == CallTargetKind::Method &&
                        target.receiverType.kind == TypeKind::View) {
                        const auto borrowed = current_->expressions.size();
                        current_->expressions.push_back(
                            {FirOwnershipExpression{FirOwnershipOperator::View, receiver},
                             target.receiverType, source.span});
                        receiver = borrowed;
                    }
                    const auto kind = target.kind == CallTargetKind::ContractMethod
                                          ? FirCallKind::Contract
                                          : FirCallKind::Function;
                    value = FirCallExpression{kind, target.function,
                                              target.typeArguments, {receiver}, {},
                                              target.contract, target.method, 0, {0}};
                }
            } else {
                value = FirUnaryExpression{lowerUnary(unary->operation),
                                           lowerExpression(unary->operand)};
            }
        } else if (const auto *ownership = std::get_if<OwnershipExpression>(&source.value)) {
            if (model_.taskWaitTargets[id].has_value()) {
                value = FirTaskWaitExpression{
                    lowerExpression(model_.taskWaitTargets[id]->task)};
            } else if (ownership->operation == OwnershipOperator::Transfer ||
                       ownership->operation == OwnershipOperator::New) {
                const auto operand = lowerExpression(ownership->operand);
                if (model_.expressionContractConversions[id].has_value()) {
                    const auto &conversion = *model_.expressionContractConversions[id];
                    const auto lowered = current_->expressions.size();
                    current_->expressions.push_back(
                        {FirContractExpression{
                             operand, conversion.concreteType, conversion.contractType,
                             lowerContractMethods(conversion.methods)},
                         conversion.targetType, source.span});
                    expressionMap_[id] = lowered;
                    return lowered;
                }
                expressionMap_[id] = operand;
                return *expressionMap_[id];
            } else {
                value = FirOwnershipExpression{lowerOwnership(ownership->operation),
                                               lowerExpression(ownership->operand)};
            }
        } else if (const auto *binary = std::get_if<BinaryExpression>(&source.value)) {
            value = FirBinaryExpression{lowerExpression(binary->left),
                                        lowerBinary(binary->operation),
                                        lowerExpression(binary->right)};
        } else if (const auto *call = std::get_if<CallExpression>(&source.value)) {
            const auto &target = required(model_.callTargets[id]);
            std::vector<FirExpressionId> arguments;
            arguments.reserve(call->arguments.size());
            for (std::size_t index = 0; index < call->arguments.size(); ++index) {
                arguments.push_back(lowerCallArgument(call->arguments[index], index, target,
                                                      source.span));
            }
            if (model_.blockingCallTargets[id].has_value()) {
                const auto &blocking = *model_.blockingCallTargets[id];
                value = FirBlockingCallExpression{blocking.function, std::move(arguments),
                                                  blocking.argumentStorages,
                                                  blocking.resultStorage,
                                                  target.argumentParameters};
            } else if (model_.callbackCallTargets[id].has_value()) {
                const auto &callback = *model_.callbackCallTargets[id];
                value = FirCallbackCallExpression{callback.function, std::move(arguments),
                                                  callback.argumentStorages,
                                                  callback.resultStorage,
                                                  target.argumentParameters};
            } else if (target.kind == CallTargetKind::Channel) {
                value = FirChannelExpression{
                    target.typeArguments.empty() ? invalidType : target.typeArguments.front(),
                    arguments.empty() ? 0 : arguments.front()};
            } else {
                auto kind = FirCallKind::Function;
                switch (target.kind) {
                case CallTargetKind::Function:
                case CallTargetKind::Method:
                    break;
                case CallTargetKind::FunctionValue:
                    kind = FirCallKind::FunctionValue;
                    break;
                case CallTargetKind::ContractMethod:
                case CallTargetKind::Channel:
                    std::terminate();
                case CallTargetKind::Print:
                    kind = FirCallKind::Print;
                    break;
                case CallTargetKind::Panic:
                    kind = FirCallKind::Panic;
                    break;
                case CallTargetKind::Len:
                    kind = FirCallKind::Len;
                    break;
                case CallTargetKind::Null:
                    kind = FirCallKind::Null;
                    break;
                case CallTargetKind::IsNull:
                    kind = FirCallKind::IsNull;
                    break;
                case CallTargetKind::NumericConversion:
                    kind = FirCallKind::NumericConversion;
                    break;
                }
                value = FirCallExpression{kind, target.function, target.typeArguments,
                                          std::move(arguments), target.argumentDrops, 0, 0,
                                          target.local, target.argumentParameters};
            }
        } else if (const auto *literal = std::get_if<StructExpression>(&source.value)) {
            const auto &target = required(model_.structTargets[id]);
            std::vector<FirStructFieldValue> fields;
            fields.reserve(literal->fields.size() + target.defaults.size());
            for (std::size_t index = 0; index < literal->fields.size(); ++index) {
                fields.push_back(
                    {target.fields[index], lowerExpression(literal->fields[index].value)});
            }
            for (const auto &field : target.defaults) {
                const auto lowered = current_->expressions.size();
                current_->expressions.push_back(
                    {FirCallExpression{FirCallKind::Function, field.function,
                                       target.type.arguments, {}, {}, 0, 0, 0, {}},
                     substituteType(
                         model_.structs[target.type.declaration].fieldTypes[field.field],
                         target.type.arguments),
                     source.span});
                fields.push_back({field.field, lowered});
            }
            value = FirStructExpression{target.type, std::move(fields)};
        } else if (const auto *spawn = std::get_if<SpawnExpression>(&source.value)) {
            value = FirSpawnExpression{lowerExpression(spawn->call)};
        } else if (const auto *member = std::get_if<MemberExpression>(&source.value)) {
            if (member->base.has_value() && !member->invoked && member->member == "pointer" &&
                (model_.expressionTypes[id].kind == TypeKind::Raw ||
                 model_.expressionTypes[id].kind == TypeKind::RawConst)) {
                value = FirRawPointerExpression{lowerExpression(*member->base)};
            } else if (model_.channelSenderClones[id]) {
                value = FirChannelSenderCloneExpression{lowerExpression(*member->base)};
            } else if (model_.channelOperationTargets[id].has_value()) {
                const auto &target = *model_.channelOperationTargets[id];
                if (target.kind == ChannelOperationKind::Send) {
                    std::optional<FirExpressionId> argument;
                    if (target.value.has_value()) {
                        argument = lowerExpression(*target.value);
                    }
                    value = FirChannelSendExpression{target.endpoint, argument,
                                                     target.valueStorage,
                                                     target.resultStorage};
                } else {
                    value = FirChannelReceiveExpression{target.endpoint,
                                                        target.resultStorage};
                }
            } else if (model_.enumTargets[id].has_value()) {
                const auto &target = *model_.enumTargets[id];
                std::optional<FirExpressionId> payload;
                if (!member->arguments.empty()) {
                    payload = lowerExpression(member->arguments.front());
                }
                value = FirEnumExpression{target.type, target.variant, payload};
            } else if (model_.callTargets[id].has_value()) {
                const auto &target = *model_.callTargets[id];
                if ((target.kind == CallTargetKind::Function ||
                     target.kind == CallTargetKind::NumericConversion) &&
                    !target.receiver.has_value()) {
                    std::vector<FirExpressionId> arguments;
                    arguments.reserve(member->arguments.size());
                    for (std::size_t index = 0; index < member->arguments.size(); ++index) {
                        arguments.push_back(lowerCallArgument(
                            member->arguments[index], index, target, source.span));
                    }
                    const auto kind = target.kind == CallTargetKind::NumericConversion
                                          ? FirCallKind::NumericConversion
                                          : FirCallKind::Function;
                    value = FirCallExpression{kind, target.function,
                                              target.typeArguments, std::move(arguments),
                                              target.argumentDrops, 0, 0, 0,
                                              target.argumentParameters};
                    current_->expressions.push_back(
                        {std::move(value), model_.expressionTypes[id], source.span});
                    expressionMap_[id] = current_->expressions.size() - 1;
                    return *expressionMap_[id];
                }
                auto receiver = lowerExpression(required(target.receiver));
                if (target.receiverConversion.has_value()) {
                    const auto &conversion = *target.receiverConversion;
                    const auto borrowed = current_->expressions.size();
                    const auto operation = target.receiverType.kind == TypeKind::Edit
                                               ? FirOwnershipOperator::Edit
                                               : FirOwnershipOperator::View;
                    current_->expressions.push_back(
                        {FirOwnershipExpression{operation, receiver}, target.receiverType,
                         source.span});
                    const auto converted = current_->expressions.size();
                    current_->expressions.push_back(
                        {FirContractExpression{
                             borrowed, conversion.concreteType, conversion.contractType,
                             lowerContractMethods(conversion.methods)},
                         conversion.targetType, source.span});
                    receiver = converted;
                } else if (target.kind == CallTargetKind::Method &&
                    (target.receiverType.kind == TypeKind::View ||
                     target.receiverType.kind == TypeKind::Edit)) {
                    const auto lowered = current_->expressions.size();
                    const auto operation = target.receiverType.kind == TypeKind::View
                                               ? FirOwnershipOperator::View
                                               : FirOwnershipOperator::Edit;
                    current_->expressions.push_back(
                        {FirOwnershipExpression{operation, receiver}, target.receiverType,
                         source.span});
                    receiver = lowered;
                }
                std::vector<FirExpressionId> arguments;
                arguments.reserve(member->arguments.size() + 1);
                arguments.push_back(receiver);
                for (std::size_t index = 0; index < member->arguments.size(); ++index) {
                    arguments.push_back(lowerCallArgument(
                        member->arguments[index], index, target, source.span));
                }
                const auto kind = target.kind == CallTargetKind::ContractMethod
                                      ? FirCallKind::Contract
                                      : FirCallKind::Function;
                std::vector<std::size_t> argumentParameters;
                argumentParameters.reserve(target.argumentParameters.size() + 1);
                argumentParameters.push_back(0);
                for (const auto parameter : target.argumentParameters) {
                    argumentParameters.push_back(parameter + 1);
                }
                value = FirCallExpression{kind, target.function, target.typeArguments,
                                          std::move(arguments), {}, target.contract,
                                          target.method, 0, std::move(argumentParameters)};
            } else {
                value = FirFieldExpression{lowerExpression(required(member->base)),
                                           required(model_.expressionFields[id])};
            }
        } else if (const auto *index = std::get_if<IndexExpression>(&source.value)) {
            value = FirIndexExpression{lowerExpression(index->base),
                                       lowerExpression(index->index)};
        } else if (const auto *replace = std::get_if<ReplaceExpression>(&source.value)) {
            value = FirReplaceExpression{lowerExpression(replace->target),
                                         lowerExpression(replace->value)};
        } else if (const auto *function = std::get_if<FunctionExpression>(&source.value)) {
            static_cast<void>(function);
            const auto &target = required(model_.closureTargets[id]);
            std::vector<FirClosureCapture> captures;
            captures.reserve(target.captures.size());
            for (std::size_t captureIndex = 0; captureIndex < target.captures.size();
                 ++captureIndex) {
                captures.push_back({target.captures[captureIndex],
                                    lowerCapture(target.modes[captureIndex])});
            }
            value = FirClosureExpression{target.function, std::move(captures)};
        } else if (const auto *conditional =
                       std::get_if<ConditionalExpression>(&source.value)) {
            value = FirConditionalExpression{lowerExpression(conditional->condition),
                                             lowerBlock(conditional->thenBlock),
                                             lowerExpression(conditional->thenValue),
                                             lowerBlock(conditional->elseBlock),
                                             lowerExpression(conditional->elseValue)};
        } else {
            const auto &match = std::get<MatchExpression>(source.value);
            const auto &target = required(model_.matchTargets[id]);
            std::vector<FirMatchArm> arms;
            arms.reserve(match.arms.size());
            for (std::size_t arm = 0; arm < match.arms.size(); ++arm) {
                arms.push_back({target.variants[arm], target.bindings[arm],
                                match.arms[arm].pattern.has_value()
                                    ? std::optional<FirExpressionId>{
                                          lowerExpression(*match.arms[arm].pattern)}
                                    : std::nullopt,
                                lowerExpression(match.arms[arm].expression), target.drops[arm]});
            }
            value = FirMatchExpression{lowerExpression(match.value), target.type, std::move(arms)};
        }

        if (model_.expressionContractConversions[id].has_value()) {
            const auto &conversion = *model_.expressionContractConversions[id];
            const auto operand = current_->expressions.size();
            current_->expressions.push_back(
                {std::move(value), conversion.sourceType, source.span});
            const auto lowered = current_->expressions.size();
            current_->expressions.push_back(
                {FirContractExpression{
                     operand, conversion.concreteType, conversion.contractType,
                     lowerContractMethods(conversion.methods)},
                 conversion.targetType, source.span});
            expressionMap_[id] = lowered;
            return lowered;
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
