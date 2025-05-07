#include "foundation/codegen.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/lexer.hpp"
#include "foundation/lower.hpp"
#include "foundation/parser.hpp"
#include "foundation/sema.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct CheckedProgram {
    foundation::Program program;
    std::optional<foundation::SemanticModel> semantic;
    std::optional<foundation::FirProgram> fir;
    foundation::Diagnostics diagnostics;
};

CheckedProgram check(std::string_view source) {
    CheckedProgram result;
    foundation::Lexer lexer(source, result.diagnostics);
    foundation::Parser parser(lexer.scan(), result.diagnostics);
    result.program = parser.parse();
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    result.semantic = foundation::analyze(result.program, result.diagnostics);
    if (result.semantic.has_value()) {
        result.fir = foundation::lower(result.program, *result.semantic);
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

void typedProgramLowersToDeterministicC() {
    constexpr std::string_view source = R"(
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn main() -> i32 {
    let message: String = "hello\nworld";
    print(message);
    var total: i32 = 0;
    while total < 3 {
        total = total + 1;
    }
    if total == add(1, 2) {
        return 0;
    } else {
        return 1;
    }
}
)";
    auto first = check(source);
    auto second = check(source);

    expect(!first.diagnostics.hasErrors(), "typed program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated typed program has no diagnostics");
    expect(first.fir.has_value(), "typed program lowers to FIR");
    expect(second.fir.has_value(), "repeated typed program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "FIR C emission is deterministic");
    expect(firstC.find("fdn_i32_add") != std::string::npos,
           "i32 addition uses the checked runtime operation");
    expect(firstC.find("while (true)") != std::string::npos,
           "structured loop is emitted from FIR");
    expect(firstC.find("fdn_fn_add_0") != std::string::npos,
           "user function call uses a stable C name");
}

void structValuesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
struct Point {
    x: i32
    y: i32
}

fn move(point: Point, x: i32) -> Point {
    return Point { y: point.y x: x };
}

fn main() -> i32 {
    let start = Point { x: 1 y: 2 };
    var current: Point = start;
    current = move(current, 3);
    return current.x;
}
)";
    auto first = check(source);
    auto second = check(source);

    expect(!first.diagnostics.hasErrors(), "struct program has no diagnostics");
    expect(first.fir.has_value(), "struct program lowers to FIR");
    expect(second.fir.has_value(), "repeated struct program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "struct C emission is deterministic");
    expect(firstC.find("typedef struct fdn_struct_0") != std::string::npos,
           "struct type has a stable C declaration");
    expect(firstC.find(".fdn_field_0") != std::string::npos,
           "field access uses a stable C field name");
}

void deepStructGraphsStayIterative() {
    constexpr int typeCount = 2048;
    std::string source;
    for (int index = 0; index + 1 < typeCount; ++index) {
        source += "struct Type" + std::to_string(index) + " { next: Type" +
                  std::to_string(index + 1) + " }\n";
    }
    source += "struct Type" + std::to_string(typeCount - 1) + " { value: i32 }\n";
    source += "fn main() -> i32 { return 0; }\n";

    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "deep acyclic struct graph has no diagnostics");
    expect(result.fir.has_value(), "deep acyclic struct graph lowers to FIR");
    if (result.fir.has_value()) {
        const auto generated = foundation::emitC(*result.fir);
        expect(generated.find("struct fdn_struct_2047") != std::string::npos,
               "deep acyclic struct graph emits every declaration");
    }
}

void enumMatchesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
enum Value {
    Empty
    Number(value: i32)
}

fn read(value: Value) -> i32 {
    return match value {
        Value::Empty => 0
        Value::Number(number) => number
    };
}

fn main() -> i32 {
    let value = Value::Number(3);
    return read(value) - 3;
}
)";
    auto first = check(source);
    auto second = check(source);

    expect(!first.diagnostics.hasErrors(), "enum program has no diagnostics");
    expect(first.fir.has_value(), "enum program lowers to FIR");
    expect(second.fir.has_value(), "repeated enum program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "enum C emission is deterministic");
    expect(firstC.find("FDN_ENUM_0_VARIANT_1") != std::string::npos,
           "enum variant has a stable C tag");
    expect(firstC.find("switch (") != std::string::npos,
           "match expression emits a C switch");
}

