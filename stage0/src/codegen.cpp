#include "foundation/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace foundation {

namespace {

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

std::string cType(const Type &type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "int32_t";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "fdn_string";
    case TypeKind::Own:
    case TypeKind::Edit:
        return type.arguments.size() == 1 ? cType(type.arguments.front()) + " *" : "void *";
    case TypeKind::View:
        return type.arguments.size() == 1 ? "const " + cType(type.arguments.front()) + " *"
                                          : "const void *";
    case TypeKind::Parameter:
        break;
    case TypeKind::Struct:
        return "fdn_struct_" + std::to_string(type.declaration);
    case TypeKind::Enum:
        return "fdn_enum_" + std::to_string(type.declaration);
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

std::string functionName(const FirProgram &program, FirFunctionId id) {
    if (id == program.main) {
        return "main";
    }
    const auto &function = program.functions[id];
    auto name = "fdn_fn_" + safeName(function.name) + "_" + std::to_string(function.source);
    if (function.generic) {
        name += "_g" + std::to_string(id);
    }
    return name;
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

std::string indentation(unsigned int depth) { return std::string(depth * 4, ' '); }

std::string i32Constant(std::int32_t value) {
    if (value == INT32_MIN) {
        return "(-INT32_C(2147483647) - INT32_C(1))";
    }
    return "INT32_C(" + std::to_string(value) + ")";
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
        return std::move(result_);
    }

  private:
    Type instantiateType(const Type &source) {
        if (source.kind == TypeKind::Parameter || source.kind == TypeKind::Invalid) {
            std::terminate();
        }
        if (source.kind == TypeKind::Own || source.kind == TypeKind::View ||
            source.kind == TypeKind::Edit) {
            if (source.arguments.size() != 1) {
                std::terminate();
            }
            return Type{source.kind, 0, {instantiateType(source.arguments.front())}};
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
                                           field.exported});
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
                payload = instantiateType(substitute(*variant.payload, source.arguments));
            }
            instance.variants.push_back({variant.name, std::move(payload), variant.exported});
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
            std::terminate();
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
            if (auto *call = std::get_if<FirCallExpression>(&expression.value);
                call != nullptr && call->kind == FirCallKind::Function) {
                std::vector<Type> callArguments;
                callArguments.reserve(call->typeArguments.size());
                for (const auto &argument : call->typeArguments) {
                    callArguments.push_back(substitute(argument, arguments));
                }
                call->function = instantiateFunction(call->function, callArguments);
                call->typeArguments.clear();
            } else if (auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
                literal->type = instantiateType(substitute(literal->type, arguments));
            } else if (auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
                constructor->type = instantiateType(substitute(constructor->type, arguments));
            } else if (auto *match = std::get_if<FirMatchExpression>(&expression.value)) {
                match->type = instantiateType(substitute(match->type, arguments));
            }
        }
        result_.functions[id] = std::move(function);
        return id;
    }

    const FirProgram &source_;
    FirProgram result_;
    std::unordered_map<std::string, FirStructId> structs_;
    std::unordered_map<std::string, FirEnumId> enums_;
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
    if (std::holds_alternative<FirMoveExpression>(expression.value)) {
        return false;
    }
    if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
        return expressionDiverges(program, function, unary->operand);
    }
    if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
        return expressionDiverges(program, function, ownership->operand);
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
            if (!function.diverges &&
                blockFlow(program, function, function.body) == ControlFlow::Diverges) {
                function.diverges = true;
                changed = true;
            }
        }
    } while (changed);
}

bool typeRequiresDrop(const FirProgram &program, const Type &type) {
    if (type.kind == TypeKind::Own || type.kind == TypeKind::Parameter) {
        return true;
    }
    if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
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
    if (type.kind == TypeKind::Struct) {
        return "fdn_drop_struct_" + std::to_string(type.declaration);
    }
    return "fdn_drop_enum_" + std::to_string(type.declaration);
}

