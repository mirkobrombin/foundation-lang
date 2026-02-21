#include "foundation/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace foundation {

namespace {

[[noreturn]] void internalError(const char *message) {
    std::fputs("foundation compiler internal error: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

std::string cString(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const auto raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte >= 0x20 && byte <= 0x7e) {
                out << static_cast<char>(byte);
            } else {
                out << '\\' << std::oct << std::setw(3) << std::setfill('0')
                    << static_cast<unsigned int>(byte) << std::dec;
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

std::string cTypeTag(const Type &type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "i32";
    case TypeKind::U64:
        return "u64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "string";
    case TypeKind::Array:
        return "array_" + std::to_string(type.declaration) + '_' +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Slice:
        return "slice_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Own:
        return "own_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::View:
        if (type.arguments.size() == 1 &&
            type.arguments.front().kind == TypeKind::Contract) {
            return "contract_" + std::to_string(type.arguments.front().declaration);
        }
        return "view_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Edit:
        if (type.arguments.size() == 1 &&
            type.arguments.front().kind == TypeKind::Contract) {
            return "contract_" + std::to_string(type.arguments.front().declaration);
        }
        return "edit_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Struct:
        return "struct_" + std::to_string(type.declaration);
    case TypeKind::Enum:
        return "enum_" + std::to_string(type.declaration);
    case TypeKind::Contract:
        return "contract_" + std::to_string(type.declaration);
    case TypeKind::Function: {
        std::string result = "function";
        for (const auto &argument : type.arguments) {
            result += '_' + cTypeTag(argument);
        }
        return result;
    }
    case TypeKind::Task:
        return "task_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Channel:
        return "channel_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Sender:
        return "sender_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Receiver:
        return "receiver_" +
               (type.arguments.size() == 1 ? cTypeTag(type.arguments.front()) : "invalid");
    case TypeKind::Parameter:
    case TypeKind::Invalid:
        break;
    }
    return "invalid";
}

std::string arrayName(const Type &type) { return "fdn_" + cTypeTag(type); }

std::string sliceName(const Type &type) { return "fdn_" + cTypeTag(type); }

std::string channelDropName(const Type &type) {
    return "fdn_channel_drop_" + cTypeTag(type);
}

std::string cType(const Type &type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "int32_t";
    case TypeKind::U64:
        return "uint64_t";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "fdn_string";
    case TypeKind::Array:
        return arrayName(type);
    case TypeKind::Slice:
        break;
    case TypeKind::Own:
        return type.arguments.size() == 1 ? cType(type.arguments.front()) + " *" : "void *";
    case TypeKind::Edit:
        if (type.arguments.size() == 1 &&
            type.arguments.front().kind == TypeKind::Contract) {
            return "fdn_contract_" + std::to_string(type.arguments.front().declaration);
        }
        if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            return sliceName(type);
        }
        return type.arguments.size() == 1 ? cType(type.arguments.front()) + " *" : "void *";
    case TypeKind::View:
        if (type.arguments.size() == 1 &&
            type.arguments.front().kind == TypeKind::Contract) {
            return "fdn_contract_" + std::to_string(type.arguments.front().declaration);
        }
        if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            return sliceName(type);
        }
        return type.arguments.size() == 1 ? "const " + cType(type.arguments.front()) + " *"
                                          : "const void *";
    case TypeKind::Parameter:
        break;
    case TypeKind::Struct:
        return "fdn_struct_" + std::to_string(type.declaration);
    case TypeKind::Enum:
        return "fdn_enum_" + std::to_string(type.declaration);
    case TypeKind::Contract:
        return "fdn_contract_" + std::to_string(type.declaration);
    case TypeKind::Function:
        return "fdn_" + cTypeTag(type);
    case TypeKind::Task:
        return "fdn_task *";
    case TypeKind::Channel:
        return "fdn_channel_pair";
    case TypeKind::Sender:
    case TypeKind::Receiver:
        return "fdn_channel *";
    case TypeKind::Invalid:
        break;
    }
    return "void";
}

std::string safeName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const auto value : name) {
        const auto byte = static_cast<unsigned char>(value);
        result.push_back(std::isalnum(byte) != 0 ? value : '_');
    }
    return result;
}

std::string_view unqualifiedName(std::string_view name) {
    const auto separator = name.rfind('.');
    return name.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

std::string_view traceFunctionName(const FirFunction &function) {
    if (!function.packageName.empty() &&
        function.name.starts_with(function.packageName + '.')) {
        return std::string_view(function.name).substr(function.packageName.size() + 1);
    }
    return unqualifiedName(function.name);
}

std::string functionName(const FirProgram &program, FirFunctionId id) {
    if (id == program.main) {
        return "fdn_program_main";
    }
    const auto &function = program.functions[id];
    auto name = "fdn_fn_" + safeName(function.name) + "_" + std::to_string(function.source);
    if (function.generic) {
        name += "_g" + std::to_string(id);
    }
    return name;
}

std::string functionAdapterName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_value_adapter";
}

std::string taskFrameName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_task_frame";
}

std::string taskPollName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_task_poll";
}

std::string taskMoveResultName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_task_move_result";
}

std::string taskDropFrameName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_task_drop_frame";
}

std::string blockingWorkName(const FirProgram &program, FirFunctionId function,
                             FirExpressionId expression) {
    return functionName(program, function) + "_blocking_" + std::to_string(expression);
}

std::string blockingJobName(FirExpressionId expression) {
    return "fdn_blocking_job_" + std::to_string(expression);
}

std::string closureEnvironmentName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_environment";
}

std::string closureDropName(const FirProgram &program, FirFunctionId id) {
    return closureEnvironmentName(program, id) + "_drop";
}

std::string localName(const FirFunction &function, FirLocalId id) {
    return "fdn_local_" + safeName(function.locals[id].name) + "_" + std::to_string(id);
}

std::string fieldName(FirFieldId id) { return "fdn_field_" + std::to_string(id); }

std::string enumTag(FirEnumId type, FirVariantId variant) {
    return "FDN_ENUM_" + std::to_string(type) + "_VARIANT_" + std::to_string(variant);
}

std::string payloadName(FirVariantId variant) {
    return "fdn_payload_" + std::to_string(variant);
}

std::string vtableName(const Type &contract, const Type &concrete) {
    return "fdn_vtable_c" + std::to_string(contract.declaration) + "_s" +
           std::to_string(concrete.declaration);
}

std::string indentation(unsigned int depth) { return std::string(depth * 4, ' '); }

std::string i32Constant(const FirIntegerExpression &value) {
    if (value.negative && value.magnitude == UINT64_C(2147483648)) {
        return "(-INT32_C(2147483647) - INT32_C(1))";
    }
    const auto magnitude = "INT32_C(" + std::to_string(value.magnitude) + ")";
    return value.negative ? "(-" + magnitude + ')' : magnitude;
}

std::string u64Constant(const FirIntegerExpression &value) {
    return "UINT64_C(" + std::to_string(value.magnitude) + ")";
}

Type substitute(const Type &type, const std::vector<Type> &arguments) {
    if (type.kind == TypeKind::Parameter) {
        return type.declaration < arguments.size() ? arguments[type.declaration] : invalidType;
    }
    auto result = type;
    for (auto &argument : result.arguments) {
        argument = substitute(argument, arguments);
    }
    return result;
}

std::string typeKey(const Type &type) {
    std::string result = std::to_string(static_cast<unsigned int>(type.kind)) + ':' +
                         std::to_string(type.declaration);
    if (!type.arguments.empty()) {
        result += '<';
        for (const auto &argument : type.arguments) {
            result += typeKey(argument) + ';';
        }
        result += '>';
    }
    return result;
}

std::string functionKey(FirFunctionId function, const std::vector<Type> &arguments) {
    std::string result = std::to_string(function) + '<';
    for (const auto &argument : arguments) {
        result += typeKey(argument) + ';';
    }
    result += '>';
    return result;
}

class Monomorphizer {
  public:
    explicit Monomorphizer(const FirProgram &source) : source_(source) {}

    FirProgram run() {
        result_.main = instantiateFunction(source_.main, {});
        for (std::size_t index = 0; index < source_.functions.size(); ++index) {
            const auto &function = source_.functions[index];
            if (function.cSymbol.has_value() && function.hasBody) {
                static_cast<void>(instantiateFunction(index, {}));
            }
        }
        return std::move(result_);
    }

  private:
    Type instantiateType(const Type &source) {
        if (source.kind == TypeKind::Parameter || source.kind == TypeKind::Invalid) {
            internalError("unresolved type reached monomorphization");
        }
        if (source.kind == TypeKind::Function) {
            Type result{TypeKind::Function};
            result.arguments.reserve(source.arguments.size());
            for (const auto &argument : source.arguments) {
                result.arguments.push_back(instantiateType(argument));
            }
            return result;
        }
        if (source.kind == TypeKind::Own || source.kind == TypeKind::View ||
            source.kind == TypeKind::Edit || source.kind == TypeKind::Array ||
            source.kind == TypeKind::Slice || source.kind == TypeKind::Task) {
            if (source.arguments.size() != 1) {
                internalError("invalid wrapper type reached monomorphization");
            }
            return Type{source.kind, source.declaration,
                        {instantiateType(source.arguments.front())}};
        }
        if (source.kind == TypeKind::Contract) {
            const auto key = typeKey(source);
            if (const auto found = contracts_.find(key); found != contracts_.end()) {
                return Type{TypeKind::Contract, found->second, {}};
            }
            const auto id = result_.contracts.size();
            contracts_.emplace(key, id);
            result_.contracts.emplace_back();
            const auto &declaration = source_.contracts[source.declaration];
            FirContract instance;
            instance.name = declaration.name;
            instance.exported = declaration.exported;
            for (const auto &method : declaration.methods) {
                FirContractMethod target;
                target.receiver = method.receiver;
                target.name = method.name;
                target.returnType =
                    instantiateType(substitute(method.returnType, source.arguments));
                target.parameterNames = method.parameterNames;
                target.exported = method.exported;
                for (const auto &parameter : method.parameters) {
                    target.parameters.push_back(
                        instantiateType(substitute(parameter, source.arguments)));
                }
                instance.methods.push_back(std::move(target));
            }
            result_.contracts[id] = std::move(instance);
            return Type{TypeKind::Contract, id, {}};
        }
        if (source.kind != TypeKind::Struct && source.kind != TypeKind::Enum) {
            return source;
        }

        const auto key = typeKey(source);
        if (source.kind == TypeKind::Struct) {
            if (const auto found = structs_.find(key); found != structs_.end()) {
                return Type{TypeKind::Struct, found->second, {}};
            }
            const auto id = result_.structs.size();
            structs_.emplace(key, id);
            result_.structs.emplace_back();
            const auto &declaration = source_.structs[source.declaration];
            FirStruct instance;
            instance.name = declaration.name;
            instance.exported = declaration.exported;
            for (const auto &field : declaration.fields) {
                instance.fields.push_back({field.name,
                                           instantiateType(
                                               substitute(field.type, source.arguments)),
                                           field.exported, {}});
            }
            if (declaration.dropFunction.has_value()) {
                instance.dropFunction =
                    instantiateFunction(*declaration.dropFunction, source.arguments);
            }
            result_.structs[id] = std::move(instance);
            return Type{TypeKind::Struct, id, {}};
        }

        if (const auto found = enums_.find(key); found != enums_.end()) {
            return Type{TypeKind::Enum, found->second, {}};
        }
        const auto id = result_.enums.size();
        enums_.emplace(key, id);
        result_.enums.emplace_back();
        const auto &declaration = source_.enums[source.declaration];
        FirEnum instance;
        instance.name = declaration.name;
        instance.exported = declaration.exported;
        instance.builtin = declaration.builtin;
        for (const auto &variant : declaration.variants) {
            std::optional<Type> payload;
            if (variant.payload.has_value()) {
                const auto substituted = substitute(*variant.payload, source.arguments);
                if (!(declaration.builtin && substituted == voidType)) {
                    payload = instantiateType(substituted);
                }
            }
            instance.variants.push_back(
                {variant.name, std::move(payload), variant.exported, {}});
        }
        result_.enums[id] = std::move(instance);
        return Type{TypeKind::Enum, id, {}};
    }

    FirFunctionId instantiateFunction(FirFunctionId sourceId,
                                      const std::vector<Type> &arguments) {
        const auto key = functionKey(sourceId, arguments);
        if (const auto found = functions_.find(key); found != functions_.end()) {
            return found->second;
        }
        if (functions_.size() >= 4096) {
            internalError("monomorphization function limit exceeded");
        }

        const auto id = result_.functions.size();
        functions_.emplace(key, id);
        result_.functions.emplace_back();
        auto function = source_.functions[sourceId];
        function.typeParameterCount = 0;
        function.returnType = instantiateType(substitute(function.returnType, arguments));
        for (auto &local : function.locals) {
            local.type = instantiateType(substitute(local.type, arguments));
        }
        for (auto &expression : function.expressions) {
            expression.type = instantiateType(substitute(expression.type, arguments));
            if (auto *functionValue =
                    std::get_if<FirFunctionValueExpression>(&expression.value)) {
                std::vector<Type> valueArguments;
                valueArguments.reserve(functionValue->typeArguments.size());
                for (const auto &argument : functionValue->typeArguments) {
                    valueArguments.push_back(substitute(argument, arguments));
                }
                functionValue->function =
                    instantiateFunction(functionValue->function, valueArguments);
                functionValue->typeArguments.clear();
            } else if (auto *closure = std::get_if<FirClosureExpression>(&expression.value)) {
                closure->function = instantiateFunction(closure->function, arguments);
            } else if (auto *functionCall = std::get_if<FirCallExpression>(&expression.value);
                functionCall != nullptr && functionCall->kind == FirCallKind::Function) {
                std::vector<Type> callArguments;
                callArguments.reserve(functionCall->typeArguments.size());
                for (const auto &argument : functionCall->typeArguments) {
                    callArguments.push_back(substitute(argument, arguments));
                }
                functionCall->function =
                    instantiateFunction(functionCall->function, callArguments);
                functionCall->typeArguments.clear();
            } else if (auto *blocking =
                           std::get_if<FirBlockingCallExpression>(&expression.value)) {
                blocking->function = instantiateFunction(blocking->function, {});
            } else if (auto *contractCall =
                           std::get_if<FirCallExpression>(&expression.value);
                       contractCall != nullptr &&
                       contractCall->kind == FirCallKind::Contract) {
                std::vector<Type> contractArguments;
                contractArguments.reserve(contractCall->typeArguments.size());
                for (const auto &argument : contractCall->typeArguments) {
                    contractArguments.push_back(substitute(argument, arguments));
                }
                const auto contractType = instantiateType(Type{
                    TypeKind::Contract, contractCall->contract, std::move(contractArguments)});
                contractCall->contract = contractType.declaration;
                contractCall->typeArguments.clear();
            } else if (auto *contractValue =
                           std::get_if<FirContractExpression>(&expression.value)) {
                const auto concrete = substitute(contractValue->concreteType, arguments);
                const auto contractType = substitute(contractValue->contractType, arguments);
                contractValue->concreteType = instantiateType(concrete);
                contractValue->contractType = instantiateType(contractType);
                for (auto &method : contractValue->methods) {
                    std::vector<Type> methodArguments;
                    methodArguments.reserve(method.typeArguments.size());
                    for (const auto &argument : method.typeArguments) {
                        methodArguments.push_back(substitute(argument, arguments));
                    }
                    method.function = instantiateFunction(method.function, methodArguments);
                    method.typeArguments.clear();
                    if (method.contractDefault) {
                        method.defaultContract = instantiateType(
                            substitute(method.defaultContract, arguments));
                    }
                }
            } else if (auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
                literal->type = instantiateType(substitute(literal->type, arguments));
            } else if (auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
                constructor->type = instantiateType(substitute(constructor->type, arguments));
            } else if (auto *match = std::get_if<FirMatchExpression>(&expression.value)) {
                match->type = instantiateType(substitute(match->type, arguments));
            }
        }
        for (auto &statement : function.statements) {
            if (auto *destructure =
                    std::get_if<FirStructDestructureStatement>(&statement.value)) {
                destructure->type =
                    instantiateType(substitute(destructure->type, arguments));
            }
        }
        result_.functions[id] = std::move(function);
        return id;
    }

    const FirProgram &source_;
    FirProgram result_;
    std::unordered_map<std::string, FirStructId> structs_;
    std::unordered_map<std::string, FirEnumId> enums_;
    std::unordered_map<std::string, FirContractId> contracts_;
    std::unordered_map<std::string, FirFunctionId> functions_;
};

enum class ControlFlow {
    Continues,
    Returns,
    Diverges,
};

