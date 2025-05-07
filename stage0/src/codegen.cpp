#include "foundation/codegen.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <variant>

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

} // namespace

std::string emitC(const Program &program) {
    std::ostringstream out;
    out << "#include <stdint.h>\n";
    out << "#include \"foundation/runtime.h\"\n\n";

    const auto &main = program.functions.front();
    out << "int main(void) {\n";
    for (const auto &statement : main.statements) {
        if (const auto *print = std::get_if<PrintStatement>(&statement)) {
            out << "    fdn_println(" << cString(print->value) << ");\n";
            continue;
        }
        const auto &returned = std::get<ReturnStatement>(statement);
        out << "    return (int32_t)" << returned.value << ";\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace foundation
