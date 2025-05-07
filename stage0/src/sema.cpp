#include "foundation/sema.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace foundation {

bool analyze(const Program &program, Diagnostics &diagnostics) {
    std::unordered_set<std::string_view> names;
    const Function *main = nullptr;

    for (const auto &function : program.functions) {
        if (!names.insert(function.name).second) {
            diagnostics.error("FDN2001", "duplicate function " + function.name, function.span);
        }
        if (function.name == "main") {
            main = &function;
        } else {
            diagnostics.error("FDN2002", "stage 0 only supports the main function", function.span);
        }
        if (function.returnType != "i32") {
            diagnostics.error("FDN2003", "stage 0 functions must return i32", function.span);
        }
        if (function.statements.empty() ||
            !std::holds_alternative<ReturnStatement>(function.statements.back())) {
            diagnostics.error("FDN2004", "function must end with return", function.span);
        }

        for (const auto &statement : function.statements) {
            const auto *returned = std::get_if<ReturnStatement>(&statement);
            if (returned == nullptr) {
                continue;
            }
            if (returned->value < INT32_MIN || returned->value > INT32_MAX) {
                diagnostics.error("FDN2005", "return value does not fit i32", returned->span);
            }
        }
    }

    if (main == nullptr) {
        diagnostics.error("FDN2006", "program must declare main", {0, 0, 1, 1});
    }
    return !diagnostics.hasErrors();
}

} // namespace foundation