bool expressionDiverges(const FirProgram &program, const FirFunction &function,
                        FirExpressionId id) {
    const auto &expression = function.expressions[id];
    if (const auto *array = std::get_if<FirArrayExpression>(&expression.value)) {
        return std::any_of(array->elements.begin(), array->elements.end(), [&](const auto element) {
            return expressionDiverges(program, function, element);
        });
    }
    if (std::holds_alternative<FirMoveExpression>(expression.value)) {
        return false;
    }
    if (std::holds_alternative<FirFunctionValueExpression>(expression.value) ||
        std::holds_alternative<FirClosureExpression>(expression.value)) {
        return false;
    }
    if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
        return expressionDiverges(program, function, unary->operand);
    }
    if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
        return expressionDiverges(program, function, ownership->operand);
    }
    if (const auto *spawn = std::get_if<FirSpawnExpression>(&expression.value)) {
        const auto *call = std::get_if<FirCallExpression>(
            &function.expressions[spawn->call].value);
        if (call == nullptr) {
            return false;
        }
        return std::any_of(call->arguments.begin(), call->arguments.end(),
                           [&](const auto argument) {
                               return expressionDiverges(program, function, argument);
                           });
    }
    if (const auto *wait = std::get_if<FirTaskWaitExpression>(&expression.value)) {
        return expressionDiverges(program, function, wait->task);
    }
    if (const auto *blocking = std::get_if<FirBlockingCallExpression>(&expression.value)) {
        return std::any_of(blocking->arguments.begin(), blocking->arguments.end(),
                           [&](const auto argument) {
                               return expressionDiverges(program, function, argument);
                           });
    }
    if (const auto *channel = std::get_if<FirChannelExpression>(&expression.value)) {
        return expressionDiverges(program, function, channel->capacity);
    }
    if (const auto *send = std::get_if<FirChannelSendExpression>(&expression.value)) {
        return send->value.has_value() &&
               expressionDiverges(program, function, *send->value);
    }
    if (std::holds_alternative<FirChannelReceiveExpression>(expression.value)) {
        return false;
    }
    if (const auto *index = std::get_if<FirIndexExpression>(&expression.value)) {
        return expressionDiverges(program, function, index->base) ||
               expressionDiverges(program, function, index->index);
    }
    if (const auto *replace = std::get_if<FirReplaceExpression>(&expression.value)) {
        return expressionDiverges(program, function, replace->value) ||
               expressionDiverges(program, function, replace->target);
    }
    if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
        if (expressionDiverges(program, function, binary->left)) {
            return true;
        }
        return binary->operation != FirBinaryOperator::And &&
               binary->operation != FirBinaryOperator::Or &&
               expressionDiverges(program, function, binary->right);
    }
    if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
        for (const auto argument : call->arguments) {
            if (expressionDiverges(program, function, argument)) {
                return true;
            }
        }
        return call->kind == FirCallKind::Panic ||
               (call->kind == FirCallKind::Function &&
                program.functions[call->function].diverges);
    }
    if (const auto *contract = std::get_if<FirContractExpression>(&expression.value)) {
        return expressionDiverges(program, function, contract->value);
    }
    if (const auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
        return std::any_of(literal->fields.begin(), literal->fields.end(),
                           [&](const FirStructFieldValue &field) {
                               return expressionDiverges(program, function, field.value);
                           });
    }
    if (const auto *field = std::get_if<FirFieldExpression>(&expression.value)) {
        return expressionDiverges(program, function, field->base);
    }
    if (const auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
        return constructor->payload.has_value() &&
               expressionDiverges(program, function, *constructor->payload);
    }
    if (const auto *match = std::get_if<FirMatchExpression>(&expression.value)) {
        if (expressionDiverges(program, function, match->value)) {
            return true;
        }
        return !match->arms.empty() &&
               std::all_of(match->arms.begin(), match->arms.end(),
                           [&](const FirMatchArm &arm) {
                               return expressionDiverges(program, function, arm.expression);
                           });
    }
    return false;
}

ControlFlow blockFlow(const FirProgram &program, const FirFunction &function, FirBlockId id);

ControlFlow statementFlow(const FirProgram &program, const FirFunction &function,
                          const FirStatement &statement) {
    if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
        return expressionDiverges(program, function, variable->initializer)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *binding = std::get_if<FirLetElseStatement>(&statement.value)) {
        return expressionDiverges(program, function, binding->initializer)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *destructure =
            std::get_if<FirStructDestructureStatement>(&statement.value)) {
        return expressionDiverges(program, function, destructure->initializer)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
        return expressionDiverges(program, function, assignment->target) ||
                       expressionDiverges(program, function, assignment->value)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
        return expressionDiverges(program, function, expression->expression)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *discarded = std::get_if<FirDiscardStatement>(&statement.value)) {
        return expressionDiverges(program, function, discarded->expression)
                   ? ControlFlow::Diverges
                   : ControlFlow::Continues;
    }
    if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
        if (returned->value.has_value() &&
            expressionDiverges(program, function, *returned->value)) {
            return ControlFlow::Diverges;
        }
        return ControlFlow::Returns;
    }
    if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
        if (expressionDiverges(program, function, branch->condition)) {
            return ControlFlow::Diverges;
        }
        if (!branch->elseBlock.has_value()) {
            return ControlFlow::Continues;
        }
        const auto thenFlow = blockFlow(program, function, branch->thenBlock);
        const auto elseFlow = blockFlow(program, function, *branch->elseBlock);
        if (thenFlow == ControlFlow::Continues || elseFlow == ControlFlow::Continues) {
            return ControlFlow::Continues;
        }
        if (thenFlow == ControlFlow::Diverges && elseFlow == ControlFlow::Diverges) {
            return ControlFlow::Diverges;
        }
        return ControlFlow::Returns;
    }

    if (const auto *selection = std::get_if<FirSelectStatement>(&statement.value)) {
        for (const auto &arm : selection->operations) {
            if (arm.value.has_value() &&
                expressionDiverges(program, function, *arm.value)) {
                return ControlFlow::Diverges;
            }
        }
        std::vector<ControlFlow> flows;
        flows.reserve(selection->operations.size() + 2);
        for (const auto &arm : selection->operations) {
            flows.push_back(blockFlow(program, function, arm.body));
        }
        if (selection->timeout.has_value()) {
            flows.push_back(blockFlow(program, function, selection->timeout->body));
        }
        flows.push_back(blockFlow(program, function, selection->errorBlock));
        if (std::any_of(flows.begin(), flows.end(), [](ControlFlow flow) {
                return flow == ControlFlow::Continues;
            })) {
            return ControlFlow::Continues;
        }
        return std::all_of(flows.begin(), flows.end(), [](ControlFlow flow) {
                   return flow == ControlFlow::Diverges;
               })
                   ? ControlFlow::Diverges
                   : ControlFlow::Returns;
    }

    const auto &loop = std::get<FirWhileStatement>(statement.value);
    return expressionDiverges(program, function, loop.condition) ? ControlFlow::Diverges
                                                                 : ControlFlow::Continues;
}

ControlFlow blockFlow(const FirProgram &program, const FirFunction &function, FirBlockId id) {
    for (const auto statement : function.blocks[id].statements) {
        const auto flow = statementFlow(program, function, function.statements[statement]);
        if (flow != ControlFlow::Continues) {
            return flow;
        }
    }
    return ControlFlow::Continues;
}

void markDivergingFunctions(FirProgram &program) {
    bool changed;
    do {
        changed = false;
        for (auto &function : program.functions) {
            if (function.hasBody && !function.diverges &&
                blockFlow(program, function, function.body) == ControlFlow::Diverges) {
                function.diverges = true;
                changed = true;
            }
        }
    } while (changed);
}

bool typeRequiresDrop(const FirProgram &program, const Type &type) {
    if (type.kind == TypeKind::String || type.kind == TypeKind::Own ||
        type.kind == TypeKind::Task || type.kind == TypeKind::Channel ||
        type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver ||
        type.kind == TypeKind::Function ||
        type.kind == TypeKind::Parameter) {
        return true;
    }
    if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
        return typeRequiresDrop(program, type.arguments.front());
    }
    if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
        if (program.structs[type.declaration].dropFunction.has_value()) {
            return true;
        }
        return std::any_of(program.structs[type.declaration].fields.begin(),
                           program.structs[type.declaration].fields.end(),
                           [&](const FirStructField &field) {
                               return typeRequiresDrop(program, field.type);
                           });
    }
    if (type.kind == TypeKind::Enum && type.declaration < program.enums.size()) {
        return std::any_of(program.enums[type.declaration].variants.begin(),
                           program.enums[type.declaration].variants.end(),
                           [&](const FirEnumVariant &variant) {
                               return variant.payload.has_value() &&
                                      typeRequiresDrop(program, *variant.payload);
                           });
    }
    return false;
}

std::string dropName(const Type &type) {
    if (type.kind == TypeKind::Function) {
        return "fdn_drop_" + cTypeTag(type);
    }
    if (type.kind == TypeKind::Array) {
        return "fdn_drop_" + cTypeTag(type);
    }
    if (type.kind == TypeKind::Struct) {
        return "fdn_drop_struct_" + std::to_string(type.declaration);
    }
    return "fdn_drop_enum_" + std::to_string(type.declaration);
}

std::string moveName(const Type &type) {
    if (type.kind == TypeKind::Function) {
        return "fdn_move_" + cTypeTag(type);
    }
    if (type.kind == TypeKind::Array) {
        return "fdn_move_" + cTypeTag(type);
    }
    if (type.kind == TypeKind::Struct) {
        return "fdn_move_struct_" + std::to_string(type.declaration);
    }
    return "fdn_move_enum_" + std::to_string(type.declaration);
}

void emitDropValue(std::ostringstream &out, const FirProgram &program, const Type &type,
                   const std::string &value, unsigned int depth) {
    if (!typeRequiresDrop(program, type)) {
        return;
    }
    if (type.kind == TypeKind::Own) {
        out << indentation(depth) << "if (" << value << " != NULL) {\n";
        const auto &target = type.arguments.front();
        if (target.kind == TypeKind::String) {
            out << indentation(depth + 1) << "fdn_string_drop(" << value << ");\n";
        } else if (target.kind == TypeKind::Contract) {
            out << indentation(depth + 1) << value
                << "->fdn_vtable->fdn_drop(" << value << "->fdn_data);\n";
        } else if (target.kind == TypeKind::Array || target.kind == TypeKind::Struct ||
                   target.kind == TypeKind::Enum) {
            if (typeRequiresDrop(program, target)) {
                out << indentation(depth + 1) << dropName(target) << '(' << value << ");\n";
            }
        }
        out << indentation(depth + 1) << "fdn_dealloc(" << value << ");\n";
        out << indentation(depth + 1) << value << " = NULL;\n";
        out << indentation(depth) << "}\n";
        return;
    }
    if (type.kind == TypeKind::String) {
        out << indentation(depth) << "fdn_string_drop(&" << value << ");\n";
        return;
    }
    if (type.kind == TypeKind::Task) {
        out << indentation(depth) << "fdn_task_drop(&" << value << ");\n";
        return;
    }
    if (type.kind == TypeKind::Channel) {
        out << indentation(depth) << "fdn_channel_drop_receiver(&" << value
            << ".fdn_field_1);\n";
        out << indentation(depth) << "fdn_channel_drop_sender(&" << value
            << ".fdn_field_0);\n";
        return;
    }
    if (type.kind == TypeKind::Sender) {
        out << indentation(depth) << "fdn_channel_drop_sender(&" << value << ");\n";
        return;
    }
    if (type.kind == TypeKind::Receiver) {
        out << indentation(depth) << "fdn_channel_drop_receiver(&" << value << ");\n";
        return;
    }
    if (type.kind == TypeKind::Function) {
        out << indentation(depth) << "if (" << value << ".fdn_drop != NULL) {\n";
        out << indentation(depth + 1) << value << ".fdn_drop(" << value
            << ".fdn_env);\n";
        out << indentation(depth) << "}\n";
        out << indentation(depth) << value << ".fdn_env = NULL;\n";
        out << indentation(depth) << value << ".fdn_call = NULL;\n";
        out << indentation(depth) << value << ".fdn_drop = NULL;\n";
        return;
    }
    if (type.kind == TypeKind::Array || type.kind == TypeKind::Struct ||
        type.kind == TypeKind::Enum) {
        out << indentation(depth) << dropName(type) << "(&" << value << ");\n";
    }
}

void emitMoveAssignment(std::ostringstream &out, const FirProgram &program, const Type &type,
                        const std::string &target, const std::string &source,
                        unsigned int depth) {
    if (type.kind == TypeKind::Own || type.kind == TypeKind::Task ||
        type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver) {
        out << indentation(depth) << target << " = " << source << ";\n";
        out << indentation(depth) << source << " = NULL;\n";
        return;
    }
    if (type.kind == TypeKind::Channel) {
        out << indentation(depth) << target << " = " << source << ";\n";
        out << indentation(depth) << source << ".fdn_field_0 = NULL;\n";
        out << indentation(depth) << source << ".fdn_field_1 = NULL;\n";
        return;
    }
    if (type.kind == TypeKind::String) {
        out << indentation(depth) << target << " = fdn_string_move(&" << source << ");\n";
        return;
    }
    if (type.kind == TypeKind::Function) {
        out << indentation(depth) << target << " = " << source << ";\n";
        out << indentation(depth) << source << ".fdn_env = NULL;\n";
        out << indentation(depth) << source << ".fdn_call = NULL;\n";
        out << indentation(depth) << source << ".fdn_drop = NULL;\n";
        return;
    }
    if ((type.kind == TypeKind::Array || type.kind == TypeKind::Struct ||
         type.kind == TypeKind::Enum) &&
        typeRequiresDrop(program, type)) {
        out << indentation(depth) << target << " = " << moveName(type) << "(&" << source
            << ");\n";
        return;
    }
    out << indentation(depth) << target << " = " << source << ";\n";
}

std::size_t taskSuspensionCount(const FirFunction &function);

class FunctionEmitter {
  public:
    FunctionEmitter(std::ostringstream &out, const FirProgram &program, FirFunctionId functionId,
                    bool taskPoll = false)
        : out_(out), program_(program), function_(program.functions[functionId]),
          functionId_(functionId), taskPoll_(taskPoll) {}

    bool emitBlock(FirBlockId id, unsigned int depth) {
        for (const auto statement : function_.blocks[id].statements) {
            if (taskPoll_ && emitSuspendingStatement(function_.statements[statement], depth)) {
                continue;
            }
            if (emitStatement(function_.statements[statement], depth)) {
                return true;
            }
        }
        emitDrops(function_.blocks[id].drops, depth);
        return false;
    }

  private:
    struct EmittedExpression {
        std::string value;
        bool diverges{};
    };