std::string moveName(const Type &type) {
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
        if (target.kind == TypeKind::Struct || target.kind == TypeKind::Enum) {
            if (typeRequiresDrop(program, target)) {
                out << indentation(depth + 1) << dropName(target) << '(' << value << ");\n";
            }
        }
        out << indentation(depth + 1) << "fdn_dealloc(" << value << ");\n";
        out << indentation(depth + 1) << value << " = NULL;\n";
        out << indentation(depth) << "}\n";
        return;
    }
    if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
        out << indentation(depth) << dropName(type) << "(&" << value << ");\n";
    }
}

void emitMoveAssignment(std::ostringstream &out, const FirProgram &program, const Type &type,
                        const std::string &target, const std::string &source,
                        unsigned int depth) {
    if (type.kind == TypeKind::Own) {
        out << indentation(depth) << target << " = " << source << ";\n";
        out << indentation(depth) << source << " = NULL;\n";
        return;
    }
    if ((type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) &&
        typeRequiresDrop(program, type)) {
        out << indentation(depth) << target << " = " << moveName(type) << "(&" << source
            << ");\n";
        return;
    }
    out << indentation(depth) << target << " = " << source << ";\n";
}

class FunctionEmitter {
  public:
    FunctionEmitter(std::ostringstream &out, const FirProgram &program, const FirFunction &function)
        : out_(out), program_(program), function_(function) {}

    bool emitBlock(FirBlockId id, unsigned int depth) {
        for (const auto statement : function_.blocks[id].statements) {
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
            return {i32Constant(integer->value), false};
        }
        if (const auto *boolean = std::get_if<FirBooleanExpression>(&expression.value)) {
            return {boolean->value ? "true" : "false", false};
        }
        if (const auto *string = std::get_if<FirStringExpression>(&expression.value)) {
            return {cString(string->value), false};
        }
        if (const auto *local = std::get_if<FirLocalExpression>(&expression.value)) {
            return {localName(function_, local->local), false};
        }
        if (const auto *moved = std::get_if<FirMoveExpression>(&expression.value)) {
            return emitMove(moved->local, depth);
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
        if (const auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
            return emitEnum(*constructor, depth);
        }
        return emitMatch(std::get<FirMatchExpression>(expression.value), expression.type,
                         expression.span, depth);
    }

    EmittedExpression emitMove(FirLocalId local, unsigned int depth) {
        const auto &type = function_.locals[local].type;
        const auto source = localName(function_, local);
        return emitMoveValue(type, source, depth);
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
        if (operandType.kind == TypeKind::Own || operandType.kind == TypeKind::View ||
            operandType.kind == TypeKind::Edit) {
            return operand;
        }
        return {"&" + operand.value, false};
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
            out_ << "fdn_i32_add(" << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Subtract:
            out_ << "fdn_i32_subtract(" << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Multiply:
            out_ << "fdn_i32_multiply(" << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Divide:
            out_ << "fdn_i32_divide(" << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Remainder:
            out_ << "fdn_i32_remainder(" << left.value << ", " << right.value << ')';
            break;
        case FirBinaryOperator::Equal:
            out_ << left.value << " == " << right.value;
            break;
        case FirBinaryOperator::NotEqual:
            out_ << left.value << " != " << right.value;
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
        if (call.kind == FirCallKind::Print) {
            invocation << "fdn_println";
        } else if (call.kind == FirCallKind::Panic) {
            invocation << "fdn_panic";
        } else {
            invocation << functionName(program_, call.function);
        }
        invocation << '(';
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index != 0) {
                invocation << ", ";
            }
            invocation << arguments[index];
        }
        invocation << ')';

