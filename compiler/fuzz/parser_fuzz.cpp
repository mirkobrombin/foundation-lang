#include "foundation/codegen.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/lexer.hpp"
#include "foundation/lower.hpp"
#include "foundation/parser.hpp"
#include "foundation/sema.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const auto source = std::string_view(reinterpret_cast<const char *>(data), size);
    foundation::Diagnostics diagnostics;
    foundation::Lexer lexer(source, diagnostics);
    foundation::Parser parser(lexer.scan(), diagnostics);
    const auto program = parser.parse();
    if (diagnostics.hasErrors()) {
        return 0;
    }
    const auto semantic = foundation::analyze(program, diagnostics);
    if (semantic.has_value()) {
        static_cast<void>(foundation::emitC(foundation::lower(program, *semantic)));
    }
    return 0;
}