    EmittedExpression emitExpression(FirExpressionId id, unsigned int depth) {
        const auto &expression = function_.expressions[id];
        if (const auto *integer = std::get_if<FirIntegerExpression>(&expression.value)) {
            return {expression.type == u64Type ? u64Constant(*integer)
                                               : i32Constant(*integer),
                    false};
        }
        if (const auto *boolean = std::get_if<FirBooleanExpression>(&expression.value)) {
            return {boolean->value ? "true" : "false", false};
        }
        if (const auto *string = std::get_if<FirStringExpression>(&expression.value)) {
            const auto temporary = nextTemporary();
            out_ << indentation(depth) << "fdn_string " << temporary << " = fdn_string_static("
                 << cString(string->value) << ", " << string->value.size() << ");\n";
            return {temporary, false};
        }
        if (const auto *array = std::get_if<FirArrayExpression>(&expression.value)) {
            return emitArray(*array, expression.type, depth);
        }
        if (const auto *local = std::get_if<FirLocalExpression>(&expression.value)) {
            return {localValue(local->local), false};
        }
        if (const auto *moved = std::get_if<FirMoveExpression>(&expression.value)) {
            return emitMove(moved->local, depth);
        }
        if (const auto *function =
                std::get_if<FirFunctionValueExpression>(&expression.value)) {
            return emitFunctionValue(*function, expression.type, depth);
        }
        if (const auto *closure = std::get_if<FirClosureExpression>(&expression.value)) {
            return emitClosure(*closure, expression.type, depth);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
            const auto operand = emitExpression(unary->operand, depth);
            if (operand.diverges) {
                return operand;
            }
            const auto temporary = nextTemporary();
            if (unary->operation == FirUnaryOperator::Negate) {
                emitLocation(expression.span, depth);
            }
            out_ << indentation(depth) << cType(expression.type) << ' ' << temporary << " = ";
            if (unary->operation == FirUnaryOperator::Negate) {
                out_ << "fdn_i32_negate(" << operand.value << ");\n";
            } else {
                out_ << '!' << operand.value << ";\n";
            }
            return {temporary, false};
        }
        if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
            return emitOwnership(*ownership, expression.type, expression.span, depth);
        }
        if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
            return emitBinary(*binary, expression.type, expression.span, depth);
        }
        if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
            return emitCall(*call, expression.type, expression.span, depth);
        }
        if (const auto *channel = std::get_if<FirChannelExpression>(&expression.value)) {
            return emitChannel(*channel, depth);
        }
        if (std::holds_alternative<FirChannelSendExpression>(expression.value) ||
            std::holds_alternative<FirChannelReceiveExpression>(expression.value)) {
            internalError("suspending channel operation reached expression emission");
        }
        if (const auto *spawn = std::get_if<FirSpawnExpression>(&expression.value)) {
            return emitSpawn(*spawn, depth);
        }
        if (const auto *wait = std::get_if<FirTaskWaitExpression>(&expression.value)) {
            return emitTaskWait(*wait, expression.type, depth);
        }
        if (std::holds_alternative<FirBlockingCallExpression>(expression.value)) {
            internalError("suspending blocking call reached expression emission");
        }
        if (const auto *contract = std::get_if<FirContractExpression>(&expression.value)) {
            return emitContract(*contract, expression.type, depth);
        }
        if (const auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
            return emitStruct(*literal, depth);
        }
        if (const auto *field = std::get_if<FirFieldExpression>(&expression.value)) {
            auto base = emitExpression(field->base, depth);
            if (!base.diverges) {
                const auto baseType = function_.expressions[field->base].type;
                const auto pointer = baseType.kind == TypeKind::Own ||
                                     baseType.kind == TypeKind::View ||
                                     baseType.kind == TypeKind::Edit;
                base.value += (pointer ? "->" : ".") + fieldName(field->field);
            }
            return base;
        }
        if (const auto *index = std::get_if<FirIndexExpression>(&expression.value)) {
            return emitIndex(*index, expression.span, depth);
        }
        if (const auto *replace = std::get_if<FirReplaceExpression>(&expression.value)) {
            return emitReplace(*replace, expression.type, depth);
        }
        if (const auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
            return emitEnum(*constructor, depth);
        }
        return emitMatch(std::get<FirMatchExpression>(expression.value), expression.type,
                         expression.span, depth);
    }

    EmittedExpression emitMove(FirLocalId local, unsigned int depth) {
        const auto &type = function_.locals[local].type;
        const auto source = localValue(local);
        auto result = emitMoveValue(type, source, depth);
        if (taskPoll_ && typeRequiresDrop(program_, type)) {
            out_ << indentation(depth) << localActive(local) << " = false;\n";
        }
        return result;
    }

    EmittedExpression emitSpawn(const FirSpawnExpression &spawn, unsigned int depth) {
        const auto *call = std::get_if<FirCallExpression>(
            &function_.expressions[spawn.call].value);
        if (call == nullptr || call->kind != FirCallKind::Function ||
            call->function >= program_.functions.size() ||
            !program_.functions[call->function].task) {
            internalError("spawn did not lower to a task call");
        }
        std::vector<std::string> arguments;
        arguments.reserve(call->arguments.size());
        for (const auto argument : call->arguments) {
            auto value = emitExpression(argument, depth);
            if (value.diverges) {
                return value;
            }
            arguments.push_back(std::move(value.value));
        }
        const auto frame = nextTemporary();
        const auto task = nextTemporary();
        out_ << indentation(depth) << "struct " << taskFrameName(program_, call->function)
             << " *" << frame << " = fdn_alloc(sizeof(*" << frame << "));\n";
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            out_ << indentation(depth) << frame << "->fdn_arg_" << index << " = "
                 << arguments[index] << ";\n";
        }
        out_ << indentation(depth) << frame << "->fdn_arguments_active = true;\n";
        if (taskSuspensionCount(program_.functions[call->function]) != 0) {
            out_ << indentation(depth) << frame << "->fdn_state = 0;\n";
            for (std::size_t local = 0;
                 local < program_.functions[call->function].locals.size(); ++local) {
                if (typeRequiresDrop(program_,
                                     program_.functions[call->function].locals[local].type)) {
                    out_ << indentation(depth) << frame << "->fdn_local_" << local
                         << "_active = false;\n";
                }
            }
            for (std::size_t expression = 0;
                 expression < program_.functions[call->function].expressions.size();
                 ++expression) {
                if (std::holds_alternative<FirBlockingCallExpression>(
                        program_.functions[call->function].expressions[expression].value)) {
                    out_ << indentation(depth) << frame << "->"
                         << blockingJobName(expression) << " = NULL;\n";
                }
            }
        }
        if (program_.functions[call->function].returnType != voidType) {
            out_ << indentation(depth) << frame << "->fdn_result_active = false;\n";
        }
        out_ << indentation(depth) << "fdn_task *" << task << " = fdn_task_spawn(" << frame
             << ", &" << taskPollName(program_, call->function) << ", &"
             << taskMoveResultName(program_, call->function) << ", &"
             << taskDropFrameName(program_, call->function) << ");\n";
        return {task, false};
    }

    EmittedExpression emitTaskWait(const FirTaskWaitExpression &wait, const Type &type,
                                   unsigned int depth) {
        auto task = emitExpression(wait.task, depth);
        if (task.diverges) {
            return task;
        }
        if (type == voidType) {
            out_ << indentation(depth) << "fdn_task_wait(&" << task.value << ", NULL);\n";
            return {{}, false};
        }
        const auto result = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << result << ";\n";
        out_ << indentation(depth) << "fdn_task_wait(&" << task.value << ", &" << result
             << ");\n";
        return {result, false};
    }

    EmittedExpression emitChannel(const FirChannelExpression &channel, unsigned int depth) {
        auto capacity = emitExpression(channel.capacity, depth);
        if (capacity.diverges) {
            return capacity;
        }
        const auto result = nextTemporary();
        out_ << indentation(depth) << "fdn_channel_pair " << result << " = {NULL, NULL};\n";
        out_ << indentation(depth) << "fdn_channel_open(";
        if (channel.payload == voidType) {
            out_ << "0";
        } else {
            out_ << "sizeof(" << cType(channel.payload) << ')';
        }
        out_ << ", (size_t)" << capacity.value << ", ";
        if (channel.payload != voidType && typeRequiresDrop(program_, channel.payload)) {
            out_ << '&' << channelDropName(channel.payload);
        } else {
            out_ << "NULL";
        }
        out_ << ", &" << result
             << ".fdn_field_0, &" << result << ".fdn_field_1);\n";
        return {result, false};
    }

    EmittedExpression emitFunctionValue(const FirFunctionValueExpression &function,
                                        const Type &type, unsigned int depth) {
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
        out_ << indentation(depth) << temporary << ".fdn_env = NULL;\n";
        out_ << indentation(depth) << temporary << ".fdn_call = &"
             << functionAdapterName(program_, function.function) << ";\n";
        out_ << indentation(depth) << temporary << ".fdn_drop = NULL;\n";
        return {temporary, false};
    }

    EmittedExpression emitClosure(const FirClosureExpression &closure, const Type &type,
                                  unsigned int depth) {
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
        const auto &target = program_.functions[closure.function];
        if (closure.captures.empty()) {
            out_ << indentation(depth) << temporary << ".fdn_env = NULL;\n";
            out_ << indentation(depth) << temporary << ".fdn_drop = NULL;\n";
        } else {
            const auto environment = nextTemporary();
            out_ << indentation(depth) << "struct "
                 << closureEnvironmentName(program_, closure.function) << " *" << environment
                 << " = fdn_alloc(sizeof(*" << environment << "));\n";
            std::size_t captureLocal{};
            for (const auto &capture : closure.captures) {
                while (captureLocal < target.locals.size() &&
                       !target.locals[captureLocal].capture) {
                    ++captureLocal;
                }
                if (captureLocal >= target.locals.size()) {
                    internalError("closure capture has no environment field");
                }
                const auto field = "fdn_capture_" + std::to_string(captureLocal);
                const auto source = localValue(capture.local);
                if (capture.mode == FirCaptureMode::Own) {
                    emitMoveAssignment(out_, program_, function_.locals[capture.local].type,
                                       environment + "->" + field, source, depth);
                } else if (capture.mode == FirCaptureMode::View ||
                           capture.mode == FirCaptureMode::Edit) {
                    out_ << indentation(depth) << environment << "->" << field << " = &"
                         << source << ";\n";
                } else {
                    out_ << indentation(depth) << environment << "->" << field << " = "
                         << source << ";\n";
                }
                ++captureLocal;
            }
            out_ << indentation(depth) << temporary << ".fdn_env = " << environment << ";\n";
            out_ << indentation(depth) << temporary << ".fdn_drop = &"
                 << closureDropName(program_, closure.function) << ";\n";
        }
        out_ << indentation(depth) << temporary << ".fdn_call = &"
             << functionName(program_, closure.function) << ";\n";
        return {temporary, false};
    }

    EmittedExpression emitMoveValue(const Type &type, const std::string &source,
                                    unsigned int depth) {
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
        emitMoveAssignment(out_, program_, type, temporary, source, depth);
        return {temporary, false};
    }

    EmittedExpression emitOwnership(const FirOwnershipExpression &ownership, const Type &type,
                                    SourceSpan span, unsigned int depth) {
        const auto operand = emitExpression(ownership.operand, depth);
        if (operand.diverges) {
            return operand;
        }
        if (ownership.operation == FirOwnershipOperator::Own) {
            const auto temporary = nextTemporary();
            emitLocation(span, depth);
            out_ << indentation(depth) << cType(type) << ' ' << temporary
                 << " = fdn_alloc(sizeof(*" << temporary << "));\n";
            out_ << indentation(depth) << '*' << temporary << " = " << operand.value << ";\n";
            return {temporary, false};
        }

        const auto operandType = function_.expressions[ownership.operand].type;
        if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            if ((operandType.kind == TypeKind::View || operandType.kind == TypeKind::Edit) &&
                operandType.arguments.size() == 1 &&
                operandType.arguments.front().kind == TypeKind::Slice) {
                return operand;
            }
            auto arrayType = operandType;
            auto pointer = false;
            if (arrayType.kind == TypeKind::Own && arrayType.arguments.size() == 1) {
                const auto ownedArray = arrayType.arguments.front();
                arrayType = ownedArray;
                pointer = true;
            }
            const auto temporary = nextTemporary();
            out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
            out_ << indentation(depth) << temporary << ".fdn_data = " << operand.value
                 << (pointer ? "->fdn_data;\n" : ".fdn_data;\n");
            out_ << indentation(depth) << temporary << ".fdn_length = " << arrayType.declaration
                 << ";\n";
            return {temporary, false};
        }
        if (operandType.kind == TypeKind::Own || operandType.kind == TypeKind::View ||
            operandType.kind == TypeKind::Edit) {
            if (operandType.kind == TypeKind::Own && operandType.arguments.size() == 1 &&
                operandType.arguments.front().kind == TypeKind::Contract) {
                return {"*" + operand.value, false};
            }
            return operand;
        }
        return {"&" + operand.value, false};
    }

    EmittedExpression emitArray(const FirArrayExpression &array, const Type &type,
                                unsigned int depth) {
        std::vector<std::string> elements;
        elements.reserve(array.elements.size());
        for (const auto element : array.elements) {
            auto value = emitExpression(element, depth);
            if (value.diverges) {
                return value;
            }
            elements.push_back(std::move(value.value));
        }
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = {0};\n";
        for (std::size_t index = 0; index < elements.size(); ++index) {
            out_ << indentation(depth) << temporary << ".fdn_data[" << index
                 << "] = " << elements[index] << ";\n";
        }
        return {temporary, false};
    }

    EmittedExpression emitIndex(const FirIndexExpression &index, SourceSpan span,
                                unsigned int depth) {
        const auto base = emitExpression(index.base, depth);
        if (base.diverges) {
            return base;
        }
        const auto value = emitExpression(index.index, depth);
        if (value.diverges) {
            return value;
        }
        const auto &sourceType = function_.expressions[index.base].type;
        auto sequence = sourceType;
        auto access = base.value;
        if (sequence.kind == TypeKind::Own && sequence.arguments.size() == 1) {
            const auto ownedSequence = sequence.arguments.front();
            sequence = ownedSequence;
            access += "->fdn_data";
        } else if ((sequence.kind == TypeKind::View || sequence.kind == TypeKind::Edit) &&
                   sequence.arguments.size() == 1) {
            const auto borrowedSequence = sequence.arguments.front();
            sequence = borrowedSequence;
            access += ".fdn_data";
        } else {
            access += ".fdn_data";
        }
        const auto length = sequence.kind == TypeKind::Array
                                ? std::to_string(sequence.declaration)
                                : base.value + ".fdn_length";
        emitLocation(span, depth);
        const auto checked = nextTemporary();
        out_ << indentation(depth) << "size_t " << checked << " = fdn_bounds_check("
             << value.value << ", " << length << ");\n";
        return {access + '[' + checked + ']', false};
    }

    EmittedExpression emitReplace(const FirReplaceExpression &replace, const Type &type,
                                  unsigned int depth) {
        const auto value = emitExpression(replace.value, depth);
        if (value.diverges) {
            return value;
        }
        const auto target = emitExpression(replace.target, depth);
        if (target.diverges) {
            return target;
        }
        const auto previous = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << previous << ";\n";
        emitMoveAssignment(out_, program_, type, previous, target.value, depth);
        emitMoveAssignment(out_, program_, type, target.value, value.value, depth);
        return {previous, false};
    }

    EmittedExpression emitBinary(const FirBinaryExpression &binary, const Type &type,
                                 SourceSpan span, unsigned int depth) {
        const auto left = emitExpression(binary.left, depth);
        if (left.diverges) {
            return left;
        }
        if (binary.operation == FirBinaryOperator::And ||
            binary.operation == FirBinaryOperator::Or) {
            const auto temporary = nextTemporary();
            out_ << indentation(depth) << "bool " << temporary << " = " << left.value << ";\n";
            const auto condition =
                binary.operation == FirBinaryOperator::And ? temporary : "!" + temporary;
            out_ << indentation(depth) << "if (" << condition << ") {\n";
            const auto right = emitExpression(binary.right, depth + 1);
            if (!right.diverges) {
                out_ << indentation(depth + 1) << temporary << " = " << right.value << ";\n";
            }
            out_ << indentation(depth) << "}\n";
            return {temporary, false};
        }

        if (expressionDiverges(program_, function_, binary.right)) {
            discardValue(left, depth);
            return emitExpression(binary.right, depth);
        }
        const auto right = emitExpression(binary.right, depth);
        if (right.diverges) {
            return right;
        }
        const auto temporary = nextTemporary();
        if (binary.operation == FirBinaryOperator::Add ||
            binary.operation == FirBinaryOperator::Subtract ||
            binary.operation == FirBinaryOperator::Multiply ||
            binary.operation == FirBinaryOperator::Divide ||
            binary.operation == FirBinaryOperator::Remainder) {
            emitLocation(span, depth);
        }
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = ";
        switch (binary.operation) {
        case FirBinaryOperator::Add:
            if (type == stringType) {
                out_ << "fdn_string_concat(" << left.value << ", " << right.value << ')';
            } else if (type == u64Type) {
                out_ << "fdn_u64_add(" << left.value << ", " << right.value << ')';
            } else {
                out_ << "fdn_i32_add(" << left.value << ", " << right.value << ')';
            }
            break;
        case FirBinaryOperator::Subtract:
            out_ << (type == u64Type ? "fdn_u64_subtract(" : "fdn_i32_subtract(")
                 << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Multiply:
            out_ << (type == u64Type ? "fdn_u64_multiply(" : "fdn_i32_multiply(")
                 << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Divide:
            out_ << (type == u64Type ? "fdn_u64_divide(" : "fdn_i32_divide(")
                 << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Remainder:
            out_ << (type == u64Type ? "fdn_u64_remainder(" : "fdn_i32_remainder(")
                 << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Equal:
            if (function_.expressions[binary.left].type == stringType) {
                out_ << "fdn_string_equal(" << left.value << ", " << right.value << ')';
            } else {
                out_ << left.value << " == " << right.value;
            }
            break;
        case FirBinaryOperator::NotEqual:
            if (function_.expressions[binary.left].type == stringType) {
                out_ << "!fdn_string_equal(" << left.value << ", " << right.value << ')';
            } else {
                out_ << left.value << " != " << right.value;
            }
            break;
        case FirBinaryOperator::Less:
            out_ << left.value << " < " << right.value;
            break;
        case FirBinaryOperator::LessEqual:
            out_ << left.value << " <= " << right.value;
            break;
        case FirBinaryOperator::Greater:
            out_ << left.value << " > " << right.value;
            break;
        case FirBinaryOperator::GreaterEqual:
            out_ << left.value << " >= " << right.value;
            break;
        case FirBinaryOperator::And:
        case FirBinaryOperator::Or:
            break;
        }
        out_ << ";\n";
        dropInspectedTemporary(binary.left, left, depth);
        dropInspectedTemporary(binary.right, right, depth);
        return {temporary, false};
    }

    EmittedExpression emitCall(const FirCallExpression &call, const Type &type, SourceSpan span,
                               unsigned int depth) {
        std::vector<std::string> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            if (expressionDiverges(program_, function_, argument)) {
                for (const auto &value : arguments) {
                    discardValue({value, false}, depth);
                }
                return emitExpression(argument, depth);
            }
            auto emitted = emitExpression(argument, depth);
            if (emitted.diverges) {
                return emitted;
            }
            arguments.push_back(std::move(emitted.value));
        }

        emitLocation(span, depth);
        std::ostringstream invocation;
        if (call.kind == FirCallKind::Contract) {
            if (arguments.empty()) {
                internalError("contract call has no receiver");
            }
            const auto receiverType = function_.expressions[call.arguments.front()].type;
            const auto member = receiverType.kind == TypeKind::Own ? "->" : ".";
            invocation << arguments.front() << member << "fdn_vtable->fdn_method_" << call.method
                       << '(';
            invocation << arguments.front() << member << "fdn_data";
            for (std::size_t index = 1; index < arguments.size(); ++index) {
                invocation << ", " << arguments[index];
            }
            invocation << ')';
        } else if (call.kind == FirCallKind::FunctionValue) {
            const auto callable = localValue(call.local);
            const auto &localType = function_.locals[call.local].type;
            const auto pointer = localType.kind == TypeKind::View ||
                                 localType.kind == TypeKind::Edit;
            const auto member = pointer ? "->" : ".";
            invocation << callable << member << "fdn_call(" << callable << member
                       << "fdn_env";
            for (const auto &argument : arguments) {
                invocation << ", " << argument;
            }
            invocation << ')';
        } else if (call.kind == FirCallKind::Print) {
            invocation << "fdn_println";
        } else if (call.kind == FirCallKind::Panic) {
            invocation << "fdn_panic";
        } else if (call.kind == FirCallKind::Len) {
            if (arguments.size() != 1) {
                internalError("len call does not have one argument");
            }
            auto sequence = function_.expressions[call.arguments.front()].type;
            auto member = std::string{"."};
            if ((sequence.kind == TypeKind::View || sequence.kind == TypeKind::Edit) &&
                sequence.arguments.size() == 1) {
                if (sequence.arguments.front().kind != TypeKind::Slice) {
                    member = "->";
                }
                sequence = sequence.arguments.front();
            }
            if (sequence.kind == TypeKind::Array) {
                invocation << "UINT64_C(" << sequence.declaration << ')';
            } else if (sequence.kind == TypeKind::Slice) {
                invocation << "(uint64_t)" << arguments.front() << member << "fdn_length";
            } else if (sequence.kind == TypeKind::String) {
                invocation << "(uint64_t)" << arguments.front() << member << "length";
            } else {
                internalError("len call has an unsupported argument");
            }
        } else {
            invocation << functionName(program_, call.function);
        }
        if (call.kind != FirCallKind::Contract &&
            call.kind != FirCallKind::FunctionValue && call.kind != FirCallKind::Len) {
            invocation << '(';
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (index != 0) {
                    invocation << ", ";
                }
                invocation << arguments[index];
            }
            invocation << ')';
        }

        if (call.kind == FirCallKind::Panic ||
            (call.kind == FirCallKind::Function &&
             program_.functions[call.function].diverges)) {
            out_ << indentation(depth) << invocation.str() << ";\n";
            return {{}, true};
        }
        if (type == voidType) {
            out_ << indentation(depth) << invocation.str() << ";\n";
            if (contractCallConsumesReceiver(call)) {
                out_ << indentation(depth) << "fdn_dealloc(" << arguments.front() << ");\n";
                out_ << indentation(depth) << arguments.front() << " = NULL;\n";
            }
            for (std::size_t index = 0;
                 index < call.arguments.size() && index < call.argumentDrops.size(); ++index) {
                if (call.argumentDrops[index]) {
                    emitDropValue(out_, program_, function_.expressions[call.arguments[index]].type,
                                  arguments[index], depth);
                }
            }
            return {{}, false};
        }
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = "
             << invocation.str() << ";\n";
        if (contractCallConsumesReceiver(call)) {
            out_ << indentation(depth) << "fdn_dealloc(" << arguments.front() << ");\n";
            out_ << indentation(depth) << arguments.front() << " = NULL;\n";
        }
        return {temporary, false};
    }

    bool contractCallConsumesReceiver(const FirCallExpression &call) const {
        return call.kind == FirCallKind::Contract && call.contract < program_.contracts.size() &&
               call.method < program_.contracts[call.contract].methods.size() &&
               program_.contracts[call.contract].methods[call.method].receiver ==
                   FirReceiverKind::Own;
    }

    EmittedExpression emitContract(const FirContractExpression &contract, const Type &type,
                                   unsigned int depth) {
        const auto value = emitExpression(contract.value, depth);
        if (value.diverges) {
            return value;
        }
        const auto temporary = nextTemporary();
        if (type.kind == TypeKind::Own) {
            out_ << indentation(depth) << cType(type) << ' ' << temporary
                 << " = fdn_alloc(sizeof(*" << temporary << "));\n";
            out_ << indentation(depth) << temporary << "->fdn_data = (void *)" << value.value
                 << ";\n";
            out_ << indentation(depth) << temporary << "->fdn_vtable = &"
                 << vtableName(contract.contractType, contract.concreteType) << ";\n";
        } else {
            out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
            out_ << indentation(depth) << temporary << ".fdn_data = (void *)" << value.value
                 << ";\n";
            out_ << indentation(depth) << temporary << ".fdn_vtable = &"
                 << vtableName(contract.contractType, contract.concreteType) << ";\n";
        }
        return {temporary, false};
    }

    EmittedExpression emitStruct(const FirStructExpression &literal, unsigned int depth) {
        std::vector<std::string> values;
        values.reserve(literal.fields.size());
        for (const auto &field : literal.fields) {
            if (expressionDiverges(program_, function_, field.value)) {
                for (const auto &value : values) {
                    discardValue({value, false}, depth);
                }
                return emitExpression(field.value, depth);
            }
            auto value = emitExpression(field.value, depth);
            if (value.diverges) {
                return value;
            }
            values.push_back(std::move(value.value));
        }

        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(literal.type) << ' ' << temporary << " = {0};\n";
        if (literal.type.kind == TypeKind::Struct &&
            literal.type.declaration < program_.structs.size() &&
            program_.structs[literal.type.declaration].dropFunction.has_value()) {
            out_ << indentation(depth) << temporary << ".fdn_drop_active = true;\n";
        }
        for (std::size_t index = 0; index < literal.fields.size(); ++index) {
            out_ << indentation(depth) << temporary << '.'
                 << fieldName(literal.fields[index].field) << " = " << values[index] << ";\n";
        }
        return {temporary, false};
    }

    EmittedExpression emitEnum(const FirEnumExpression &constructor, unsigned int depth) {
        std::optional<std::string> payloadValue;
        if (constructor.payload.has_value()) {
            auto payload = emitExpression(*constructor.payload, depth);
            if (payload.diverges) {
                return payload;
            }
            payloadValue = std::move(payload.value);
        }

        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(constructor.type) << ' ' << temporary
             << " = {0};\n";
        if (payloadValue.has_value()) {
            out_ << indentation(depth) << temporary << ".fdn_data."
                 << payloadName(constructor.variant) << " = " << *payloadValue << ";\n";
        }
        out_ << indentation(depth) << temporary << ".fdn_tag = "
             << enumTag(constructor.type.declaration, constructor.variant) << ";\n";
        return {temporary, false};
    }

    EmittedExpression emitMatch(const FirMatchExpression &match, const Type &type, SourceSpan span,
                                unsigned int depth) {
        const auto value = emitExpression(match.value, depth);
        if (value.diverges) {
            return value;
        }
        const auto allDiverge =
            !match.arms.empty() &&
            std::all_of(match.arms.begin(), match.arms.end(), [&](const FirMatchArm &arm) {
                return expressionDiverges(program_, function_, arm.expression);
            });
        std::string temporary;
        if (type != voidType && !allDiverge) {
            temporary = nextTemporary();
            out_ << indentation(depth) << cType(type) << ' ' << temporary << ";\n";
        }
        out_ << indentation(depth) << "switch (" << value.value << ".fdn_tag) {\n";
        for (const auto &arm : match.arms) {
            out_ << indentation(depth) << "case "
                 << enumTag(match.type.declaration, arm.variant) << ": {\n";
            if (arm.binding.has_value()) {
                const auto local = *arm.binding;
                const auto payload = value.value + ".fdn_data." + payloadName(arm.variant);
                if (typeRequiresDrop(program_, function_.locals[local].type)) {
                    const auto moved = emitMoveValue(function_.locals[local].type, payload,
                                                     depth + 1);
                    out_ << indentation(depth + 1);
                    if (!taskPoll_) {
                        out_ << cType(function_.locals[local].type) << ' ';
                    }
                    out_ << localValue(local) << " = " << moved.value << ";\n";
                } else {
                    out_ << indentation(depth + 1);
                    if (!taskPoll_) {
                        out_ << cType(function_.locals[local].type) << ' ';
                    }
                    out_ << localValue(local) << " = " << payload << ";\n";
                }
                activateLocal(local, depth + 1);
                out_ << indentation(depth + 1) << "(void)" << localValue(local)
                     << ";\n";
            }
            const auto armValue = emitExpression(arm.expression, depth + 1);
            if (!armValue.diverges) {
                if (type != voidType) {
                    out_ << indentation(depth + 1) << temporary << " = " << armValue.value
                         << ";\n";
                }
                emitDrops(arm.drops, depth + 1);
                emitDropValue(out_, program_, function_.expressions[match.value].type, value.value,
                              depth + 1);
                out_ << indentation(depth + 1) << "break;\n";
            }
            out_ << indentation(depth) << "}\n";
        }
        out_ << indentation(depth) << "default:\n";
        emitLocation(span, depth + 1);
        out_ << indentation(depth + 1) << "fdn_invalid_enum_tag();\n";
        out_ << indentation(depth) << "}\n";
        return {allDiverge ? std::string{} : temporary, allDiverge};
    }

    bool emitSuspendingStatement(const FirStatement &statement, unsigned int depth) {
        if (const auto *selection = std::get_if<FirSelectStatement>(&statement.value)) {
            emitSelect(*selection, statement.span, depth);
            return true;
        }
        std::optional<FirLocalId> resultLocal;
        bool discarded = false;
        FirExpressionId expression;
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            expression = variable->initializer;
            resultLocal = variable->local;
        } else if (const auto *value = std::get_if<FirExpressionStatement>(&statement.value)) {
            expression = value->expression;
        } else if (const auto *value = std::get_if<FirDiscardStatement>(&statement.value)) {
            expression = value->expression;
            discarded = true;
        } else {
            return false;
        }
        const auto *blocking =
            std::get_if<FirBlockingCallExpression>(&function_.expressions[expression].value);
        if (blocking != nullptr) {
            if (blocking->arguments.size() != blocking->argumentStorages.size()) {
                internalError("blocking call has invalid argument storage");
            }
            for (std::size_t index = 0; index < blocking->arguments.size(); ++index) {
                const auto argument = emitExpression(blocking->arguments[index], depth);
                if (argument.diverges) {
                    return true;
                }
                out_ << indentation(depth) << localValue(blocking->argumentStorages[index])
                     << " = " << argument.value << ";\n";
            }

            const auto state = ++taskState_;
            out_ << indentation(depth) << "fdn_frame->fdn_state = " << state << ";\n";
            out_ << "fdn_task_state_" << state << ":\n";
            emitLocation(function_.expressions[expression].span, depth);
            out_ << indentation(depth) << "if (!fdn_blocking_poll(&fdn_frame->"
                 << blockingJobName(expression) << ", fdn_frame, &"
                 << blockingWorkName(program_, functionId_, expression) << ")) {\n";
            out_ << indentation(depth + 1)
                 << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
            out_ << indentation(depth + 1) << "fdn_frame_leave(&fdn_frame_current);\n";
            out_ << indentation(depth + 1) << "return FDN_TASK_PENDING;\n";
            out_ << indentation(depth) << "}\n";

            if (blocking->resultStorage.has_value()) {
                const auto storage = *blocking->resultStorage;
                const auto &type = function_.locals[storage].type;
                if (resultLocal.has_value()) {
                    emitMoveAssignment(out_, program_, type, localValue(*resultLocal),
                                       localValue(storage), depth);
                    if (typeRequiresDrop(program_, type)) {
                        out_ << indentation(depth) << localActive(storage) << " = false;\n";
                    }
                    activateLocal(*resultLocal, depth);
                    out_ << indentation(depth) << "(void)" << localValue(*resultLocal)
                         << ";\n";
                } else if (discarded) {
                    emitDropValue(out_, program_, type, localValue(storage), depth);
                    if (typeRequiresDrop(program_, type)) {
                        out_ << indentation(depth) << localActive(storage) << " = false;\n";
                    }
                } else {
                    out_ << indentation(depth) << "(void)" << localValue(storage) << ";\n";
                }
            }
            return true;
        }
        const auto *wait =
            std::get_if<FirTaskWaitExpression>(&function_.expressions[expression].value);
        if (wait != nullptr) {
            const auto *task =
                std::get_if<FirMoveExpression>(&function_.expressions[wait->task].value);
            if (task == nullptr) {
                internalError("suspending task wait does not consume a local handle");
            }
            if (!resultLocal.has_value() && function_.expressions[expression].type != voidType) {
                internalError("standalone suspending task wait has a result");
            }

            const auto state = ++taskState_;
            out_ << indentation(depth) << "fdn_frame->fdn_state = " << state << ";\n";
            out_ << "fdn_task_state_" << state << ":\n";
            emitLocation(function_.expressions[expression].span, depth);
            out_ << indentation(depth) << "if (!fdn_task_poll_wait(&"
                 << localValue(task->local) << ", ";
            if (resultLocal.has_value()) {
                out_ << '&' << localValue(*resultLocal);
            } else {
                out_ << "NULL";
            }
            out_ << ")) {\n";
            out_ << indentation(depth + 1)
                 << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
            out_ << indentation(depth + 1) << "fdn_frame_leave(&fdn_frame_current);\n";
            out_ << indentation(depth + 1) << "return FDN_TASK_PENDING;\n";
            out_ << indentation(depth) << "}\n";
            if (typeRequiresDrop(program_, function_.locals[task->local].type)) {
                out_ << indentation(depth) << localActive(task->local) << " = false;\n";
            }
            if (resultLocal.has_value()) {
                activateLocal(*resultLocal, depth);
                out_ << indentation(depth) << "(void)" << localValue(*resultLocal) << ";\n";
            }
            static_cast<void>(discarded);
            return true;
        }

        const auto *send =
            std::get_if<FirChannelSendExpression>(&function_.expressions[expression].value);
        const auto *receive =
            std::get_if<FirChannelReceiveExpression>(&function_.expressions[expression].value);
        if (send == nullptr && receive == nullptr) {
            return false;
        }

        const auto resultStorage =
            send != nullptr ? send->resultStorage : receive->resultStorage;
        const auto &resultType = function_.locals[resultStorage].type;
        if (resultType.kind != TypeKind::Enum || resultType.declaration >= program_.enums.size() ||
            program_.enums[resultType.declaration].variants.size() != 2 ||
            !program_.enums[resultType.declaration].variants[1].payload.has_value()) {
            internalError("channel operation has an invalid Result type");
        }
        const auto errorType =
            *program_.enums[resultType.declaration].variants[1].payload;
        if (errorType.kind != TypeKind::Enum || errorType.declaration >= program_.enums.size() ||
            program_.enums[errorType.declaration].variants.size() < 2) {
            internalError("channel operation has an invalid ChannelError type");
        }

        if (send != nullptr && send->value.has_value()) {
            if (!send->valueStorage.has_value()) {
                internalError("channel send is missing persistent value storage");
            }
            const auto value = emitExpression(*send->value, depth);
            if (!value.diverges) {
                emitMoveAssignment(out_, program_,
                                   function_.locals[*send->valueStorage].type,
                                   localValue(*send->valueStorage), value.value, depth);
                activateLocal(*send->valueStorage, depth);
            }
        }

        const auto state = ++taskState_;
        out_ << indentation(depth) << "fdn_frame->fdn_state = " << state << ";\n";
        out_ << "fdn_task_state_" << state << ":\n";
        emitLocation(function_.expressions[expression].span, depth);
        const auto status = nextTemporary();
        out_ << indentation(depth) << "fdn_channel_status " << status << " = ";
        if (send != nullptr) {
            out_ << "fdn_channel_poll_send(" << localValue(send->sender) << ", ";
            if (send->valueStorage.has_value()) {
                out_ << '&' << localValue(*send->valueStorage);
            } else {
                out_ << "NULL";
            }
        } else {
            out_ << "fdn_channel_poll_receive(" << localValue(receive->receiver) << ", ";
            if (program_.enums[resultType.declaration].variants[0].payload.has_value()) {
                out_ << '&' << localValue(resultStorage) << ".fdn_data."
                     << payloadName(0);
            } else {
                out_ << "NULL";
            }
        }
        out_ << ");\n";
        out_ << indentation(depth) << "if (" << status << " == FDN_CHANNEL_PENDING) {\n";
        out_ << indentation(depth + 1)
             << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
        out_ << indentation(depth + 1) << "fdn_frame_leave(&fdn_frame_current);\n";
        out_ << indentation(depth + 1) << "return FDN_TASK_PENDING;\n";
        out_ << indentation(depth) << "}\n";

        if (send != nullptr && send->valueStorage.has_value()) {
            const auto storage = *send->valueStorage;
            if (typeRequiresDrop(program_, function_.locals[storage].type)) {
                out_ << indentation(depth) << "if (" << status
                     << " == FDN_CHANNEL_READY) {\n";
                out_ << indentation(depth + 1) << localActive(storage) << " = false;\n";
                out_ << indentation(depth) << "} else {\n";
                emitLocalDrop(storage, depth + 1);
                out_ << indentation(depth) << "}\n";
            }
        }

        out_ << indentation(depth) << "if (" << status << " == FDN_CHANNEL_READY) {\n";
        out_ << indentation(depth + 1) << localValue(resultStorage) << ".fdn_tag = "
             << enumTag(resultType.declaration, 0) << ";\n";
        out_ << indentation(depth) << "} else {\n";
        out_ << indentation(depth + 1) << localValue(resultStorage) << ".fdn_data."
             << payloadName(1) << ".fdn_tag = (" << status
             << " == FDN_CHANNEL_CLOSED ? " << enumTag(errorType.declaration, 0) << " : "
             << enumTag(errorType.declaration, 1) << ");\n";
        out_ << indentation(depth + 1) << localValue(resultStorage) << ".fdn_tag = "
             << enumTag(resultType.declaration, 1) << ";\n";
        out_ << indentation(depth) << "}\n";
        activateLocal(resultStorage, depth);

        if (resultLocal.has_value()) {
            emitMoveAssignment(out_, program_, resultType, localValue(*resultLocal),
                               localValue(resultStorage), depth);
            if (typeRequiresDrop(program_, resultType)) {
                out_ << indentation(depth) << localActive(resultStorage) << " = false;\n";
            }
            activateLocal(*resultLocal, depth);
            out_ << indentation(depth) << "(void)" << localValue(*resultLocal) << ";\n";
        } else if (discarded) {
            if (typeRequiresDrop(program_, resultType)) {
                emitLocalDrop(resultStorage, depth);
            } else {
                out_ << indentation(depth) << "(void)" << localValue(resultStorage) << ";\n";
            }
        } else {
            internalError("channel Result is neither bound nor discarded");
        }
        return true;
    }

    void emitSelectSendCleanup(const FirSelectStatement &selection,
                               std::optional<std::size_t> selected,
                               unsigned int depth) {
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            if (!arm.send || !arm.valueStorage.has_value()) {
                continue;
            }
            const auto storage = *arm.valueStorage;
            if (!typeRequiresDrop(program_, function_.locals[storage].type)) {
                continue;
            }
            if (selected.has_value() && *selected == index) {
                out_ << indentation(depth) << localActive(storage) << " = false;\n";
            } else {
                emitLocalDrop(storage, depth);
            }
        }
    }

    void emitSelect(const FirSelectStatement &selection, SourceSpan span,
                    unsigned int depth) {
        for (const auto &arm : selection.operations) {
            if (!arm.send || !arm.value.has_value()) {
                continue;
            }
            if (!arm.valueStorage.has_value()) {
                internalError("select send is missing persistent value storage");
            }
            const auto value = emitExpression(*arm.value, depth);
            if (!value.diverges) {
                emitMoveAssignment(out_, program_,
                                   function_.locals[*arm.valueStorage].type,
                                   localValue(*arm.valueStorage), value.value, depth);
                activateLocal(*arm.valueStorage, depth);
            }
        }

        const auto now = nextTemporary();
        if (selection.timeout.has_value()) {
            out_ << indentation(depth) << "uint64_t " << now
                 << " = fdn_monotonic_nanoseconds();\n";
            out_ << indentation(depth) << localValue(selection.deadlineStorage) << " = ";
            if (selection.timeout->nanoseconds == UINT64_MAX) {
                out_ << "UINT64_MAX;\n";
            } else {
                out_ << "UINT64_C(" << selection.timeout->nanoseconds << ") > UINT64_MAX - "
                     << now << " ? UINT64_MAX : " << now << " + UINT64_C("
                     << selection.timeout->nanoseconds << ");\n";
            }
        } else {
            out_ << indentation(depth) << localValue(selection.deadlineStorage)
                 << " = UINT64_MAX;\n";
        }

        const auto state = ++taskState_;
        out_ << indentation(depth) << "fdn_frame->fdn_state = " << state << ";\n";
        out_ << "fdn_task_state_" << state << ":\n";
        emitLocation(span, depth);
        const auto cases = nextTemporary();
        out_ << indentation(depth) << "fdn_channel_select_case " << cases << '['
             << selection.operations.size() << "];\n";
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            out_ << indentation(depth) << cases << '[' << index << "].channel = "
                 << localValue(arm.endpoint) << ";\n";
            out_ << indentation(depth) << cases << '[' << index << "].kind = "
                 << (arm.send ? "FDN_CHANNEL_SELECT_SEND" : "FDN_CHANNEL_SELECT_RECEIVE")
                 << ";\n";
            out_ << indentation(depth) << cases << '[' << index << "].value = ";
            if (arm.send) {
                if (arm.valueStorage.has_value()) {
                    out_ << '&' << localValue(*arm.valueStorage);
                } else {
                    out_ << "NULL";
                }
            } else {
                const auto &resultType = function_.locals[arm.resultStorage].type;
                if (resultType.kind == TypeKind::Enum &&
                    resultType.declaration < program_.enums.size() &&
                    program_.enums[resultType.declaration].variants[0].payload.has_value()) {
                    out_ << '&' << localValue(arm.resultStorage) << ".fdn_data."
                         << payloadName(0);
                } else {
                    out_ << "NULL";
                }
            }
            out_ << ";\n";
        }
        const auto selected = nextTemporary();
        const auto status = nextTemporary();
        out_ << indentation(depth) << "size_t " << selected << " = SIZE_MAX;\n";
        out_ << indentation(depth) << "fdn_channel_status " << status
             << " = fdn_channel_poll_select(fdn_frame, " << cases << ", "
             << selection.operations.size() << ", "
             << localValue(selection.deadlineStorage) << ", &" << selected << ");\n";
        out_ << indentation(depth) << "if (" << status << " == FDN_CHANNEL_PENDING) {\n";
        out_ << indentation(depth + 1)
             << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
        out_ << indentation(depth + 1) << "fdn_frame_leave(&fdn_frame_current);\n";
        out_ << indentation(depth + 1) << "return FDN_TASK_PENDING;\n";
        out_ << indentation(depth) << "}\n";

        out_ << indentation(depth) << "if (" << status << " == FDN_CHANNEL_TIMEOUT) {\n";
        emitSelectSendCleanup(selection, std::nullopt, depth + 1);
        if (selection.timeout.has_value()) {
            static_cast<void>(emitBlock(selection.timeout->body, depth + 1));
        } else {
            out_ << indentation(depth + 1)
                 << "fdn_panic_cstr(\"select reached an unavailable timeout\");\n";
        }
        out_ << indentation(depth) << "} else if (" << status
             << " != FDN_CHANNEL_READY) {\n";
        const auto &errorType = function_.locals[selection.errorLocal].type;
        out_ << indentation(depth + 1) << localValue(selection.errorLocal)
             << ".fdn_tag = (" << status << " == FDN_CHANNEL_CLOSED ? "
             << enumTag(errorType.declaration, 0) << " : "
             << enumTag(errorType.declaration, 1) << ");\n";
        emitSelectSendCleanup(selection, std::nullopt, depth + 1);
        static_cast<void>(emitBlock(selection.errorBlock, depth + 1));
        out_ << indentation(depth) << "} else {\n";
        out_ << indentation(depth + 1) << "switch (" << selected << ") {\n";
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            out_ << indentation(depth + 1) << "case " << index << ": {\n";
            if (arm.binding.has_value()) {
                const auto binding = *arm.binding;
                const auto payload = localValue(arm.resultStorage) + ".fdn_data." +
                                     payloadName(0);
                emitMoveAssignment(out_, program_, function_.locals[binding].type,
                                   localValue(binding), payload, depth + 2);
                activateLocal(binding, depth + 2);
            }
            emitSelectSendCleanup(selection, index, depth + 2);
            const auto exits = emitBlock(arm.body, depth + 2);
            if (!exits) {
                out_ << indentation(depth + 2) << "break;\n";
            }
            out_ << indentation(depth + 1) << "}\n";
        }
        out_ << indentation(depth + 1) << "default:\n";
        out_ << indentation(depth + 2) << "fdn_panic_cstr(\"invalid select branch\");\n";
        out_ << indentation(depth + 1) << "}\n";
        out_ << indentation(depth) << "}\n";
    }

    bool emitStatement(const FirStatement &statement, unsigned int depth) {
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            const auto initializer = emitExpression(variable->initializer, depth);
            if (initializer.diverges) {
                return true;
            }
            out_ << indentation(depth);
            if (!taskPoll_) {
                out_ << cType(function_.locals[variable->local].type) << ' ';
            }
            out_ << localValue(variable->local) << " = " << initializer.value << ";\n";
            activateLocal(variable->local, depth);
            out_ << indentation(depth) << "(void)" << localValue(variable->local)
                 << ";\n";
            return false;
        }
        if (const auto *binding = std::get_if<FirLetElseStatement>(&statement.value)) {
            const auto initializer = emitExpression(binding->initializer, depth);
            if (initializer.diverges) {
                return true;
            }
            const auto resultType = function_.expressions[binding->initializer].type;
            out_ << indentation(depth) << "if (" << initializer.value << ".fdn_tag == "
                 << enumTag(resultType.declaration, 1) << ") {\n";
            out_ << indentation(depth + 1);
            if (!taskPoll_) {
                out_ << cType(function_.locals[binding->errorLocal].type) << ' ';
            }
            out_ << localValue(binding->errorLocal) << " = " << initializer.value
                 << ".fdn_data." << payloadName(1) << ";\n";
            activateLocal(binding->errorLocal, depth + 1);
            static_cast<void>(emitBlock(binding->elseBlock, depth + 1));
            out_ << indentation(depth) << "}\n";
            out_ << indentation(depth);
            if (!taskPoll_) {
                out_ << cType(function_.locals[binding->local].type) << ' ';
            }
            out_ << localValue(binding->local) << " = " << initializer.value
                 << ".fdn_data." << payloadName(0) << ";\n";
            activateLocal(binding->local, depth);
            return false;
        }
        if (const auto *destructure =
                std::get_if<FirStructDestructureStatement>(&statement.value)) {
            const auto initializer = emitExpression(destructure->initializer, depth);
            if (initializer.diverges) {
                return true;
            }
            const auto access = initializer.value + (destructure->owned ? "->" : ".");
            for (const auto &binding : destructure->bindings) {
                const auto &type = function_.locals[binding.local].type;
                const auto moved = emitMoveValue(type, access + fieldName(binding.field), depth);
                out_ << indentation(depth);
                if (!taskPoll_) {
                    out_ << cType(type) << ' ';
                }
                out_ << localValue(binding.local) << " = " << moved.value << ";\n";
                activateLocal(binding.local, depth);
                out_ << indentation(depth) << "(void)" << localValue(binding.local) << ";\n";
            }
            emitDropValue(out_, program_, function_.expressions[destructure->initializer].type,
                          initializer.value, depth);
            return false;
        }
        if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            const auto value = emitExpression(assignment->value, depth);
            if (value.diverges) {
                return true;
            }
            const auto target = emitExpression(assignment->target, depth);
            if (target.diverges) {
                return true;
            }
            const auto *targetLocal =
                std::get_if<FirLocalExpression>(&function_.expressions[assignment->target].value);
            if (taskPoll_ && targetLocal != nullptr) {
                emitLocalDrop(targetLocal->local, depth);
            } else {
                emitDropValue(out_, program_, function_.expressions[assignment->target].type,
                              target.value, depth);
            }
            out_ << indentation(depth) << target.value << " = " << value.value << ";\n";
            if (targetLocal != nullptr) {
                activateLocal(targetLocal->local, depth);
            }
            return false;
        }
        if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            const auto value = emitExpression(expression->expression, depth);
            if (value.diverges) {
                return true;
            }
            if (!value.value.empty()) {
                out_ << indentation(depth) << "(void)" << value.value << ";\n";
            }
            return false;
        }
        if (const auto *discarded = std::get_if<FirDiscardStatement>(&statement.value)) {
            const auto value = emitExpression(discarded->expression, depth);
            if (value.diverges) {
                return true;
            }
            const auto &type = function_.expressions[discarded->expression].type;
            if (typeRequiresDrop(program_, type)) {
                emitDropValue(out_, program_, type, value.value, depth);
            } else if (!value.value.empty()) {
                out_ << indentation(depth) << "(void)" << value.value << ";\n";
            }
            return false;
        }
        if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
            if (returned->value.has_value()) {
                const auto value = emitExpression(*returned->value, depth);
                if (value.diverges) {
                    return true;
                }
                auto result = value.value;
                if (!returned->drops.empty()) {
                    result = nextTemporary();
                    out_ << indentation(depth)
                         << cType(function_.expressions[*returned->value].type) << ' ' << result
                         << " = " << value.value << ";\n";
                }
                emitDrops(returned->drops, depth);
                if (taskPoll_) {
                    emitMoveAssignment(out_, program_, function_.returnType,
                                       "fdn_frame->fdn_result", result, depth);
                    out_ << indentation(depth) << "fdn_frame->fdn_result_active = true;\n";
                    out_ << indentation(depth)
                         << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
                    out_ << indentation(depth) << "fdn_frame_leave(&fdn_frame_current);\n";
                    out_ << indentation(depth) << "return FDN_TASK_READY;\n";
                    return true;
                }
                out_ << indentation(depth) << "fdn_frame_leave(&fdn_frame_current);\n";
                out_ << indentation(depth) << "return " << result << ";\n";
            } else {
                emitDrops(returned->drops, depth);
                if (taskPoll_) {
                    out_ << indentation(depth)
                         << "fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
                    out_ << indentation(depth) << "fdn_frame_leave(&fdn_frame_current);\n";
                    out_ << indentation(depth) << "return FDN_TASK_READY;\n";
                    return true;
                }
                out_ << indentation(depth) << "fdn_frame_leave(&fdn_frame_current);\n";
                out_ << indentation(depth) << "return;\n";
            }
            return true;
        }
        if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            const auto condition = emitExpression(branch->condition, depth);
            if (condition.diverges) {
                return true;
            }
            out_ << indentation(depth) << "if (" << condition.value << ") {\n";
            const auto thenExits = emitBlock(branch->thenBlock, depth + 1);
            out_ << indentation(depth) << '}';
            auto elseExits = false;
            if (branch->elseBlock.has_value()) {
                out_ << " else {\n";
                elseExits = emitBlock(*branch->elseBlock, depth + 1);
                out_ << indentation(depth) << '}';
            }
            out_ << '\n';
            return branch->elseBlock.has_value() && thenExits && elseExits;
        }

        const auto &loop = std::get<FirWhileStatement>(statement.value);
        out_ << indentation(depth) << "while (true) {\n";
        const auto condition = emitExpression(loop.condition, depth + 1);
        if (condition.diverges) {
            out_ << indentation(depth) << "}\n";
            return true;
        }
        out_ << indentation(depth + 1) << "if (!" << condition.value << ") {\n";
        out_ << indentation(depth + 2) << "break;\n";
        out_ << indentation(depth + 1) << "}\n";
        static_cast<void>(emitBlock(loop.body, depth + 1));
        out_ << indentation(depth) << "}\n";
        return false;
    }

    void emitLocation(SourceSpan span, unsigned int depth) {
        out_ << indentation(depth) << "fdn_frame_location(&fdn_frame_current, " << span.line
             << ", " << span.column << ");\n";
    }

    void emitDrops(const std::vector<FirLocalId> &drops, unsigned int depth) {
        for (const auto local : drops) {
            emitLocalDrop(local, depth);
        }
    }

    std::string localValue(FirLocalId local) const {
        const auto &declaration = function_.locals[local];
        if (!declaration.capture) {
            return taskPoll_ ? "fdn_frame->fdn_local_" + std::to_string(local)
                             : localName(function_, local);
        }
        const auto field = "fdn_env_data->fdn_capture_" + std::to_string(local);
        if (declaration.captureMode == FirCaptureMode::View ||
            declaration.captureMode == FirCaptureMode::Edit) {
            return "(*" + field + ')';
        }
        return field;
    }

    std::string localActive(FirLocalId local) const {
        return "fdn_frame->fdn_local_" + std::to_string(local) + "_active";
    }

    void activateLocal(FirLocalId local, unsigned int depth) {
        if (taskPoll_ && typeRequiresDrop(program_, function_.locals[local].type)) {
            out_ << indentation(depth) << localActive(local) << " = true;\n";
        }
    }

    void emitLocalDrop(FirLocalId local, unsigned int depth) {
        const auto &type = function_.locals[local].type;
        if (!taskPoll_ || !typeRequiresDrop(program_, type)) {
            emitDropValue(out_, program_, type, localValue(local), depth);
            return;
        }
        out_ << indentation(depth) << "if (" << localActive(local) << ") {\n";
        emitDropValue(out_, program_, type, localValue(local), depth + 1);
        out_ << indentation(depth + 1) << localActive(local) << " = false;\n";
        out_ << indentation(depth) << "}\n";
    }

    void discardValue(const EmittedExpression &expression, unsigned int depth) {
        if (!expression.value.empty()) {
            out_ << indentation(depth) << "(void)" << expression.value << ";\n";
        }
    }

    bool isPlaceExpression(FirExpressionId id) const {
        const auto &expression = function_.expressions[id].value;
        if (std::holds_alternative<FirLocalExpression>(expression)) {
            return true;
        }
        if (const auto *field = std::get_if<FirFieldExpression>(&expression)) {
            return isPlaceExpression(field->base);
        }
        if (const auto *index = std::get_if<FirIndexExpression>(&expression)) {
            return isPlaceExpression(index->base);
        }
        return false;
    }

    void dropInspectedTemporary(FirExpressionId id, const EmittedExpression &expression,
                                unsigned int depth) {
        const auto &type = function_.expressions[id].type;
        if (!isPlaceExpression(id) && typeRequiresDrop(program_, type)) {
            emitDropValue(out_, program_, type, expression.value, depth);
        }
    }

    std::string nextTemporary() { return "fdn_tmp_" + std::to_string(temporary_++); }

    std::ostringstream &out_;
    const FirProgram &program_;
    const FirFunction &function_;
    FirFunctionId functionId_{};
    std::size_t temporary_{};
    std::size_t taskState_{};
    bool taskPoll_{};
};