        if (call.kind == FirCallKind::Panic ||
            (call.kind == FirCallKind::Function &&
             program_.functions[call.function].diverges)) {
            out_ << indentation(depth) << invocation.str() << ";\n";
            return {{}, true};
        }
        if (type == voidType) {
            out_ << indentation(depth) << invocation.str() << ";\n";
            return {{}, false};
        }
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = "
             << invocation.str() << ";\n";
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
        out_ << indentation(depth) << cType(literal.type) << ' ' << temporary << ";\n";
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
        out_ << indentation(depth) << cType(constructor.type) << ' ' << temporary << ";\n";
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
                    out_ << indentation(depth + 1) << cType(function_.locals[local].type) << ' '
                         << localName(function_, local) << " = " << moved.value << ";\n";
                } else {
                    out_ << indentation(depth + 1) << cType(function_.locals[local].type) << ' '
                         << localName(function_, local) << " = " << payload << ";\n";
                }
                out_ << indentation(depth + 1) << "(void)" << localName(function_, local)
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

    bool emitStatement(const FirStatement &statement, unsigned int depth) {
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            const auto initializer = emitExpression(variable->initializer, depth);
            if (initializer.diverges) {
                return true;
            }
            out_ << indentation(depth) << cType(function_.locals[variable->local].type) << ' '
                 << localName(function_, variable->local) << " = " << initializer.value << ";\n";
            out_ << indentation(depth) << "(void)" << localName(function_, variable->local)
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
            out_ << indentation(depth + 1) << cType(function_.locals[binding->errorLocal].type)
                 << ' ' << localName(function_, binding->errorLocal) << " = " << initializer.value
                 << ".fdn_data." << payloadName(1) << ";\n";
            static_cast<void>(emitBlock(binding->elseBlock, depth + 1));
            out_ << indentation(depth) << "}\n";
            out_ << indentation(depth) << cType(function_.locals[binding->local].type) << ' '
                 << localName(function_, binding->local) << " = " << initializer.value
                 << ".fdn_data." << payloadName(0) << ";\n";
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
            emitDropValue(out_, program_, function_.expressions[assignment->target].type,
                          target.value, depth);
            out_ << indentation(depth) << target.value << " = " << value.value << ";\n";
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
                emitAllocationCheck(depth);
                out_ << indentation(depth) << "fdn_frame_leave(&fdn_frame_current);\n";
                out_ << indentation(depth) << "return " << result << ";\n";
            } else {
                emitDrops(returned->drops, depth);
                emitAllocationCheck(depth);
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
            emitDropValue(out_, program_, function_.locals[local].type,
                          localName(function_, local), depth);
        }
    }

    void emitAllocationCheck(unsigned int depth) {
        if (function_.name != "main") {
            return;
        }
        out_ << "#if defined(FOUNDATION_VERIFY_ALLOCATIONS)\n";
        out_ << indentation(depth) << "if (fdn_live_allocations() != 0) {\n";
        out_ << indentation(depth + 1) << "fdn_panic(\"live allocations after main\");\n";
        out_ << indentation(depth) << "}\n";
        out_ << "#endif\n";
    }

    void discardValue(const EmittedExpression &expression, unsigned int depth) {
        if (!expression.value.empty()) {
            out_ << indentation(depth) << "(void)" << expression.value << ";\n";
        }
    }

    std::string nextTemporary() { return "fdn_tmp_" + std::to_string(temporary_++); }

    std::ostringstream &out_;
    const FirProgram &program_;
    const FirFunction &function_;
    std::size_t temporary_{};
};