void syntaxFailuresHaveStableDiagnostics() {
    const auto missingSemicolon = check("fn main() -> i32 { print(\"bad\") return 0; }");
    expect(hasCode(missingSemicolon.diagnostics, "FDN1014"),
           "missing expression semicolon reports FDN1014");

    std::string invalid(100000, '@');
    invalid += "fn main() -> i32 { return 0; }";
    const auto invalidCharacters = check(invalid);
    expect(invalidCharacters.diagnostics.all().size() == 101,
           "invalid byte diagnostics stop at the global error limit");
    expect(hasCode(invalidCharacters.diagnostics, "FDN0001"),
           "invalid bytes report FDN0001");
    expect(hasCode(invalidCharacters.diagnostics, "FDN0000"),
           "the diagnostic limit reports FDN0000");

    std::string nul = "fn main() -> i32 { print(\"";
    nul.push_back('\0');
    nul += "\"); return 0; }";
    const auto nulString = check(nul);
    expect(hasCode(nulString.diagnostics, "FDN0004"),
           "NUL in a string literal reports FDN0004");

    std::string deepUnary = "fn main() -> i32 { return ";
    deepUnary.append(4096, '!');
    deepUnary += "true; }";
    const auto nestedExpression = check(deepUnary);
    expect(hasCode(nestedExpression.diagnostics, "FDN1029"),
           "deep expression nesting reports FDN1029");

    std::string wideExpression = "fn main() -> i32 { return 0";
    for (int index = 0; index < 4096; ++index) {
        wideExpression += " + 1";
    }
    wideExpression += "; }";
    const auto complexExpression = check(wideExpression);
    expect(hasCode(complexExpression.diagnostics, "FDN1029"),
           "complex expression reports FDN1029");

    std::string deepBlocks = "fn main() -> i32 {";
    for (int index = 0; index < 512; ++index) {
        deepBlocks += "if true {";
    }
    deepBlocks += "return 0;";
    for (int index = 0; index < 512; ++index) {
        deepBlocks += '}';
    }
    deepBlocks += '}';
    const auto nestedBlocks = check(deepBlocks);
    expect(hasCode(nestedBlocks.diagnostics, "FDN1030"),
           "deep block nesting reports FDN1030");
}