void emitStructDefinition(std::ostringstream &out, const FirProgram &program, FirStructId id) {
    out << "struct fdn_struct_" << id << " {\n";
    if (program.structs[id].dropFunction.has_value()) {
        out << "    bool fdn_drop_active;\n";
    }
    if (program.structs[id].fields.empty()) {
        out << "    uint8_t fdn_unit;\n";
    }
    for (std::size_t field = 0; field < program.structs[id].fields.size(); ++field) {
        out << "    " << cType(program.structs[id].fields[field].type) << ' ' << fieldName(field)
            << ";\n";
    }
    out << "};\n\n";
}

void emitEnumDefinition(std::ostringstream &out, const FirProgram &program, FirEnumId id) {
    out << "enum {\n";
    for (std::size_t variant = 0; variant < program.enums[id].variants.size(); ++variant) {
        out << "    " << enumTag(id, variant) << " = " << variant;
        out << (variant + 1 == program.enums[id].variants.size() ? "\n" : ",\n");
    }
    out << "};\n";
    out << "struct fdn_enum_" << id << " {\n";
    out << "    int32_t fdn_tag;\n";
    bool hasPayload = false;
    for (const auto &variant : program.enums[id].variants) {
        hasPayload = hasPayload || variant.payload.has_value();
    }
    if (hasPayload) {
        out << "    union {\n";
        for (std::size_t variant = 0; variant < program.enums[id].variants.size(); ++variant) {
            if (program.enums[id].variants[variant].payload.has_value()) {
                out << "        " << cType(*program.enums[id].variants[variant].payload) << ' '
                    << payloadName(variant) << ";\n";
            }
        }
        out << "    } fdn_data;\n";
    }
    out << "};\n\n";
}