void emitStructDefinition(std::ostringstream &out, const FirProgram &program, FirStructId id) {
    out << "struct fdn_struct_" << id << " {\n";
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

void emitOwnershipPrototypes(std::ostringstream &out, const FirProgram &program) {
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

void emitStructOwnership(std::ostringstream &out, const FirProgram &program, FirStructId id) {
    const Type type{TypeKind::Struct, id};
    if (!typeRequiresDrop(program, type)) {
        return;
    }

    out << "static inline FDN_MAYBE_UNUSED void " << dropName(type) << '(' << cType(type)
        << " *value) {\n";
    for (std::size_t field = program.structs[id].fields.size(); field-- > 0;) {
        emitDropValue(out, program, program.structs[id].fields[field].type,
                      "value->" + fieldName(field), 1);
    }
    out << "}\n\n";

    out << "static inline FDN_MAYBE_UNUSED " << cType(type) << ' ' << moveName(type) << '('
        << cType(type)
        << " *value) {\n";
    out << "    " << cType(type) << " result;\n";
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
    out << "    " << cType(type) << " result;\n";
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

void emitOwnershipDefinitions(std::ostringstream &out, const FirProgram &program) {
    for (std::size_t id = 0; id < program.structs.size(); ++id) {
        emitStructOwnership(out, program, id);
    }
    for (std::size_t id = 0; id < program.enums.size(); ++id) {
        emitEnumOwnership(out, program, id);
    }
}

std::optional<std::size_t> typeNode(const FirProgram &program, const Type &type) {
    if (type.kind == TypeKind::Struct) {
        return type.declaration;
    }
    if (type.kind == TypeKind::Enum) {
        return program.structs.size() + type.declaration;
    }
    return std::nullopt;
}

void emitSignature(std::ostringstream &out, const FirProgram &program, FirFunctionId id) {
    const auto &function = program.functions[id];
    if (id == program.main) {
        out << "int main(void)";
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

} // namespace

std::string emitC(const FirProgram &source, std::string_view sourcePath) {
    Monomorphizer monomorphizer(source);
    auto program = monomorphizer.run();
    markDivergingFunctions(program);
    std::ostringstream out;
    out << "#include <stdbool.h>\n";
    out << "#include <stdint.h>\n";
    out << "#include \"foundation/runtime.h\"\n\n";
    out << "#if defined(__GNUC__) || defined(__clang__)\n";
    out << "#define FDN_MAYBE_UNUSED __attribute__((unused))\n";
    out << "#else\n";
    out << "#define FDN_MAYBE_UNUSED\n";
    out << "#endif\n\n";
    out << "typedef const char *fdn_string;\n\n";

    for (std::size_t index = 0; index < program.structs.size(); ++index) {
        out << "typedef struct fdn_struct_" << index << " fdn_struct_" << index << ";\n";
    }
    for (std::size_t index = 0; index < program.enums.size(); ++index) {
        out << "typedef struct fdn_enum_" << index << " fdn_enum_" << index << ";\n";
    }
    if (!program.structs.empty() || !program.enums.empty()) {
        out << '\n';
    }

    const auto typeCount = program.structs.size() + program.enums.size();
    std::vector<std::size_t> dependencies(typeCount);
    std::vector<std::vector<std::size_t>> dependents(typeCount);
    for (std::size_t type = 0; type < program.structs.size(); ++type) {
        for (const auto &field : program.structs[type].fields) {
            const auto target = typeNode(program, field.type);
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
            const auto target = typeNode(program, *variant.payload);
            if (target.has_value()) {
                ++dependencies[program.structs.size() + type];
                dependents[*target].push_back(program.structs.size() + type);
            }
        }
    }
    std::vector<FirStructId> ready;
    for (std::size_t type = 0; type < dependencies.size(); ++type) {
        if (dependencies[type] == 0) {
            ready.push_back(type);
        }
    }
    for (std::size_t current = 0; current < ready.size(); ++current) {
        if (ready[current] < program.structs.size()) {
            emitStructDefinition(out, program, ready[current]);
        } else {
            emitEnumDefinition(out, program, ready[current] - program.structs.size());
        }
        for (const auto dependent : dependents[ready[current]]) {
            if (--dependencies[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    if (ready.size() != typeCount) {
        std::terminate();
    }

    emitOwnershipPrototypes(out, program);
    emitOwnershipDefinitions(out, program);

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitSignature(out, program, index);
        out << ";\n";
    }
    out << '\n';

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        emitSignature(out, program, index);
        out << " {\n";
        out << "    fdn_frame fdn_frame_current;\n";
        out << "    fdn_frame_enter(&fdn_frame_current, \"main\", "
            << cString(function.name) << ", " << cString(sourcePath) << ", "
            << function.sourceSpan.line << ", " << function.sourceSpan.column << ");\n";
        FunctionEmitter emitter(out, program, function);
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
    return out.str();
}

} // namespace foundation