void semanticFailuresHaveStableDiagnostics() {
    const auto noMain = check("fn worker() -> i32 { return 0; }");
    expect(hasCode(noMain.diagnostics, "FDN2006"), "missing main reports FDN2006");

    const auto wide = check("fn main() -> i32 { return 2147483648; }");
    expect(hasCode(wide.diagnostics, "FDN2005"), "wide i32 literal reports FDN2005");

    const auto minimum = check("fn main() -> i32 { return -2147483648; }");
    expect(!minimum.diagnostics.hasErrors(), "minimum i32 literal is accepted");

    const auto immutable =
        check("fn main() -> i32 { let value: i32 = 1; value = 2; return value; }");
    expect(hasCode(immutable.diagnostics, "FDN2013"),
           "immutable assignment reports FDN2013");

    const auto mismatch = check("fn main() -> i32 { let value: bool = 1; return 0; }");
    expect(hasCode(mismatch.diagnostics, "FDN2011"), "type mismatch reports FDN2011");

    const auto unknown = check("fn main() -> i32 { missing(); return 0; }");
    expect(hasCode(unknown.diagnostics, "FDN2009"), "unknown function reports FDN2009");

    const auto duplicate =
        check("fn main() -> i32 { let value = 1; let value = 2; return value; }");
    expect(hasCode(duplicate.diagnostics, "FDN2003"),
           "duplicate binding reports FDN2003");

    const auto unknownBinding = check("fn main() -> i32 { return value; }");
    expect(hasCode(unknownBinding.diagnostics, "FDN2004"),
           "unknown binding reports FDN2004");

    const auto wrongArity = check(R"(
fn take(value: i32) -> i32 { return value; }
fn main() -> i32 { return take(); }
)");
    expect(hasCode(wrongArity.diagnostics, "FDN2010"),
           "wrong function arity reports FDN2010");

    const auto reservedPrint = check(R"(
fn print() -> void { return; }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(reservedPrint.diagnostics, "FDN2018"),
           "reserved print declaration reports FDN2018");

    const auto mainCall = check("fn main() -> i32 { return main(); }");
    expect(hasCode(mainCall.diagnostics, "FDN2019"), "calling main reports FDN2019");

    const auto voidParameter = check(R"(
fn consume(value: void) -> void { return; }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(voidParameter.diagnostics, "FDN2016"),
           "void parameter reports FDN2016");

    const auto voidReturnValue = check(R"(
fn consume() -> void { return print("value"); }
fn main() -> i32 { consume(); return 0; }
)");
    expect(hasCode(voidReturnValue.diagnostics, "FDN2015"),
           "void return value reports FDN2015");

    const auto fallthrough = check("fn main() -> i32 { let value = 1; }");
    expect(hasCode(fallthrough.diagnostics, "FDN2008"),
           "fallthrough in non-void function reports FDN2008");

    const auto duplicateType = check(R"(
struct Item { value: i32 }
struct Item { other: i32 }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(duplicateType.diagnostics, "FDN2020"),
           "duplicate struct type reports FDN2020");

    const auto duplicateField = check(R"(
struct Item { value: i32 value: bool }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(duplicateField.diagnostics, "FDN2021"),
           "duplicate struct field reports FDN2021");

    const auto recursiveStruct = check(R"(
struct Node { next: Node }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(recursiveStruct.diagnostics, "FDN2023"),
           "recursive value struct reports FDN2023");

    const auto unknownField = check(R"(
struct Item { value: i32 }
fn main() -> i32 { let item = Item { value: 1 }; return item.missing; }
)");
    expect(hasCode(unknownField.diagnostics, "FDN2025"),
           "unknown struct field reports FDN2025");

    const auto duplicateInitializer = check(R"(
struct Item { value: i32 }
fn main() -> i32 { let item = Item { value: 1 value: 2 }; return 0; }
)");
    expect(hasCode(duplicateInitializer.diagnostics, "FDN2026"),
           "duplicate field initializer reports FDN2026");

    const auto nonStructAccess = check("fn main() -> i32 { let value = 1; return value.field; }");
    expect(hasCode(nonStructAccess.diagnostics, "FDN2028"),
           "field access on a primitive reports FDN2028");

    const auto missingPayload = check(R"(
enum Value { Empty Number(value: i32) }
fn main() -> i32 { let value = Value::Number; return 0; }
)");
    expect(hasCode(missingPayload.diagnostics, "FDN2036"),
           "missing enum payload reports FDN2036");

    const auto duplicatePattern = check(R"(
enum Value { Empty Number(value: i32) }
fn main() -> i32 {
    let value = Value::Empty;
    return match value {
        Value::Empty => 0
        Value::Empty => 1
        Value::Number(number) => number
    };
}
)");
    expect(hasCode(duplicatePattern.diagnostics, "FDN2039"),
           "duplicate match pattern reports FDN2039");

    const auto valueCycle = check(R"(
struct Node { state: State }
enum State { End Next(node: Node) }
fn main() -> i32 { return 0; }
)");
    expect(hasCode(valueCycle.diagnostics, "FDN2023"),
           "struct and enum value cycle reports FDN2023");
}

void diagnosticsBoundLongSourceExcerpts() {
    std::string source(4096, 'x');
    foundation::Diagnostics diagnostics;
    diagnostics.error("FDN9999", "bounded diagnostic", {4000, 4096, 1, 4001});

    const auto rendered = foundation::renderDiagnostics("long.fdn", source, diagnostics);
    expect(rendered.size() < 512, "long diagnostic source excerpts are bounded");
    expect(rendered.find("...") != std::string::npos,
           "bounded diagnostic source excerpts mark omitted text");
    expect(rendered.find('^') != std::string::npos,
           "bounded diagnostic source excerpts retain the marker");

    foundation::Diagnostics newlineDiagnostics;
    newlineDiagnostics.error("FDN9998", "newline diagnostic", {5, 1, 1, 6});
    const auto newline = foundation::renderDiagnostics("newline.fdn", "value\nnext",
                                                       newlineDiagnostics);
    expect(newline.find("1 | value") != std::string::npos,
           "diagnostic at a newline renders the preceding source line");
}

} // namespace

int main() {
    typedProgramLowersToDeterministicC();
    structValuesLowerToDeterministicC();
    deepStructGraphsStayIterative();
    enumMatchesLowerToDeterministicC();
    syntaxFailuresHaveStableDiagnostics();
    semanticFailuresHaveStableDiagnostics();
    diagnosticsBoundLongSourceExcerpts();

    if (failures != 0) {
        std::cerr << failures << " test assertions failed\n";
        return 1;
    }
    std::cout << "stage0 compiler tests passed\n";
    return 0;
}