void emitContractDefinition(std::ostringstream &out, const FirProgram &program,
                            FirContractId id) {
    out << "struct fdn_contract_" << id << "_vtable {\n";
    out << "    void (*fdn_drop)(void *);\n";
    for (std::size_t method = 0; method < program.contracts[id].methods.size(); ++method) {
        const auto &declaration = program.contracts[id].methods[method];
        out << "    " << cType(declaration.returnType) << " (*fdn_method_" << method
            << ")(void *";
        for (const auto &parameter : declaration.parameters) {
            out << ", " << cType(parameter);
        }
        out << ");\n";
    }
    out << "};\n";
    out << "struct fdn_contract_" << id << " {\n";
    out << "    void *fdn_data;\n";
    out << "    const struct fdn_contract_" << id << "_vtable *fdn_vtable;\n";
    out << "};\n\n";
}

void collectFunctionType(const Type &type, std::unordered_map<std::string, Type> &functions) {
    if (type.kind == TypeKind::Function) {
        functions.emplace(typeKey(type), type);
    }
    for (const auto &argument : type.arguments) {
        collectFunctionType(argument, functions);
    }
}

std::vector<Type> collectFunctionTypes(const FirProgram &program) {
    std::unordered_map<std::string, Type> found;
    for (const auto &type : program.structs) {
        for (const auto &field : type.fields) {
            collectFunctionType(field.type, found);
        }
    }
    for (const auto &type : program.enums) {
        for (const auto &variant : type.variants) {
            if (variant.payload.has_value()) {
                collectFunctionType(*variant.payload, found);
            }
        }
    }
    for (const auto &function : program.functions) {
        collectFunctionType(function.returnType, found);
        for (const auto &local : function.locals) {
            collectFunctionType(local.type, found);
        }
        for (const auto &expression : function.expressions) {
            collectFunctionType(expression.type, found);
        }
    }
    std::vector<Type> result;
    result.reserve(found.size());
    for (auto &[key, type] : found) {
        static_cast<void>(key);
        result.push_back(std::move(type));
    }
    std::sort(result.begin(), result.end(), [](const Type &left, const Type &right) {
        return typeKey(left) < typeKey(right);
    });
    return result;
}

