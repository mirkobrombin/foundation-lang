#include "foundation/codegen.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/lexer.hpp"
#include "foundation/parser.hpp"
#include "foundation/sema.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct ParsedProgram {
    foundation::Program program;
    foundation::Diagnostics diagnostics;
};

ParsedProgram parse(std::string_view source) {
    ParsedProgram result;
    foundation::Lexer lexer(source, result.diagnostics);
    foundation::Parser parser(lexer.scan(), result.diagnostics);
    result.program = parser.parse();
    if (!result.diagnostics.hasErrors()) {
        static_cast<void>(foundation::analyze(result.program, result.diagnostics));
    }
    return result;
}

bool hasCode(const foundation::Diagnostics &diagnostics, std::string_view code) {
    for (const auto &diagnostic : diagnostics.all()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

int failures{};

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void acceptedProgramEmitsDeterministicC() {
    constexpr std::string_view source =
        "fn main() -> i32 {\n  print(\"hello\\nworld\");\n  return 0;\n}\n";
    auto first = parse(source);
    auto second = parse(source);

    expect(!first.diagnostics.hasErrors(), "accepted program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated parse has no diagnostics");
    if (first.diagnostics.hasErrors() || second.diagnostics.hasErrors()) {
        return;
    }

    const auto firstC = foundation::emitC(first.program);
    const auto secondC = foundation::emitC(second.program);
    expect(firstC == secondC, "C emission is deterministic");
    expect(firstC.find("fdn_println(\"hello\\nworld\")") != std::string::npos,
           "string escape survives C emission");
    expect(firstC.find("return (int32_t)0;") != std::string::npos,
           "i32 return is emitted");
}

void missingSemicolonHasStableDiagnostic() {
    const auto result = parse("fn main() -> i32 { print(\"bad\") return 0; }");
    expect(hasCode(result.diagnostics, "FDN1014"),
           "missing print semicolon reports FDN1014");
}

void invalidCharacterScanDoesNotRecurse() {
    std::string source(100000, '@');
    source += "fn main() -> i32 { return 0; }";
    const auto result = parse(source);
    expect(result.diagnostics.all().size() == 100000,
           "invalid bytes each produce one lexical diagnostic");
    expect(hasCode(result.diagnostics, "FDN0001"),
           "invalid bytes report FDN0001");
}

void semanticFailuresHaveStableDiagnostics() {
    const auto noMain = parse("fn worker() -> i32 { return 0; }");
    expect(hasCode(noMain.diagnostics, "FDN2002"), "non-main function reports FDN2002");
    expect(hasCode(noMain.diagnostics, "FDN2006"), "missing main reports FDN2006");

    const auto wide = parse("fn main() -> i32 { return 2147483648; }");
    expect(hasCode(wide.diagnostics, "FDN2005"), "wide i32 return reports FDN2005");
}

} // namespace

int main() {
    acceptedProgramEmitsDeterministicC();
    missingSemicolonHasStableDiagnostic();
    invalidCharacterScanDoesNotRecurse();
    semanticFailuresHaveStableDiagnostics();

    if (failures != 0) {
        std::cerr << failures << " test assertions failed\n";
        return 1;
    }
    std::cout << "stage0 compiler tests passed\n";
    return 0;
}
