#include "foundation/codegen.hpp"

#include <cctype>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
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

std::string cType(Type type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "int32_t";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "const char *";
    case TypeKind::Struct:
        return "fdn_struct_" + std::to_string(type.declaration);
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
    return "fdn_fn_" + safeName(program.functions[id].name) + "_" + std::to_string(id);
}

std::string localName(const FirFunction &function, FirLocalId id) {
    return "fdn_local_" + safeName(function.locals[id].name) + "_" + std::to_string(id);
}

std::string fieldName(FirFieldId id) { return "fdn_field_" + std::to_string(id); }

std::string indentation(unsigned int depth) { return std::string(depth * 4, ' '); }

std::string i32Constant(std::int32_t value) {
    if (value == INT32_MIN) {
        return "(-INT32_C(2147483647) - INT32_C(1))";
    }
    return "INT32_C(" + std::to_string(value) + ")";
}

class FunctionEmitter {
  public:
    FunctionEmitter(std::ostringstream &out, const FirProgram &program, const FirFunction &function)
        : out_(out), program_(program), function_(function) {}

    void emitBlock(FirBlockId id, unsigned int depth) {
        for (const auto statement : function_.blocks[id].statements) {
            emitStatement(function_.statements[statement], depth);
        }
    }

  private:
    std::string emitExpression(FirExpressionId id, unsigned int depth) {
        const auto &expression = function_.expressions[id];
        if (const auto *integer = std::get_if<FirIntegerExpression>(&expression.value)) {
            return i32Constant(integer->value);
        }
        if (const auto *boolean = std::get_if<FirBooleanExpression>(&expression.value)) {
            return boolean->value ? "true" : "false";
        }
        if (const auto *string = std::get_if<FirStringExpression>(&expression.value)) {
            return cString(string->value);
        }
        if (const auto *local = std::get_if<FirLocalExpression>(&expression.value)) {
            return localName(function_, local->local);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
            const auto operand = emitExpression(unary->operand, depth);
            const auto temporary = nextTemporary();
            out_ << indentation(depth) << cType(expression.type) << ' ' << temporary << " = ";
            if (unary->operation == FirUnaryOperator::Negate) {
                out_ << "fdn_i32_negate(" << operand << ");\n";
            } else {
                out_ << '!' << operand << ";\n";
            }
            return temporary;
        }
        if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
            return emitBinary(*binary, expression.type, depth);
        }
        if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
            return emitCall(*call, expression.type, depth);
        }
        if (const auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
            return emitStruct(*literal, depth);
        }
        const auto &field = std::get<FirFieldExpression>(expression.value);
        return emitExpression(field.base, depth) + "." + fieldName(field.field);
    }

    std::string emitBinary(const FirBinaryExpression &binary, Type type, unsigned int depth) {
        const auto left = emitExpression(binary.left, depth);
        if (binary.operation == FirBinaryOperator::And ||
            binary.operation == FirBinaryOperator::Or) {
            const auto temporary = nextTemporary();
            out_ << indentation(depth) << "bool " << temporary << " = " << left << ";\n";
            const auto condition =
                binary.operation == FirBinaryOperator::And ? temporary : "!" + temporary;
            out_ << indentation(depth) << "if (" << condition << ") {\n";
            const auto right = emitExpression(binary.right, depth + 1);
            out_ << indentation(depth + 1) << temporary << " = " << right << ";\n";
            out_ << indentation(depth) << "}\n";
            return temporary;
        }

        const auto right = emitExpression(binary.right, depth);
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = ";
        switch (binary.operation) {
        case FirBinaryOperator::Add:
            out_ << "fdn_i32_add(" << left << ", " << right << ')';
            break;
        case FirBinaryOperator::Subtract:
            out_ << "fdn_i32_subtract(" << left << ", " << right << ')';
            break;
        case FirBinaryOperator::Multiply:
            out_ << "fdn_i32_multiply(" << left << ", " << right << ')';
            break;
        case FirBinaryOperator::Divide:
            out_ << "fdn_i32_divide(" << left << ", " << right << ')';
            break;
        case FirBinaryOperator::Remainder:
            out_ << "fdn_i32_remainder(" << left << ", " << right << ')';
            break;
        case FirBinaryOperator::Equal:
            out_ << left << " == " << right;
            break;
        case FirBinaryOperator::NotEqual:
            out_ << left << " != " << right;
            break;
        case FirBinaryOperator::Less:
            out_ << left << " < " << right;
            break;
        case FirBinaryOperator::LessEqual:
            out_ << left << " <= " << right;
            break;
        case FirBinaryOperator::Greater:
            out_ << left << " > " << right;
            break;
        case FirBinaryOperator::GreaterEqual:
            out_ << left << " >= " << right;
            break;
        case FirBinaryOperator::And:
        case FirBinaryOperator::Or:
            break;
        }
        out_ << ";\n";
        return temporary;
    }

    std::string emitCall(const FirCallExpression &call, Type type, unsigned int depth) {
        std::vector<std::string> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(emitExpression(argument, depth));
        }

        std::ostringstream invocation;
        if (call.kind == FirCallKind::Print) {
            invocation << "fdn_println";
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

        if (type == voidType) {
            out_ << indentation(depth) << invocation.str() << ";\n";
            return {};
        }
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(type) << ' ' << temporary << " = "
             << invocation.str() << ";\n";
        return temporary;
    }

    std::string emitStruct(const FirStructExpression &literal, unsigned int depth) {
        const auto temporary = nextTemporary();
        out_ << indentation(depth) << cType(Type{TypeKind::Struct, literal.type}) << ' '
             << temporary << ";\n";
        for (const auto &field : literal.fields) {
            const auto value = emitExpression(field.value, depth);
            out_ << indentation(depth) << temporary << '.' << fieldName(field.field) << " = "
                 << value << ";\n";
        }
        return temporary;
    }

    void emitStatement(const FirStatement &statement, unsigned int depth) {
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            const auto initializer = emitExpression(variable->initializer, depth);
            out_ << indentation(depth) << cType(function_.locals[variable->local].type) << ' '
                 << localName(function_, variable->local) << " = " << initializer << ";\n";
            out_ << indentation(depth) << "(void)" << localName(function_, variable->local)
                 << ";\n";
            return;
        }
        if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            const auto value = emitExpression(assignment->value, depth);
            out_ << indentation(depth) << localName(function_, assignment->local) << " = " << value
                 << ";\n";
            return;
        }
        if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            const auto value = emitExpression(expression->expression, depth);
            if (!value.empty()) {
                out_ << indentation(depth) << "(void)" << value << ";\n";
            }
            return;
        }
        if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
            if (returned->value.has_value()) {
                const auto value = emitExpression(*returned->value, depth);
                out_ << indentation(depth) << "return " << value << ";\n";
            } else {
                out_ << indentation(depth) << "return;\n";
            }
            return;
        }
        if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            const auto condition = emitExpression(branch->condition, depth);
            out_ << indentation(depth) << "if (" << condition << ") {\n";
            emitBlock(branch->thenBlock, depth + 1);
            out_ << indentation(depth) << '}';
            if (branch->elseBlock.has_value()) {
                out_ << " else {\n";
                emitBlock(*branch->elseBlock, depth + 1);
                out_ << indentation(depth) << '}';
            }
            out_ << '\n';
            return;
        }

        const auto &loop = std::get<FirWhileStatement>(statement.value);
        out_ << indentation(depth) << "while (true) {\n";
        const auto condition = emitExpression(loop.condition, depth + 1);
        out_ << indentation(depth + 1) << "if (!" << condition << ") {\n";
        out_ << indentation(depth + 2) << "break;\n";
        out_ << indentation(depth + 1) << "}\n";
        emitBlock(loop.body, depth + 1);
        out_ << indentation(depth) << "}\n";
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