void emitFunctionTypeDefinition(std::ostringstream &out, const Type &type) {
    if (type.arguments.empty()) {
        internalError("function type has no result");
    }
    out << "struct " << cType(type) << " {\n";
    out << "    void *fdn_env;\n";
    out << "    " << cType(type.arguments.front()) << " (*fdn_call)(void *";
    for (std::size_t index = 1; index < type.arguments.size(); ++index) {
        out << ", " << cType(type.arguments[index]);
    }
    out << ");\n";
    out << "    void (*fdn_drop)(void *);\n";
    out << "};\n\n";
}

void emitClosureEnvironmentDefinition(std::ostringstream &out, const FirProgram &program,
                                      FirFunctionId id) {
    const auto &function = program.functions[id];
    if (!function.closure ||
        std::none_of(function.locals.begin(), function.locals.end(),
                     [](const FirLocal &local) { return local.capture; })) {
        return;
    }
    out << "struct " << closureEnvironmentName(program, id) << " {\n";
    for (std::size_t local = 0; local < function.locals.size(); ++local) {
        const auto &capture = function.locals[local];
        if (!capture.capture) {
            continue;
        }
        out << "    ";
        if (capture.captureMode == FirCaptureMode::View) {
            out << "const ";
        }
        out << cType(capture.type);
        if (capture.captureMode == FirCaptureMode::View ||
            capture.captureMode == FirCaptureMode::Edit) {
            out << " *";
        } else {
            out << ' ';
        }
        out << "fdn_capture_" << local << ";\n";
    }
    out << "};\n\n";
}

void emitClosureDrop(std::ostringstream &out, const FirProgram &program, FirFunctionId id) {
    const auto &function = program.functions[id];
    if (!function.closure ||
        std::none_of(function.locals.begin(), function.locals.end(),
                     [](const FirLocal &local) { return local.capture; })) {
        return;
    }
    const auto environment = closureEnvironmentName(program, id);
    out << "static void " << closureDropName(program, id) << "(void *fdn_raw) {\n";
    out << "    struct " << environment << " *fdn_env = (struct " << environment
        << " *)fdn_raw;\n";
    for (std::size_t local = function.locals.size(); local-- > 0;) {
        const auto &capture = function.locals[local];
        if (!capture.capture || capture.captureMode == FirCaptureMode::View ||
            capture.captureMode == FirCaptureMode::Edit) {
            continue;
        }
        emitDropValue(out, program, capture.type,
                      "fdn_env->fdn_capture_" + std::to_string(local), 1);
    }
    out << "    fdn_dealloc(fdn_env);\n";
    out << "}\n\n";
}

struct ContractUse {
    Type contract{invalidType};
    Type concrete{invalidType};
    std::vector<FirContractMethodTarget> methods;
};

std::string contractTargetData(const FirProgram &program, const ContractUse &use,
                               const FirContractMethodTarget &target) {
    auto data = std::string("fdn_data");
    auto type = use.concrete;
    for (const auto field : target.delegatePath) {
        if (type.kind != TypeKind::Struct || type.declaration >= program.structs.size() ||
            field >= program.structs[type.declaration].fields.size()) {
            internalError("contract delegation path is invalid");
        }
        data = "&((" + cType(type) + " *)" + data + ")->" + fieldName(field);
        type = program.structs[type.declaration].fields[field].type;
    }
    return data;
}

std::size_t contractMethodIndex(const FirContract &contract, std::string_view name) {
    for (std::size_t index = 0; index < contract.methods.size(); ++index) {
        if (contract.methods[index].name == name) {
            return index;
        }
    }
    internalError("default contract method is missing from the effective contract");
}

std::string defaultSelfVtableName(std::string_view table, std::size_t method) {
    return std::string(table) + "_m" + std::to_string(method) + "_self";
}

void emitDefaultSelfVtable(std::ostringstream &out, const FirProgram &program,
                           const ContractUse &use, const FirContractMethodTarget &target,
                           std::string_view table, std::size_t methodIndex) {
    if (target.defaultContract.kind != TypeKind::Contract ||
        target.defaultContract.declaration >= program.contracts.size()) {
        internalError("default method contract is invalid");
    }
    const auto &effective = program.contracts[use.contract.declaration];
    const auto &origin = program.contracts[target.defaultContract.declaration];
    const auto selfTable = defaultSelfVtableName(table, methodIndex);
    for (std::size_t method = 0; method < origin.methods.size(); ++method) {
        const auto &declaration = origin.methods[method];
        const auto effectiveMethod = contractMethodIndex(effective, declaration.name);
        out << "static " << cType(declaration.returnType) << ' ' << selfTable << "_m" << method
            << "(void *fdn_data";
        for (std::size_t parameter = 0; parameter < declaration.parameters.size(); ++parameter) {
            out << ", " << cType(declaration.parameters[parameter]) << " fdn_arg_" << parameter;
        }
        out << ") {\n    ";
        if (declaration.returnType != voidType) {
            out << "return ";
        }
        out << table << "_m" << effectiveMethod << "(fdn_data";
        for (std::size_t parameter = 0; parameter < declaration.parameters.size(); ++parameter) {
            out << ", fdn_arg_" << parameter;
        }
        out << ");\n}\n";
    }
    out << "static const struct fdn_contract_" << target.defaultContract.declaration
        << "_vtable " << selfTable << " = {\n";
    out << "    " << table << "_drop";
    out << (origin.methods.empty() ? "\n" : ",\n");
    for (std::size_t method = 0; method < origin.methods.size(); ++method) {
        out << "    " << selfTable << "_m" << method;
        out << (method + 1 == origin.methods.size() ? "\n" : ",\n");
    }
    out << "};\n";
}

std::vector<ContractUse> collectContractUses(const FirProgram &program) {
    std::vector<ContractUse> uses;
    std::unordered_map<std::string, std::size_t> found;
    for (const auto &function : program.functions) {
        for (const auto &expression : function.expressions) {
            const auto *contract = std::get_if<FirContractExpression>(&expression.value);
            if (contract == nullptr) {
                continue;
            }
            const auto key = typeKey(contract->contractType) + ':' +
                             typeKey(contract->concreteType);
            if (found.emplace(key, uses.size()).second) {
                uses.push_back(
                    {contract->contractType, contract->concreteType, contract->methods});
            }
        }
    }
    return uses;
}

void emitContractAdapters(std::ostringstream &out, const FirProgram &program,
                          const ContractUse &use) {
    const auto &contract = program.contracts[use.contract.declaration];
    if (use.methods.size() != contract.methods.size()) {
        internalError("contract adapter method count mismatch");
    }
    const auto table = vtableName(use.contract, use.concrete);
    out << "static void " << table << "_drop(void *fdn_data) {\n";
    out << "    if (fdn_data == NULL) {\n";
    out << "        return;\n";
    out << "    }\n";
    if (typeRequiresDrop(program, use.concrete)) {
        out << "    " << dropName(use.concrete) << "((" << cType(use.concrete)
            << " *)fdn_data);\n";
    }
    out << "    fdn_dealloc(fdn_data);\n";
    out << "}\n";
    for (std::size_t method = 0; method < contract.methods.size(); ++method) {
        const auto &declaration = contract.methods[method];
        out << "static " << cType(declaration.returnType) << ' ' << table << "_m" << method
            << "(void *fdn_data";
        for (std::size_t parameter = 0; parameter < declaration.parameters.size(); ++parameter) {
            out << ", " << cType(declaration.parameters[parameter]) << " fdn_arg_" << parameter;
        }
        out << ");\n";
    }
    for (std::size_t method = 0; method < contract.methods.size(); ++method) {
        const auto &declaration = contract.methods[method];
        const auto &target = use.methods[method];
        const auto implementation = target.function;
        if (implementation >= program.functions.size() ||
            program.functions[implementation].parameters.empty()) {
            internalError("contract adapter implementation is invalid");
        }
        const auto &function = program.functions[implementation];
        const auto adapter = table + "_m" + std::to_string(method);
        if (target.contractDefault) {
            emitDefaultSelfVtable(out, program, use, target, table, method);
        }
        out << "static " << cType(declaration.returnType) << ' ' << adapter
            << "(void *fdn_data";
        for (std::size_t parameter = 0; parameter < declaration.parameters.size(); ++parameter) {
            out << ", " << cType(declaration.parameters[parameter]) << " fdn_arg_" << parameter;
        }
        out << ") {\n";
        if (target.contractDefault) {
            out << "    struct fdn_contract_" << target.defaultContract.declaration
                << " fdn_self = {fdn_data, &" << defaultSelfVtableName(table, method)
                << "};\n";
            out << "    ";
            if (declaration.returnType != voidType && !function.diverges) {
                out << "return ";
            }
            out << functionName(program, implementation) << "(fdn_self";
        } else {
            out << "    ";
            if (declaration.returnType != voidType && !function.diverges) {
                out << "return ";
            }
            out << functionName(program, implementation) << "(("
                << cType(function.locals[function.parameters.front()].type) << ")"
                << contractTargetData(program, use, target);
        }
        for (std::size_t parameter = 0; parameter < declaration.parameters.size(); ++parameter) {
            out << ", fdn_arg_" << parameter;
        }
        out << ");\n";
        out << "}\n";
    }
    out << "static const struct fdn_contract_" << use.contract.declaration << "_vtable "
        << table << " = {\n";
    out << "    " << table << "_drop";
    out << (contract.methods.empty() ? "\n" : ",\n");
    for (std::size_t method = 0; method < contract.methods.size(); ++method) {
        out << "    " << table << "_m" << method;
        out << (method + 1 == contract.methods.size() ? "\n" : ",\n");
    }
    out << "};\n\n";
}

void collectSequenceType(const Type &type, std::unordered_map<std::string, Type> &arrays,
                         std::unordered_map<std::string, Type> &slices) {
    if (type.kind == TypeKind::Array) {
        arrays.emplace(typeKey(type), type);
    }
    if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
        type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
        slices.emplace(typeKey(type), type);
    }
    for (const auto &argument : type.arguments) {
        collectSequenceType(argument, arrays, slices);
    }
}

void collectSequenceTypes(const FirProgram &program, std::vector<Type> &arrays,
                          std::vector<Type> &slices) {
    std::unordered_map<std::string, Type> arrayTypes;
    std::unordered_map<std::string, Type> sliceTypes;
    for (const auto &type : program.structs) {
        for (const auto &field : type.fields) {
            collectSequenceType(field.type, arrayTypes, sliceTypes);
        }
    }
    for (const auto &type : program.enums) {
        for (const auto &variant : type.variants) {
            if (variant.payload.has_value()) {
                collectSequenceType(*variant.payload, arrayTypes, sliceTypes);
            }
        }
    }
    for (const auto &type : program.contracts) {
        for (const auto &method : type.methods) {
            collectSequenceType(method.returnType, arrayTypes, sliceTypes);
            for (const auto &parameter : method.parameters) {
                collectSequenceType(parameter, arrayTypes, sliceTypes);
            }
        }
    }
    for (const auto &function : program.functions) {
        collectSequenceType(function.returnType, arrayTypes, sliceTypes);
        for (const auto &local : function.locals) {
            collectSequenceType(local.type, arrayTypes, sliceTypes);
        }
        for (const auto &expression : function.expressions) {
            collectSequenceType(expression.type, arrayTypes, sliceTypes);
        }
    }
    for (auto &[key, type] : arrayTypes) {
        static_cast<void>(key);
        arrays.push_back(std::move(type));
    }
    for (auto &[key, type] : sliceTypes) {
        static_cast<void>(key);
        slices.push_back(std::move(type));
    }
    const auto byKey = [](const Type &left, const Type &right) {
        return typeKey(left) < typeKey(right);
    };
    std::sort(arrays.begin(), arrays.end(), byKey);
    std::sort(slices.begin(), slices.end(), byKey);
}

void emitArrayDefinition(std::ostringstream &out, const Type &type) {
    out << "struct " << arrayName(type) << " {\n";
    out << "    " << cType(type.arguments.front()) << " fdn_data["
        << (type.declaration == 0 ? 1 : type.declaration) << "];\n";
    out << "};\n\n";
}

void emitSliceDefinition(std::ostringstream &out, const Type &type) {
    const auto &slice = type.arguments.front();
    const auto &element = slice.arguments.front();
    out << "typedef struct " << sliceName(type) << " {\n";
    out << "    ";
    if (type.kind == TypeKind::View) {
        out << "const ";
    }
    out << cType(element) << " *fdn_data;\n";
    out << "    size_t fdn_length;\n";
    out << "} " << sliceName(type) << ";\n\n";
}

void emitOwnershipPrototypes(std::ostringstream &out, const FirProgram &program,
                             const std::vector<Type> &arrays) {
    for (const auto &type : arrays) {
        if (!typeRequiresDrop(program, type)) {
            continue;
        }
        out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
            << " *value);\n";
        out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
            << cType(type) << " *value);\n";
    }
    for (std::size_t id = 0; id < program.structs.size(); ++id) {
        const Type type{TypeKind::Struct, id};
        if (!typeRequiresDrop(program, type)) {
            continue;
        }
        out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
            << " *value);\n";
        out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
            << cType(type)
            << " *value);\n";
    }
    for (std::size_t id = 0; id < program.enums.size(); ++id) {
        const Type type{TypeKind::Enum, id};
        if (!typeRequiresDrop(program, type)) {
            continue;
        }
        out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
            << " *value);\n";
        out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
            << cType(type)
            << " *value);\n";
    }
    out << '\n';
}

void emitArrayOwnership(std::ostringstream &out, const FirProgram &program, const Type &type) {
    if (!typeRequiresDrop(program, type)) {
        return;
    }
    const auto &element = type.arguments.front();
    out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
        << " *value) {\n";
    if (type.declaration != 0) {
        out << "    for (size_t fdn_index = " << type.declaration
            << "; fdn_index-- > 0;) {\n";
        emitDropValue(out, program, element, "value->fdn_data[fdn_index]", 2);
        out << "    }\n";
    }
    out << "}\n\n";

    out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
        << cType(type) << " *value) {\n";
    out << "    " << cType(type) << " result = {0};\n";
    if (type.declaration != 0) {
        out << "    for (size_t fdn_index = 0; fdn_index < " << type.declaration
            << "; ++fdn_index) {\n";
        emitMoveAssignment(out, program, element, "result.fdn_data[fdn_index]",
                           "value->fdn_data[fdn_index]", 2);
        out << "    }\n";
    }
    out << "    return result;\n";
    out << "}\n\n";
}

void emitStructOwnership(std::ostringstream &out, const FirProgram &program, FirStructId id) {
    const Type type{TypeKind::Struct, id};
    if (!typeRequiresDrop(program, type)) {
        return;
    }

    out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
        << " *value) {\n";
    if (program.structs[id].dropFunction.has_value()) {
        out << "    if (!value->fdn_drop_active) {\n";
        out << "        return;\n";
        out << "    }\n";
        out << "    value->fdn_drop_active = false;\n";
        out << "    " << functionName(program, *program.structs[id].dropFunction)
            << "(value);\n";
        out << "    value->fdn_drop_active = false;\n";
    }
    for (std::size_t field = program.structs[id].fields.size(); field-- > 0;) {
        emitDropValue(out, program, program.structs[id].fields[field].type,
                      "value->" + fieldName(field), 1);
    }
    out << "}\n\n";

    out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
        << cType(type)
        << " *value) {\n";
    out << "    " << cType(type) << " result = {0};\n";
    if (program.structs[id].dropFunction.has_value()) {
        out << "    result.fdn_drop_active = value->fdn_drop_active;\n";
        out << "    value->fdn_drop_active = false;\n";
    }
    for (std::size_t field = 0; field < program.structs[id].fields.size(); ++field) {
        emitMoveAssignment(out, program, program.structs[id].fields[field].type,
                           "result." + fieldName(field), "value->" + fieldName(field), 1);
    }
    out << "    return result;\n";
    out << "}\n\n";
}

void emitEnumOwnership(std::ostringstream &out, const FirProgram &program, FirEnumId id) {
    const Type type{TypeKind::Enum, id};
    if (!typeRequiresDrop(program, type)) {
        return;
    }

    out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
        << " *value) {\n";
    out << "    switch (value->fdn_tag) {\n";
    for (std::size_t variant = 0; variant < program.enums[id].variants.size(); ++variant) {
        out << "    case " << enumTag(id, variant) << ":\n";
        const auto &payload = program.enums[id].variants[variant].payload;
        if (payload.has_value() && typeRequiresDrop(program, *payload)) {
            emitDropValue(out, program, *payload,
                          "value->fdn_data." + payloadName(variant), 2);
        }
        out << "        break;\n";
    }
    out << "    default:\n";
    out << "        fdn_invalid_enum_tag();\n";
    out << "    }\n";
    out << "}\n\n";

    out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
        << cType(type)
        << " *value) {\n";
    out << "    " << cType(type) << " result = {0};\n";
    out << "    result.fdn_tag = value->fdn_tag;\n";
    out << "    switch (value->fdn_tag) {\n";
    for (std::size_t variant = 0; variant < program.enums[id].variants.size(); ++variant) {
        out << "    case " << enumTag(id, variant) << ":\n";
        const auto &payload = program.enums[id].variants[variant].payload;
        if (payload.has_value()) {
            emitMoveAssignment(out, program, *payload,
                               "result.fdn_data." + payloadName(variant),
                               "value->fdn_data." + payloadName(variant), 2);
        }
        out << "        break;\n";
    }
    out << "    default:\n";
    out << "        fdn_invalid_enum_tag();\n";
    out << "    }\n";
    out << "    return result;\n";
    out << "}\n\n";
}

void emitOwnershipDefinitions(std::ostringstream &out, const FirProgram &program,
                              const std::vector<Type> &arrays) {
    for (const auto &type : arrays) {
        emitArrayOwnership(out, program, type);
    }
    for (std::size_t id = 0; id < program.structs.size(); ++id) {
        emitStructOwnership(out, program, id);
    }
    for (std::size_t id = 0; id < program.enums.size(); ++id) {
        emitEnumOwnership(out, program, id);
    }
}

std::vector<Type> collectChannelPayloadTypes(const FirProgram &program) {
    std::map<std::string, Type> found;
    for (const auto &function : program.functions) {
        for (const auto &expression : function.expressions) {
            const auto *channel = std::get_if<FirChannelExpression>(&expression.value);
            if (channel != nullptr && channel->payload != voidType &&
                typeRequiresDrop(program, channel->payload)) {
                found.emplace(cTypeTag(channel->payload), channel->payload);
            }
        }
    }
    std::vector<Type> result;
    result.reserve(found.size());
    for (const auto &[key, type] : found) {
        static_cast<void>(key);
        result.push_back(type);
    }
    return result;
}

void emitChannelPayloadDrops(std::ostringstream &out, const FirProgram &program,
                             const std::vector<Type> &payloads) {
    for (const auto &payload : payloads) {
        out << "static FDN_MAYBE_UNUSED void " << channelDropName(payload)
            << "(void *fdn_value) {\n";
        out << "    " << cType(payload) << " *fdn_payload = fdn_value;\n";
        emitDropValue(out, program, payload, "*fdn_payload", 1);
        out << "}\n\n";
    }
}

std::optional<std::size_t>
typeNode(const FirProgram &program, const Type &type,
         const std::unordered_map<std::string, std::size_t> &arrayNodes) {
    if (type.kind == TypeKind::Struct) {
        return type.declaration;
    }
    if (type.kind == TypeKind::Enum) {
        return program.structs.size() + type.declaration;
    }
    if (type.kind == TypeKind::Array) {
        const auto found = arrayNodes.find(typeKey(type));
        return found == arrayNodes.end() ? std::nullopt : std::optional<std::size_t>{found->second};
    }
    return std::nullopt;
}

void emitSignature(std::ostringstream &out, const FirProgram &program, FirFunctionId id) {
    const auto &function = program.functions[id];
    if (id == program.main) {
        out << "static int32_t " << functionName(program, id) << '(';
        if (function.parameters.empty()) {
            out << "void";
        } else {
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0) {
                    out << ", ";
                }
                const auto local = function.parameters[index];
                out << cType(function.locals[local].type) << ' '
                    << localName(function, local);
            }
        }
        out << ')';
        return;
    }

    if (function.closure) {
        out << cType(function.returnType) << ' ' << functionName(program, id)
            << "(void *fdn_env";
        for (const auto parameter : function.parameters) {
            out << ", " << cType(function.locals[parameter].type) << ' '
                << localName(function, parameter);
        }
        out << ')';
        return;
    }

    if (function.diverges) {
        out << "_Noreturn void ";
    } else {
        out << cType(function.returnType) << ' ';
    }
    out << functionName(program, id) << '(';
    if (function.parameters.empty()) {
        out << "void";
    } else {
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            const auto local = function.parameters[index];
            out << cType(function.locals[local].type) << ' ' << localName(function, local);
        }
    }
    out << ')';
}

std::size_t taskSuspensionCount(const FirFunction &function) {
    const auto expressions = static_cast<std::size_t>(
        std::count_if(function.expressions.begin(), function.expressions.end(),
                      [](const FirExpression &expression) {
                          return std::holds_alternative<FirTaskWaitExpression>(expression.value) ||
                                 std::holds_alternative<FirBlockingCallExpression>(
                                     expression.value) ||
                                 std::holds_alternative<FirChannelSendExpression>(
                                     expression.value) ||
                                 std::holds_alternative<FirChannelReceiveExpression>(
                                     expression.value);
                      }));
    const auto selections = static_cast<std::size_t>(
        std::count_if(function.statements.begin(), function.statements.end(),
                      [](const FirStatement &statement) {
                          return std::holds_alternative<FirSelectStatement>(statement.value);
                      }));
    return expressions + selections;
}

void emitTaskFrameDefinition(std::ostringstream &out, const FirProgram &program,
                             FirFunctionId id) {
    const auto &function = program.functions[id];
    if (!function.task) {
        return;
    }
    const auto frame = taskFrameName(program, id);
    const auto suspensions = taskSuspensionCount(function);
    out << "struct " << frame << " {\n";
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto local = function.parameters[index];
        out << "    " << cType(function.locals[local].type) << " fdn_arg_" << index << ";\n";
    }
    if (function.returnType != voidType) {
        out << "    " << cType(function.returnType) << " fdn_result;\n";
        out << "    bool fdn_result_active;\n";
    }
    out << "    bool fdn_arguments_active;\n";
    if (suspensions != 0) {
        out << "    uint32_t fdn_state;\n";
        for (std::size_t local = 0; local < function.locals.size(); ++local) {
            out << "    " << cType(function.locals[local].type) << " fdn_local_" << local
                << ";\n";
            if (typeRequiresDrop(program, function.locals[local].type)) {
                out << "    bool fdn_local_" << local << "_active;\n";
            }
        }
        for (std::size_t expression = 0; expression < function.expressions.size();
             ++expression) {
            if (std::holds_alternative<FirBlockingCallExpression>(
                    function.expressions[expression].value)) {
                out << "    fdn_blocking_job *" << blockingJobName(expression) << ";\n";
            }
        }
    }
    out << "};\n\n";
}

void emitBlockingWorkDefinitions(std::ostringstream &out, const FirProgram &program,
                                 FirFunctionId id, std::string_view sourcePath) {
    const auto &function = program.functions[id];
    if (!function.task) {
        return;
    }
    for (std::size_t expressionId = 0; expressionId < function.expressions.size();
         ++expressionId) {
        const auto *blocking = std::get_if<FirBlockingCallExpression>(
            &function.expressions[expressionId].value);
        if (blocking == nullptr) {
            continue;
        }
        if (blocking->function >= program.functions.size() ||
            !program.functions[blocking->function].blocking) {
            internalError("blocking call has an invalid target");
        }
        const auto &target = program.functions[blocking->function];
        if (blocking->arguments.size() != blocking->argumentStorages.size() ||
            blocking->argumentStorages.size() != target.parameters.size()) {
            internalError("blocking call has invalid argument storage");
        }
        out << "static void " << blockingWorkName(program, id, expressionId)
            << "(void *fdn_raw) {\n";
        out << "    struct " << taskFrameName(program, id) << " *fdn_frame = (struct "
            << taskFrameName(program, id) << " *)fdn_raw;\n";
        out << "    struct fdn_frame fdn_frame_current;\n";
        const auto traceSource = function.sourcePath.empty()
                                     ? sourcePath
                                     : std::string_view(function.sourcePath);
        const auto framePackage = function.packageName.empty()
                                      ? std::string_view("main")
                                      : std::string_view(function.packageName);
        const auto &expression = function.expressions[expressionId];
        out << "    fdn_frame_enter(&fdn_frame_current, " << cString(framePackage) << ", "
            << cString(traceFunctionName(function)) << ", " << cString(traceSource) << ", "
            << expression.span.line << ", " << expression.span.column << ");\n";
        out << "    ";
        if (blocking->resultStorage.has_value()) {
            out << "fdn_frame->fdn_local_" << *blocking->resultStorage << " = ";
        }
        out << functionName(program, blocking->function) << '(';
        for (std::size_t index = 0; index < blocking->argumentStorages.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << "fdn_frame->fdn_local_" << blocking->argumentStorages[index];
        }
        out << ");\n";
        if (blocking->resultStorage.has_value() &&
            typeRequiresDrop(program, function.locals[*blocking->resultStorage].type)) {
            out << "    fdn_frame->fdn_local_" << *blocking->resultStorage
                << "_active = true;\n";
        }
        out << "    fdn_frame_leave(&fdn_frame_current);\n";
        out << "}\n\n";
    }
}

void emitTaskSupportPrototypes(std::ostringstream &out, const FirProgram &program,
                               FirFunctionId id) {
    if (!program.functions[id].task) {
        return;
    }
    out << "static fdn_task_poll " << taskPollName(program, id)
        << "(void *, bool);\n";
    out << "static void " << taskMoveResultName(program, id) << "(void *, void *);\n";
    out << "static void " << taskDropFrameName(program, id) << "(void *);\n";
}

void emitTaskSupport(std::ostringstream &out, const FirProgram &program, FirFunctionId id,
                     std::string_view sourcePath) {
    const auto &function = program.functions[id];
    if (!function.task) {
        return;
    }
    const auto frame = taskFrameName(program, id);
    const auto suspensions = taskSuspensionCount(function);
    out << "static fdn_task_poll " << taskPollName(program, id)
        << "(void *fdn_raw, bool fdn_cancellation_requested) {\n";
    out << "    struct " << frame << " *fdn_frame = (struct " << frame
        << " *)fdn_raw;\n";
    if (function.diverges && suspensions == 0) {
        out << "    (void)fdn_task_cancellation_enter(fdn_cancellation_requested);\n";
    } else {
        out << "    bool fdn_previous_cancellation = "
               "fdn_task_cancellation_enter(fdn_cancellation_requested);\n";
    }
    if (suspensions == 0) {
        out << "    fdn_frame->fdn_arguments_active = false;\n";
        out << "    ";
        if (!function.diverges && function.returnType != voidType) {
            out << "fdn_frame->fdn_result = ";
        }
        out << functionName(program, id) << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << "fdn_frame->fdn_arg_" << index;
        }
        out << ");\n";
        if (function.diverges) {
            out << "    fdn_panic_cstr(\"unreachable task poll\");\n";
        } else {
            out << "    fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
            if (function.returnType != voidType) {
                out << "    fdn_frame->fdn_result_active = true;\n";
            }
            out << "    return FDN_TASK_READY;\n";
        }
    } else {
        out << "    struct fdn_frame fdn_frame_current;\n";
        const auto traceSource = function.sourcePath.empty() ? sourcePath
                                                             : std::string_view(function.sourcePath);
        const auto framePackage = function.packageName.empty()
                                      ? std::string_view("main")
                                      : std::string_view(function.packageName);
        out << "    fdn_frame_enter(&fdn_frame_current, " << cString(framePackage) << ", "
            << cString(traceFunctionName(function)) << ", " << cString(traceSource) << ", "
            << function.sourceSpan.line << ", " << function.sourceSpan.column << ");\n";
        out << "    switch (fdn_frame->fdn_state) {\n";
        out << "    case 0:\n";
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            const auto local = function.parameters[index];
            emitMoveAssignment(out, program, function.locals[local].type,
                               "fdn_frame->fdn_local_" + std::to_string(local),
                               "fdn_frame->fdn_arg_" + std::to_string(index), 2);
            if (typeRequiresDrop(program, function.locals[local].type)) {
                out << "        fdn_frame->fdn_local_" << local << "_active = true;\n";
            }
        }
        out << "        fdn_frame->fdn_arguments_active = false;\n";
        out << "        break;\n";
        for (std::size_t state = 1; state <= suspensions; ++state) {
            out << "    case " << state << ":\n";
            out << "        goto fdn_task_state_" << state << ";\n";
        }
        out << "    default:\n";
        out << "        fdn_panic_cstr(\"invalid task state\");\n";
        out << "    }\n";
        FunctionEmitter emitter(out, program, id, true);
        const auto exits = emitter.emitBlock(function.body, 1);
        if (!exits && function.returnType == voidType) {
            out << "    fdn_task_cancellation_leave(fdn_previous_cancellation);\n";
            out << "    fdn_frame_leave(&fdn_frame_current);\n";
            out << "    return FDN_TASK_READY;\n";
        } else if (!exits) {
            out << "    fdn_panic_cstr(\"task completed without a result\");\n";
        }
    }
    out << "}\n\n";

    out << "static void " << taskMoveResultName(program, id)
        << "(void *fdn_raw, void *fdn_raw_result) {\n";
    out << "    struct " << frame << " *fdn_frame = (struct " << frame
        << " *)fdn_raw;\n";
    if (function.returnType == voidType) {
        out << "    (void)fdn_frame;\n";
        out << "    (void)fdn_raw_result;\n";
    } else {
        out << "    if (!fdn_frame->fdn_result_active || fdn_raw_result == NULL) {\n";
        out << "        fdn_panic_cstr(\"task result is unavailable\");\n";
        out << "    }\n";
        const auto target = "*(" + cType(function.returnType) + " *)fdn_raw_result";
        emitMoveAssignment(out, program, function.returnType, target,
                           "fdn_frame->fdn_result", 1);
        out << "    fdn_frame->fdn_result_active = false;\n";
    }
    out << "}\n\n";

    out << "static void " << taskDropFrameName(program, id) << "(void *fdn_raw) {\n";
    out << "    struct " << frame << " *fdn_frame = (struct " << frame
        << " *)fdn_raw;\n";
    out << "    if (fdn_frame->fdn_arguments_active) {\n";
    for (std::size_t index = function.parameters.size(); index-- > 0;) {
        const auto local = function.parameters[index];
        emitDropValue(out, program, function.locals[local].type,
                      "fdn_frame->fdn_arg_" + std::to_string(index), 2);
    }
    out << "    }\n";
    if (suspensions != 0) {
        for (std::size_t expression = 0; expression < function.expressions.size();
             ++expression) {
            if (!std::holds_alternative<FirBlockingCallExpression>(
                    function.expressions[expression].value)) {
                continue;
            }
            out << "    if (fdn_frame->" << blockingJobName(expression)
                << " != NULL) {\n";
            out << "        fdn_panic_cstr(\"task frame still has blocking work\");\n";
            out << "    }\n";
        }
        for (std::size_t local = function.locals.size(); local-- > 0;) {
            if (!typeRequiresDrop(program, function.locals[local].type)) {
                continue;
            }
            out << "    if (fdn_frame->fdn_local_" << local << "_active) {\n";
            emitDropValue(out, program, function.locals[local].type,
                          "fdn_frame->fdn_local_" + std::to_string(local), 2);
            out << "        fdn_frame->fdn_local_" << local << "_active = false;\n";
            out << "    }\n";
        }
    }
    if (function.returnType != voidType) {
        out << "    if (fdn_frame->fdn_result_active) {\n";
        emitDropValue(out, program, function.returnType, "fdn_frame->fdn_result", 2);
        out << "    }\n";
    }
    out << "    fdn_dealloc(fdn_frame);\n";
    out << "}\n\n";
}