void emitSignature(std::ostringstream &out, const FirProgram &program, FirFunctionId id) {
    const auto &function = program.functions[id];
    if (id == program.main) {
        out << "int main(void)";
        return;
    }

    out << cType(function.returnType) << ' ' << functionName(program, id) << '(';
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

std::string emitC(const FirProgram &program) {
    std::ostringstream out;
    out << "#include <stdbool.h>\n";
    out << "#include <stdint.h>\n";
    out << "#include \"foundation/runtime.h\"\n\n";

    for (std::size_t index = 0; index < program.structs.size(); ++index) {
        out << "typedef struct fdn_struct_" << index << " fdn_struct_" << index << ";\n";
    }
    if (!program.structs.empty()) {
        out << '\n';
    }

    std::vector<std::size_t> dependencies(program.structs.size());
    std::vector<std::vector<FirStructId>> dependents(program.structs.size());
    for (std::size_t type = 0; type < program.structs.size(); ++type) {
        for (const auto &field : program.structs[type].fields) {
            if (field.type.kind == TypeKind::Struct) {
                ++dependencies[type];
                dependents[field.type.declaration].push_back(type);
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
        emitStructDefinition(out, program, ready[current]);
        for (const auto dependent : dependents[ready[current]]) {
            if (--dependencies[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    if (ready.size() != program.structs.size()) {
        std::terminate();
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        emitSignature(out, program, index);
        out << ";\n";
    }
    out << '\n';

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        emitSignature(out, program, index);
        out << " {\n";
        FunctionEmitter emitter(out, program, function);
        for (const auto parameter : function.parameters) {
            out << "    (void)" << localName(function, parameter) << ";\n";
        }
        emitter.emitBlock(function.body, 1);
        out << "}\n";
        if (index + 1 != program.functions.size()) {
            out << '\n';
        }
    }
    return out.str();
}

} // namespace foundation