void emitMainWrapper(std::ostringstream &out, const FirProgram &program) {
    const auto &function = program.functions[program.main];
    const auto acceptsArguments = function.parameters.size() == 1;
    out << "\nint main(";
    if (acceptsArguments) {
        out << "int fdn_argc, char **fdn_argv";
    } else {
        out << "void";
    }
    out << ") {\n";

    if (acceptsArguments) {
        const auto parameter = function.parameters.front();
        out << "    const size_t fdn_argument_count = "
               "fdn_argc > 1 ? (size_t)(fdn_argc - 1) : 0;\n";
        out << "    fdn_string *fdn_argument_values = NULL;\n";
        out << "    if (fdn_argument_count != 0) {\n";
        out << "        if (fdn_argument_count > SIZE_MAX / sizeof(*fdn_argument_values)) {\n";
        out << "            fdn_panic_cstr(\"command-line argument count overflow\");\n";
        out << "        }\n";
        out << "        fdn_argument_values = fdn_alloc(fdn_argument_count * "
               "sizeof(*fdn_argument_values));\n";
        out << "        for (size_t fdn_index = 0; fdn_index < fdn_argument_count; "
               "++fdn_index) {\n";
        out << "            const char *fdn_argument = fdn_argv[fdn_index + 1];\n";
        out << "            fdn_argument_values[fdn_index] = "
               "fdn_string_static(fdn_argument, strlen(fdn_argument));\n";
        out << "        }\n";
        out << "    }\n";
        out << "    const " << cType(function.locals[parameter].type)
            << " fdn_arguments = {fdn_argument_values, fdn_argument_count};\n";
        out << "    const int32_t fdn_result = " << functionName(program, program.main)
            << "(fdn_arguments);\n";
        out << "    fdn_dealloc(fdn_argument_values);\n";
        out << "#if defined(FOUNDATION_VERIFY_ALLOCATIONS)\n";
        out << "    if (fdn_live_allocations() != 0) {\n";
        out << "        fdn_panic_cstr(\"live allocations after main\");\n";
        out << "    }\n";
        out << "#endif\n";
        out << "    return (int)fdn_result;\n";
    } else {
        out << "#if defined(FOUNDATION_VERIFY_ALLOCATIONS)\n";
        out << "    const int32_t fdn_result = " << functionName(program, program.main)
            << "();\n";
        out << "    if (fdn_live_allocations() != 0) {\n";
        out << "        fdn_panic_cstr(\"live allocations after main\");\n";
        out << "    }\n";
        out << "    return (int)fdn_result;\n";
        out << "#else\n";
        out << "    return (int)" << functionName(program, program.main) << "();\n";
        out << "#endif\n";
    }
    out << "}\n";
}

std::vector<FirFunctionId> collectFunctionValueUses(const FirProgram &program) {
    std::vector<FirFunctionId> result;
    for (const auto &function : program.functions) {
        for (const auto &expression : function.expressions) {
            const auto *value = std::get_if<FirFunctionValueExpression>(&expression.value);
            if (value != nullptr) {
                result.push_back(value->function);
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void emitFunctionValueAdapter(std::ostringstream &out, const FirProgram &program,
                              FirFunctionId id) {
    const auto &function = program.functions[id];
    out << "static " << cType(function.returnType) << ' '
        << functionAdapterName(program, id) << "(void *fdn_env";
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto local = function.parameters[index];
        out << ", " << cType(function.locals[local].type) << " fdn_arg_" << index;
    }
    out << ") {\n";
    out << "    (void)fdn_env;\n";
    out << "    ";
    if (function.returnType != voidType && !function.diverges) {
        out << "return ";
    }
    out << functionName(program, id) << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << "fdn_arg_" << index;
    }
    out << ");\n";
    if (function.diverges) {
        out << "    fdn_panic_cstr(\"unreachable function value adapter\");\n";
    }
    out << "}\n\n";
}

void emitCAbiParameters(std::ostringstream &out, const FirFunction &function,
                        bool includeNames) {
    if (function.parameters.empty()) {
        out << "void";
        return;
    }
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        const auto local = function.parameters[index];
        out << cType(function.locals[local].type);
        if (includeNames) {
            out << " fdn_arg_" << index;
        }
    }
}

void emitCAbiSignature(std::ostringstream &out, const FirFunction &function,
                       bool includeNames) {
    out << cType(function.returnType) << ' ' << *function.cSymbol << '(';
    emitCAbiParameters(out, function, includeNames);
    out << ')';
}

std::string sourceFramePath(const FirFunction &function, std::string_view fallback) {
    return function.sourcePath.empty() ? std::string(fallback) : function.sourcePath;
}

void emitCArguments(std::ostringstream &out, const FirFunction &function, bool abiNames) {
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << (abiNames ? "fdn_arg_" + std::to_string(index)
                         : localName(function, function.parameters[index]));
    }
}

void emitImportedFunction(std::ostringstream &out, const FirProgram &program,
                          FirFunctionId id, std::string_view sourcePath) {
    const auto &function = program.functions[id];
    emitSignature(out, program, id);
    out << " {\n";
    out << "    fdn_frame fdn_frame_current;\n";
    out << "    fdn_frame_enter_native(&fdn_frame_current, " << cString(*function.cSymbol)
        << ", " << cString(sourceFramePath(function, sourcePath)) << ", "
        << function.sourceSpan.line << ", " << function.sourceSpan.column << ");\n";
    if (function.returnType == voidType) {
        out << "    " << *function.cSymbol << '(';
        emitCArguments(out, function, false);
        out << ");\n";
        out << "    fdn_frame_leave(&fdn_frame_current);\n";
        out << "    return;\n";
    } else {
        out << "    " << cType(function.returnType) << " fdn_result = " << *function.cSymbol
            << '(';
        emitCArguments(out, function, false);
        out << ");\n";
        out << "    fdn_frame_leave(&fdn_frame_current);\n";
        out << "    return fdn_result;\n";
    }
    out << "}\n";
}

void emitExportedWrapper(std::ostringstream &out, const FirProgram &program,
                         FirFunctionId id, std::string_view sourcePath) {
    const auto &function = program.functions[id];
    emitCAbiSignature(out, function, true);
    out << " {\n";
    out << "    fdn_frame fdn_frame_current;\n";
    out << "    fdn_frame_enter_native(&fdn_frame_current, " << cString(*function.cSymbol)
        << ", " << cString(sourceFramePath(function, sourcePath)) << ", "
        << function.sourceSpan.line << ", " << function.sourceSpan.column << ");\n";
    if (function.diverges) {
        out << "    " << functionName(program, id) << '(';
        emitCArguments(out, function, true);
        out << ");\n";
    } else if (function.returnType == voidType) {
        out << "    " << functionName(program, id) << '(';
        emitCArguments(out, function, true);
        out << ");\n";
        out << "    fdn_frame_leave(&fdn_frame_current);\n";
        out << "    return;\n";
    } else {
        out << "    " << cType(function.returnType) << " fdn_result = "
            << functionName(program, id) << '(';
        emitCArguments(out, function, true);
        out << ");\n";
        out << "    fdn_frame_leave(&fdn_frame_current);\n";
        out << "    return fdn_result;\n";
    }
    out << "}\n";
}

} // namespace

std::string emitC(const FirProgram &source, std::string_view sourcePath) {
    Monomorphizer monomorphizer(source);
    auto program = monomorphizer.run();
    markDivergingFunctions(program);
    const auto contractUses = collectContractUses(program);
    std::vector<Type> arrays;
    std::vector<Type> slices;
    collectSequenceTypes(program, arrays, slices);
    const auto functionTypes = collectFunctionTypes(program);
    const auto functionValueUses = collectFunctionValueUses(program);
    const auto channelPayloads = collectChannelPayloadTypes(program);
    std::ostringstream out;
    out << "#include <stdbool.h>\n";
    out << "#include <stdint.h>\n";
    if (!program.functions[program.main].parameters.empty()) {
        out << "#include <string.h>\n";
    }
    out << "#include \"foundation/runtime.h\"\n\n";
    out << "#if defined(__GNUC__) || defined(__clang__)\n";
    out << "#define FDN_MAYBE_UNUSED __attribute__((unused))\n";
    out << "#else\n";
    out << "#define FDN_MAYBE_UNUSED\n";
    out << "#endif\n\n";
    out << "typedef struct fdn_channel_pair {\n";
    out << "    fdn_channel *fdn_field_0;\n";
    out << "    fdn_channel *fdn_field_1;\n";
    out << "} fdn_channel_pair;\n\n";
    for (std::size_t index = 0; index < program.structs.size(); ++index) {
        out << "typedef struct fdn_struct_" << index << " fdn_struct_" << index << ";\n";
    }
    for (std::size_t index = 0; index < program.enums.size(); ++index) {
        out << "typedef struct fdn_enum_" << index << " fdn_enum_" << index << ";\n";
    }
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        out << "typedef struct fdn_contract_" << index << " fdn_contract_" << index << ";\n";
    }
    for (const auto &type : arrays) {
        out << "typedef struct " << arrayName(type) << ' ' << arrayName(type) << ";\n";
    }
    for (const auto &type : functionTypes) {
        out << "typedef struct " << cType(type) << ' ' << cType(type) << ";\n";
    }
    if (!program.structs.empty() || !program.enums.empty() || !program.contracts.empty() ||
        !arrays.empty() || !functionTypes.empty()) {
        out << '\n';
    }
    for (const auto &type : functionTypes) {
        emitFunctionTypeDefinition(out, type);
    }
    for (const auto &type : slices) {
        emitSliceDefinition(out, type);
    }

    const auto typeCount = program.structs.size() + program.enums.size() + arrays.size();
    std::unordered_map<std::string, std::size_t> arrayNodes;
    for (std::size_t index = 0; index < arrays.size(); ++index) {
        arrayNodes.emplace(typeKey(arrays[index]), program.structs.size() + program.enums.size() +
                                                       index);
    }
    std::vector<std::size_t> dependencies(typeCount);
    std::vector<std::vector<std::size_t>> dependents(typeCount);
    for (std::size_t type = 0; type < program.structs.size(); ++type) {
        for (const auto &field : program.structs[type].fields) {
            const auto target = typeNode(program, field.type, arrayNodes);
            if (target.has_value()) {
                ++dependencies[type];
                dependents[*target].push_back(type);
            }
        }
    }
    for (std::size_t type = 0; type < program.enums.size(); ++type) {
        for (const auto &variant : program.enums[type].variants) {
            if (!variant.payload.has_value()) {
                continue;
            }
            const auto target = typeNode(program, *variant.payload, arrayNodes);
            if (target.has_value()) {
                ++dependencies[program.structs.size() + type];
                dependents[*target].push_back(program.structs.size() + type);
            }
        }
    }
    for (std::size_t index = 0; index < arrays.size(); ++index) {
        const auto node = program.structs.size() + program.enums.size() + index;
        const auto target = typeNode(program, arrays[index].arguments.front(), arrayNodes);
        if (target.has_value()) {
            ++dependencies[node];
            dependents[*target].push_back(node);
        }
    }
    std::vector<std::size_t> ready;
    for (std::size_t type = 0; type < dependencies.size(); ++type) {
        if (dependencies[type] == 0) {
            ready.push_back(type);
        }
    }
    for (std::size_t current = 0; current < ready.size(); ++current) {
        if (ready[current] < program.structs.size()) {
            emitStructDefinition(out, program, ready[current]);
        } else if (ready[current] < program.structs.size() + program.enums.size()) {
            emitEnumDefinition(out, program, ready[current] - program.structs.size());
        } else {
            emitArrayDefinition(
                out, arrays[ready[current] - program.structs.size() - program.enums.size()]);
        }
        for (const auto dependent : dependents[ready[current]]) {
            if (--dependencies[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    if (ready.size() != typeCount) {
        internalError("recursive inline type reached C emission");
    }

    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        emitContractDefinition(out, program, index);
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitClosureEnvironmentDefinition(out, program, index);
    }

    for (const auto &type : program.structs) {
        if (type.dropFunction.has_value()) {
            emitSignature(out, program, *type.dropFunction);
            out << ";\n";
        }
    }
    if (std::any_of(program.structs.begin(), program.structs.end(),
                    [](const FirStruct &type) { return type.dropFunction.has_value(); })) {
        out << '\n';
    }

    emitOwnershipPrototypes(out, program, arrays);
    emitOwnershipDefinitions(out, program, arrays);
    emitChannelPayloadDrops(out, program, channelPayloads);
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitClosureDrop(out, program, index);
    }

    for (const auto &function : program.functions) {
        if (function.cSymbol.has_value()) {
            emitCAbiSignature(out, function, false);
            out << ";\n";
        }
    }
    if (std::any_of(program.functions.begin(), program.functions.end(),
                    [](const FirFunction &function) { return function.cSymbol.has_value(); })) {
        out << '\n';
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        if (program.functions[index].task &&
            taskSuspensionCount(program.functions[index]) != 0) {
            continue;
        }
        emitSignature(out, program, index);
        out << ";\n";
    }
    out << '\n';

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitTaskFrameDefinition(out, program, index);
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitTaskSupportPrototypes(out, program, index);
    }
    if (std::any_of(program.functions.begin(), program.functions.end(),
                    [](const FirFunction &function) { return function.task; })) {
        out << '\n';
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitBlockingWorkDefinitions(out, program, index, sourcePath);
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitTaskSupport(out, program, index, sourcePath);
    }

    for (const auto &use : contractUses) {
        emitContractAdapters(out, program, use);
    }
    for (const auto function : functionValueUses) {
        emitFunctionValueAdapter(out, program, function);
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        if (function.task && taskSuspensionCount(function) != 0) {
            continue;
        }
        if (!function.hasBody) {
            emitImportedFunction(out, program, index, sourcePath);
            if (index + 1 != program.functions.size()) {
                out << '\n';
            }
            continue;
        }
        emitSignature(out, program, index);
        out << " {\n";
        out << "    fdn_frame fdn_frame_current;\n";
        const auto traceSource = function.sourcePath.empty() ? sourcePath : function.sourcePath;
        const auto framePackage = function.packageName.empty()
                                      ? std::string_view("main")
                                      : std::string_view(function.packageName);
        out << "    fdn_frame_enter(&fdn_frame_current, " << cString(framePackage) << ", "
            << cString(traceFunctionName(function)) << ", " << cString(traceSource) << ", "
            << function.sourceSpan.line << ", " << function.sourceSpan.column << ");\n";
        if (function.closure) {
            const auto hasCaptures = std::any_of(
                function.locals.begin(), function.locals.end(),
                [](const FirLocal &local) { return local.capture; });
            if (hasCaptures) {
                out << "    struct " << closureEnvironmentName(program, index)
                    << " *fdn_env_data = (struct "
                    << closureEnvironmentName(program, index) << " *)fdn_env;\n";
            } else {
                out << "    (void)fdn_env;\n";
            }
        }
        FunctionEmitter emitter(out, program, index);
        for (const auto parameter : function.parameters) {
            out << "    (void)" << localName(function, parameter) << ";\n";
        }
        bool exits;
        if (index == program.main && function.diverges) {
            out << "    for (;;) {\n";
            exits = emitter.emitBlock(function.body, 2);
            out << "    }\n";
        } else {
            exits = emitter.emitBlock(function.body, 1);
        }
        if (function.returnType == voidType && !exits) {
            out << "    fdn_frame_leave(&fdn_frame_current);\n";
        }
        out << "}\n";
        if (index + 1 != program.functions.size()) {
            out << '\n';
        }
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        if (!function.cSymbol.has_value() || !function.hasBody) {
            continue;
        }
        out << '\n';
        emitExportedWrapper(out, program, index, sourcePath);
    }
    emitMainWrapper(out, program);
    return out.str();
}

std::string emitCHeader(const FirProgram &source) {
    Monomorphizer monomorphizer(source);
    const auto program = monomorphizer.run();
    std::vector<const FirFunction *> exports;
    for (const auto &function : program.functions) {
        if (function.cSymbol.has_value() && function.hasBody) {
            exports.push_back(&function);
        }
    }
    std::sort(exports.begin(), exports.end(), [](const auto *left, const auto *right) {
        return *left->cSymbol < *right->cSymbol;
    });

    std::ostringstream out;
    out << "#ifndef FOUNDATION_GENERATED_C_ABI_H\n";
    out << "#define FOUNDATION_GENERATED_C_ABI_H\n\n";
    out << "#include <stdbool.h>\n";
    out << "#include <stdint.h>\n";
    out << "#include \"foundation/runtime.h\"\n\n";
    out << "#ifdef __cplusplus\n";
    out << "extern \"C\" {\n";
    out << "#endif\n\n";
    for (const auto *function : exports) {
        emitCAbiSignature(out, *function, true);
        out << ";\n";
    }
    if (!exports.empty()) {
        out << '\n';
    }
    out << "#ifdef __cplusplus\n";
    out << "}\n";
    out << "#endif\n\n";
    out << "#endif\n";
    return out.str();
}

} // namespace foundation
