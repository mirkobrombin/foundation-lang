#include "foundation/codegen.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/lexer.hpp"
#include "foundation/lower.hpp"
#include "foundation/parser.hpp"
#include "foundation/project.hpp"
#include "foundation/sema.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

std::size_t countCode(const foundation::Diagnostics &diagnostics, std::string_view code) {
    std::size_t count{};
    for (const auto &diagnostic : diagnostics.all()) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
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
fn add(left i32, right i32) i32 {
    return left + right
}

fn main() i32 {
    let message String = "hello\nworld"
    print(message)
    var total i32 = 0
    while total < 3 {
        total = total + 1
    }
    if total == add(1, 2) {
        return 0
    } else {
        return 1
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
    x i32
    y i32
}

fn move(point Point, x i32) Point {
    return Point { y = point.y x = x }
}

fn main() i32 {
    let start = Point { x = 1 y = 2 }
    var current Point = start
    current = move(current, 3)
    return current.x
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
        source += "struct Type" + std::to_string(index) + " { next Type" +
                  std::to_string(index + 1) + " }\n";
    }
    source += "struct Type" + std::to_string(typeCount - 1) + " { value i32 }\n";
    source += "fn main() i32 { 0 }\n";

    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "deep acyclic struct graph has no diagnostics");
    expect(result.fir.has_value(), "deep acyclic struct graph lowers to FIR");
    if (result.fir.has_value()) {
        const auto generated = foundation::emitC(*result.fir);
        expect(generated.find("struct fdn_struct_") == std::string::npos,
               "unreachable value types are not emitted");
    }
}

void enumMatchesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
enum Value {
    Empty
    Number(i32)
}

fn read(value Value) i32 {
    match value {
        Empty: 0
        Number(number): number
    }
}

fn main() i32 {
    let value = Value.Number(3)
    return read(value) - 3
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
    expect(firstC.find("VARIANT_1") != std::string::npos,
           "enum variant has a stable C tag");
    expect(firstC.find("switch (") != std::string::npos,
           "match expression emits a C switch");
}

void genericValuesMonomorphizeDeterministically() {
    constexpr std::string_view source = R"(
struct Box<T> { value T }
struct Nested { value Box<Box<i32>> }
enum Choice<T> { None Some(T) }
fn identity<T>(value T) T { return value }
fn main() i32 {
    let nested = Box { value = Box { value = 3 } }
    let number = Choice.Some(identity(3))
    let flag = Choice.Some(identity(true))
    if identity(true) {
        return match number {
            None: 1
            Some(value): value - nested.value.value
        }
    } else {
        return 1
    }
}

)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "generic program has no diagnostics");
    expect(first.fir.has_value(), "generic program lowers to FIR");
    expect(second.fir.has_value(), "repeated generic program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "generic C emission is deterministic");
    expect(firstC.find("fdn_fn_identity_0_g") != std::string::npos,
           "generic function has a specialization name");
    expect(firstC.find("typedef struct fdn_enum_1") != std::string::npos,
           "distinct generic enum applications have distinct C types");
}

void ownershipLowersToDeterministicC() {
    constexpr std::string_view source = R"(
struct User { id i32 }
struct Holder { user own User }

fn read(user view User) i32 { user.id }
fn replace(user edit User, id i32) void { user.id = id }

fn main() i32 {
    var user = own User { id = 3 }
    replace(edit user, 4)
    let value = read(view user)
    let holder = Holder { user = user }
    let moved = holder
    discard moved
    value - 4
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "ownership program has no diagnostics");
    expect(first.fir.has_value(), "ownership program lowers to FIR");
    expect(second.fir.has_value(), "repeated ownership program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "ownership C emission is deterministic");
    expect(firstC.find("fdn_alloc(sizeof") != std::string::npos,
           "own allocation uses the runtime allocator");
    expect(firstC.find("fdn_drop_struct_") != std::string::npos,
           "owned composites receive drop glue");
    expect(firstC.find("fdn_move_struct_") != std::string::npos,
           "owned composites receive move glue");
    expect(firstC.find(" = NULL;") != std::string::npos,
           "moves invalidate their source storage");
    expect(firstC.find("const fdn_struct_") != std::string::npos,
           "view parameters lower to const pointers");
    expect(firstC.find("FOUNDATION_VERIFY_ALLOCATIONS") != std::string::npos,
           "main can verify that deterministic cleanup reaches zero live allocations");
}

void sequenceValuesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
struct Batch<T> { values [2]T }

fn first(values view [String]) void { print(values[0]) }

fn main() i32 {
    let numbers = Batch { values = [1, 2] }
    discard numbers
    let text = Batch { values = ["left", "right"] }
    discard text
    var values = ["A\0B", "C"]
    first(view values)
    values[1] = values[0] + values[1]
    discard values
    let empty [0]i32 = []
    discard empty
    0
}

)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "sequence program has no diagnostics");
    expect(first.fir.has_value(), "sequence program lowers to FIR");
    expect(second.fir.has_value(), "repeated sequence program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "sequence C emission is deterministic");
    expect(firstC.find("fdn_string_static(\"A\\000B\", 3)") != std::string::npos,
           "String literals preserve embedded NUL bytes and length");
    expect(firstC.find("fdn_view_slice_string") != std::string::npos,
           "String slices use a typed fat pointer");
    expect(firstC.find("fdn_array_2_i32") != std::string::npos &&
               firstC.find("fdn_array_2_string") != std::string::npos,
           "generic fields specialize distinct fixed-array layouts");
    expect(firstC.find("fdn_bounds_check") != std::string::npos,
           "array and slice indexing use runtime bounds checks");
    expect(firstC.find("fdn_drop_array_2_string") != std::string::npos,
           "String arrays receive element drop glue");
}

void methodsAndContractsLowerToDeterministicC() {
    constexpr std::string_view source = R"(
contract Readable {
    fn read(view) i32
}

struct Value implements Readable {
    value i32

    fn read(view) i32 {
        self.value
    }
}

fn readAny(value view Readable) i32 {
    value.read()
}

fn main() i32 {
    let value = Value { value = 42 }
    readAny(view value) - value.read()
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "method and contract program has no diagnostics");
    expect(first.fir.has_value(), "method and contract program lowers to FIR");
    expect(second.fir.has_value(), "repeated method and contract program lowers to FIR");
    expect(first.program.contracts.size() == 1,
           "contract declaration is retained in the AST");
    expect(first.program.functions.size() == 3 &&
               first.program.functions.front().receiver.has_value(),
           "struct method is retained as a receiver function");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "method and contract C emission is deterministic");
    expect(firstC.find("struct fdn_contract_0_vtable") != std::string::npos,
           "borrowed contract receives a typed C vtable");
    expect(firstC.find(".fdn_vtable->fdn_method_0") != std::string::npos,
           "contract method call dispatches through its vtable");
    expect(firstC.find("fdn_vtable_c0_s0_m0") != std::string::npos,
           "contract implementation receives a deterministic adapter");
    expect(firstC.find("fdn_alloc") == std::string::npos,
           "contract conversion does not allocate");
}

void lightweightSyntaxCarriesVisibilityAndContext() {
    constexpr std::string_view source = R"(
struct Holder {
    Value i32
    hidden bool
}

enum Choice<T> {
    None
    Some(T)
    hidden
}

fn MakeChoice() Choice<i32> {
    .Some(4)
}

fn main() i32 {
    let Value = Holder { Value = 0 hidden = false }
    let none Choice<i32> = .None
    let chosen = MakeChoice()
    match chosen {
        None: Value.Value + 1
        Some(number): number - 4
        hidden: 2
    }
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "lightweight source has no diagnostics");
    expect(result.fir.has_value(), "lightweight source lowers to FIR");
    expect(result.program.structs.size() == 1 && result.program.structs[0].exported,
           "uppercase type is exported");
    expect(result.program.structs.size() == 1 && result.program.structs[0].fields[0].exported &&
               !result.program.structs[0].fields[1].exported,
           "field visibility follows the initial letter");
    expect(result.program.enums.size() == 3 && result.program.enums[2].variants[0].exported &&
               !result.program.enums[2].variants[2].exported,
           "variant visibility follows the initial letter");
    expect(result.program.functions.size() == 2 && result.program.functions[0].exported &&
               !result.program.functions[1].exported,
           "function visibility follows the initial letter");
    if (result.fir.has_value()) {
        expect(result.fir->structs[0].exported && result.fir->structs[0].fields[0].exported,
               "visibility survives FIR lowering");
    }
}

void panicLowersWithSourceFrames() {
    constexpr std::string_view source = R"(
fn crash(message String) i32 {
    panic(message)
}

fn main() i32 {
    crash("failed")
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "panic program has no diagnostics");
    expect(result.fir.has_value(), "panic program lowers to FIR");
    if (!result.fir.has_value()) {
        return;
    }
    const auto generated = foundation::emitC(*result.fir, "panic.fdn");
    expect(generated.find("fdn_panic") != std::string::npos,
           "panic lowers to the fatal runtime operation");
    expect(generated.find("fdn_frame_enter") != std::string::npos,
           "generated functions enter Foundation frames");
    expect(generated.find("\"panic.fdn\"") != std::string::npos,
           "generated frames retain the source path");
    expect(generated.find("return ;") == std::string::npos,
           "panic closes a non-void function without a C return value");
}

void divergingCallsCloseGeneratedControlFlow() {
    constexpr std::string_view source = R"(
fn stop() i32 {
    panic("stop")
}

fn calculate() i32 {
    1 + stop()
}

fn noOperation() void {
    return
}

fn main() i32 {
    calculate()
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "diverging call program has no diagnostics");
    expect(result.fir.has_value(), "diverging call program lowers to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "diverging.fdn");
    expect(generated.find("_Noreturn void fdn_fn_stop_0") != std::string::npos,
           "direct panic marks its Foundation function as diverging");
    expect(generated.find("_Noreturn void fdn_fn_calculate_1") != std::string::npos,
           "divergence propagates through a user function call");
    expect(generated.find("fdn_i32_add") == std::string::npos,
           "code after a diverging operand is not emitted");
    expect(generated.find("return;\n    fdn_frame_leave") == std::string::npos,
           "explicit return does not emit a second frame epilogue");
}

void syntaxFailuresHaveStableDiagnostics() {
    const auto legacySemicolon = check("fn main() i32 { print(\"bad\"); 0 }");
    expect(hasCode(legacySemicolon.diagnostics, "FDN0001"),
           "legacy semicolon reports FDN0001");

    std::string invalid(100000, '@');
    invalid += "fn main() i32 { 0 }";
    const auto invalidCharacters = check(invalid);
    expect(invalidCharacters.diagnostics.all().size() == 101,
           "invalid byte diagnostics stop at the global error limit");
    expect(hasCode(invalidCharacters.diagnostics, "FDN0001"),
           "invalid bytes report FDN0001");
    expect(hasCode(invalidCharacters.diagnostics, "FDN0000"),
           "the diagnostic limit reports FDN0000");

    std::string nul = "fn main() i32 { print(\"";
    nul.push_back('\0');
    nul += "\") 0 }";
    const auto nulString = check(nul);
    expect(hasCode(nulString.diagnostics, "FDN0004"),
           "NUL in a string literal reports FDN0004");

    std::string invalidUtf8 = "fn main() i32 { print(\"";
    invalidUtf8.push_back(static_cast<char>(0xc3));
    invalidUtf8 += "\") 0 }";
    const auto invalidUtf8String = check(invalidUtf8);
    expect(hasCode(invalidUtf8String.diagnostics, "FDN0005"),
           "invalid UTF-8 in a string literal reports FDN0005");

    std::string deepUnary = "fn main() i32 { return ";
    deepUnary.append(4096, '!');
    deepUnary += "true }";
    const auto nestedExpression = check(deepUnary);
    expect(hasCode(nestedExpression.diagnostics, "FDN1029"),
           "deep expression nesting reports FDN1029");

    std::string wideExpression = "fn main() i32 { return 0";
    for (int index = 0; index < 4096; ++index) {
        wideExpression += " + 1";
    }
    wideExpression += " }";
    const auto complexExpression = check(wideExpression);
    expect(hasCode(complexExpression.diagnostics, "FDN1029"),
           "complex expression reports FDN1029");

    std::string deepBlocks = "fn main() i32 {";
    for (int index = 0; index < 512; ++index) {
        deepBlocks += "if true {";
    }
    deepBlocks += "return 0";
    for (int index = 0; index < 512; ++index) {
        deepBlocks += '}';
    }
    deepBlocks += '}';
    const auto nestedBlocks = check(deepBlocks);
    expect(hasCode(nestedBlocks.diagnostics, "FDN1030"),
           "deep block nesting reports FDN1030");

    const auto mutableElse = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    var value = failure() else error { panic(error) }
    value
}
)");
    expect(hasCode(mutableElse.diagnostics, "FDN1067"),
           "mutable let else binding reports FDN1067");

    const auto trySyntax = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    try failure()
    0
}
)");
    expect(hasCode(trySyntax.diagnostics, "FDN2004"), "try syntax is not accepted");

    const auto throwSyntax = check("fn main() i32 { throw \"failed\" 0 }");
    expect(hasCode(throwSyntax.diagnostics, "FDN2004"), "throw syntax is not accepted");

    const auto failsSyntax = check("fn failure() fails String { return }");
    expect(hasCode(failsSyntax.diagnostics, "FDN1008"), "fails syntax is not accepted");

    const auto propagationSyntax = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 { failure()? 0 }
)");
    expect(hasCode(propagationSyntax.diagnostics, "FDN0001"),
           "implicit propagation syntax is not accepted");
}

void semanticFailuresHaveStableDiagnostics() {
    const auto noMain = check("fn worker() i32 { 0 }");
    expect(hasCode(noMain.diagnostics, "FDN2006"), "missing main reports FDN2006");

    const auto wide = check("fn main() i32 { 2147483648 }");
    expect(hasCode(wide.diagnostics, "FDN2005"), "wide i32 literal reports FDN2005");

    const auto minimum = check("fn main() i32 { -2147483648 }");
    expect(!minimum.diagnostics.hasErrors(), "minimum i32 literal is accepted");

    const auto immutable =
        check("fn main() i32 { let value i32 = 1 value = 2 return value }");
    expect(hasCode(immutable.diagnostics, "FDN2013"),
           "immutable assignment reports FDN2013");

    const auto mismatch = check("fn main() i32 { let value bool = 1 return 0 }");
    expect(hasCode(mismatch.diagnostics, "FDN2011"), "type mismatch reports FDN2011");

    const auto unknown = check("fn main() i32 { missing() return 0 }");
    expect(hasCode(unknown.diagnostics, "FDN2009"), "unknown function reports FDN2009");

    const auto duplicate =
        check("fn main() i32 { let value = 1 let value = 2 return value }");
    expect(hasCode(duplicate.diagnostics, "FDN2003"),
           "duplicate binding reports FDN2003");

    const auto unknownBinding = check("fn main() i32 { return value }");
    expect(hasCode(unknownBinding.diagnostics, "FDN2004"),
           "unknown binding reports FDN2004");

    const auto wrongArity = check(R"(
fn take(value i32) i32 { return value }
fn main() i32 { return take() }
)");
    expect(hasCode(wrongArity.diagnostics, "FDN2010"),
           "wrong function arity reports FDN2010");

    const auto reservedPrint = check(R"(
fn print() void { return }
fn main() i32 { return 0 }
)");
    expect(hasCode(reservedPrint.diagnostics, "FDN2018"),
           "reserved print declaration reports FDN2018");

    const auto reservedPanic = check(R"(
fn panic(message String) void { return }
fn main() i32 { return 0 }
)");
    expect(hasCode(reservedPanic.diagnostics, "FDN2018"),
           "reserved panic declaration reports FDN2018");

    const auto wrongPanicArgument = check("fn main() i32 { panic(1) 0 }");
    expect(hasCode(wrongPanicArgument.diagnostics, "FDN2011"),
           "non-String panic argument reports FDN2011");

    const auto mainCall = check("fn main() i32 { return main() }");
    expect(hasCode(mainCall.diagnostics, "FDN2019"), "calling main reports FDN2019");

    const auto voidParameter = check(R"(
fn consume(value void) void { return }
fn main() i32 { return 0 }
)");
    expect(hasCode(voidParameter.diagnostics, "FDN2016"),
           "void parameter reports FDN2016");

    const auto voidReturnValue = check(R"(
fn consume() void { return print("value") }
fn main() i32 { consume() return 0 }
)");
    expect(hasCode(voidReturnValue.diagnostics, "FDN2015"),
           "void return value reports FDN2015");

    const auto fallthrough = check("fn main() i32 { let value = 1 }");
    expect(hasCode(fallthrough.diagnostics, "FDN2008"),
           "fallthrough in non-void function reports FDN2008");

    const auto duplicateType = check(R"(
struct Item { value i32 }
struct Item { other i32 }
fn main() i32 { return 0 }
)");
    expect(hasCode(duplicateType.diagnostics, "FDN2020"),
           "duplicate struct type reports FDN2020");

    const auto duplicateField = check(R"(
struct Item { value i32 value bool }
fn main() i32 { return 0 }
)");
    expect(hasCode(duplicateField.diagnostics, "FDN2021"),
           "duplicate struct field reports FDN2021");

    const auto recursiveStruct = check(R"(
struct Node { next Node }
fn main() i32 { return 0 }
)");
    expect(hasCode(recursiveStruct.diagnostics, "FDN2023"),
           "recursive value struct reports FDN2023");

    const auto unknownField = check(R"(
struct Item { value i32 }
fn main() i32 { let item = Item { value = 1 } return item.missing }
)");
    expect(hasCode(unknownField.diagnostics, "FDN2025"),
           "unknown struct field reports FDN2025");

    const auto duplicateInitializer = check(R"(
struct Item { value i32 }
fn main() i32 { let item = Item { value = 1 value = 2 } return 0 }
)");
    expect(hasCode(duplicateInitializer.diagnostics, "FDN2026"),
           "duplicate field initializer reports FDN2026");

    const auto nonStructAccess = check("fn main() i32 { let value = 1 return value.field }");
    expect(hasCode(nonStructAccess.diagnostics, "FDN2028"),
           "field access on a primitive reports FDN2028");

    const auto missingPayload = check(R"(
enum Value { Empty Number(i32) }
fn main() i32 { let value = Value.Number return 0 }
)");
    expect(hasCode(missingPayload.diagnostics, "FDN2036"),
           "missing enum payload reports FDN2036");

    const auto duplicatePattern = check(R"(
enum Value { Empty Number(i32) }
fn main() i32 {
    let value = Value.Empty
    return match value {
        Empty: 0
        Empty: 1
        Number(number): number
    }
}
)");
    expect(hasCode(duplicatePattern.diagnostics, "FDN2039"),
           "duplicate match pattern reports FDN2039");

    const auto valueCycle = check(R"(
struct Node { state State }
enum State { End Next(Node) }
fn main() i32 { return 0 }
)");
    expect(hasCode(valueCycle.diagnostics, "FDN2023"),
           "struct and enum value cycle reports FDN2023");

    const auto duplicateTypeParameter = check(R"(
struct Pair<T, T> { value T }
fn main() i32 { return 0 }
)");
    expect(hasCode(duplicateTypeParameter.diagnostics, "FDN2042"),
           "duplicate type parameter reports FDN2042");

    const auto wrongTypeArity = check(R"(
enum Maybe<T> { None Some(T) }
fn read(value Maybe<i32, bool>) i32 { return 0 }
fn main() i32 { return 0 }
)");
    expect(hasCode(wrongTypeArity.diagnostics, "FDN2043"),
           "wrong type argument count reports FDN2043");

    const auto wrongFunctionTypeArity = check(R"(
fn identity<T>(value T) T { return value }
fn main() i32 { return identity<i32, bool>(1) }
)");
    expect(hasCode(wrongFunctionTypeArity.diagnostics, "FDN2043"),
           "wrong function type argument count reports FDN2043");

    const auto unresolvedType = check(R"(
enum Maybe<T> { None Some(T) }
fn main() i32 { let value = Maybe.None return 0 }
)");
    expect(hasCode(unresolvedType.diagnostics, "FDN2045"),
           "unresolved constructor type reports FDN2045");

    const auto conflictingInference = check(R"(
struct Pair<T> { first T second T }
fn main() i32 { let value = Pair { first = 1 second = true } return 0 }
)");
    expect(hasCode(conflictingInference.diagnostics, "FDN2011"),
           "conflicting generic inference reports FDN2011");

    const auto polymorphicRecursion = check(R"(
struct Box<T> { value T }
fn loop<T>(value T) i32 { return loop(Box { value = value }) }
fn main() i32 { return loop(1) }
)");
    expect(hasCode(polymorphicRecursion.diagnostics, "FDN2046"),
           "polymorphic recursion reports FDN2046");

    const auto substitutedCycle = check(R"(
struct Box<T> { value T }
struct Recursive { value Box<Recursive> }
fn main() i32 { return 0 }
)");
    expect(hasCode(substitutedCycle.diagnostics, "FDN2023"),
           "cycle introduced by generic substitution reports FDN2023");

    const auto growingTypeCycle = check(R"(
struct Box<T> { value T }
struct Grow<T> { value Grow<Box<T>> }
fn main() i32 { return 0 }
)");
    expect(hasCode(growingTypeCycle.diagnostics, "FDN2023"),
           "growing generic value cycle reports FDN2023");

    const auto voidApplication = check(R"(
struct Box<T> { value T }
fn consume(value Box<void>) i32 { return 0 }
fn main() i32 { return 0 }
)");
    expect(hasCode(voidApplication.diagnostics, "FDN2047"),
           "void generic field reports FDN2047");

    const auto invalidOwnedApplication = check(R"(
struct Box<T> { value own T }
struct User { id i32 }
fn consume(value Box<own User>) i32 { return 0 }
fn main() i32 { return 0 }
)");
    expect(hasCode(invalidOwnedApplication.diagnostics, "FDN2064"),
           "nested owner introduced by substitution reports FDN2064");

    const auto voidFunctionParameter = check(R"(
fn identity<T>(value T) T { return value }
fn main() i32 { identity(print("bad")) return 0 }
)");
    expect(hasCode(voidFunctionParameter.diagnostics, "FDN2016"),
           "void generic function parameter reports FDN2016");

    const auto primitiveRedeclaration = check(R"(
enum Option<T> { None Some(T) }
fn main() i32 { 0 }
)");
    expect(hasCode(primitiveRedeclaration.diagnostics, "FDN2020"),
           "Option redeclaration reports FDN2020");

    const auto primitiveShadow = check(R"(
fn identity<Result>(value Result) Result { value }
fn main() i32 { 0 }
)");
    expect(hasCode(primitiveShadow.diagnostics, "FDN2042"),
           "Result type parameter shadow reports FDN2042");

    const auto branchDropsResult = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    let result = failure()
    if true {
        discard result
    }
    0
}
)");
    expect(hasCode(branchDropsResult.diagnostics, "FDN2052"),
           "one-branch Result handling reports FDN2052");

    const auto repeatedExit = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    let result = failure()
    if true {
        return 0
    }
    return 0
}
)");
    expect(countCode(repeatedExit.diagnostics, "FDN2052") == 1,
           "one Result binding reports one exit diagnostic");

    const auto allBranchesHandleResult = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    let result = failure()
    if true {
        discard result
    } else {
        discard result
    }
    0
}
)");
    expect(!allBranchesHandleResult.diagnostics.hasErrors(),
           "all-branch Result handling is accepted");

    const auto fallingThroughElse = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    let value = failure() else error {
        print(error)
    }
    value
}
)");
    expect(hasCode(fallingThroughElse.diagnostics, "FDN2054"),
           "falling-through let else reports FDN2054");

    const auto panicElse = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    let value = failure() else error {
        panic(error)
    }
    value
}
)");
    expect(!panicElse.diagnostics.hasErrors(), "panic exits a let else error path");
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

void packageHeadersAndSourceDiagnosticsStayStable() {
    constexpr std::string_view source = R"(
package example.app
import example.math
import example.text as text
fn main() i32 { 0 }
)";
    foundation::Diagnostics diagnostics;
    foundation::Lexer lexer(source, diagnostics, 3);
    foundation::Parser parser(lexer.scan(), diagnostics);
    const auto program = parser.parse();

    expect(!diagnostics.hasErrors(), "package header parses without diagnostics");
    expect(program.hasPackageDeclaration, "package declaration is retained in the AST");
    expect(program.packageName == "example.app", "dotted package name is retained");
    expect(program.imports.size() == 2, "all file imports are retained");
    if (program.imports.size() == 2) {
        expect(program.imports[0].packageName == "example.math" &&
                   program.imports[0].alias.empty(),
               "default import retains its package name");
        expect(program.imports[1].packageName == "example.text" &&
                   program.imports[1].alias == "text",
               "explicit import alias is retained");
    }
    expect(!program.functions.empty() && program.functions.back().span.source == 3,
           "declaration spans retain their source ID");

    foundation::Diagnostics projectDiagnostics;
    projectDiagnostics.error("FDN9997", "second source", {0, 1, 1, 1, 1});
    const std::vector<foundation::DiagnosticSource> sources{
        {"first.fdn", "first\n"},
        {"second.fdn", "second\n"},
    };
    const auto rendered = foundation::renderDiagnostics(sources, projectDiagnostics);
    expect(rendered.find("second.fdn:1:1") != std::string::npos,
           "project diagnostic selects the originating file");
    expect(rendered.find("1 | second") != std::string::npos,
           "project diagnostic selects the originating source text");
}

void cAbiFunctionsLowerToDeterministicBoundaries() {
    constexpr std::string_view source = R"(
extern c fn nativeAdd(left i32, right i32) i32 as foundation_native_add

extern c fn FoundationDouble(value i32) i32 as foundation_double {
    value * 2
}

fn main() i32 {
    nativeAdd(FoundationDouble(20), 2)
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "C ABI program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated C ABI program has no diagnostics");
    expect(first.program.functions.size() == 3, "C ABI declarations remain functions in AST");
    if (first.program.functions.size() == 3) {
        expect(first.program.functions[0].cSymbol == "foundation_native_add" &&
                   !first.program.functions[0].hasBody,
               "bodyless C ABI declaration is an import");
        expect(first.program.functions[1].cSymbol == "foundation_double" &&
                   first.program.functions[1].hasBody,
               "C ABI declaration with a body is an export");
    }
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "C ABI program lowers to FIR");
        return;
    }

    const auto firstC = foundation::emitC(*first.fir, "ffi.fdn");
    const auto secondC = foundation::emitC(*second.fir, "ffi.fdn");
    const auto firstHeader = foundation::emitCHeader(*first.fir);
    const auto secondHeader = foundation::emitCHeader(*second.fir);
    expect(firstC == secondC, "C ABI source emission is deterministic");
    expect(firstHeader == secondHeader, "C ABI header emission is deterministic");
    expect(firstC.find("fdn_frame_enter_native") != std::string::npos,
           "C ABI wrappers add native trace frames");
    expect(firstC.find("foundation_native_add(int32_t, int32_t);") != std::string::npos,
           "C import receives a public C prototype");
    expect(firstHeader.find("foundation_double(int32_t fdn_arg_0);") != std::string::npos,
           "C export appears in the public header");
    expect(firstHeader.find("foundation_native_add") == std::string::npos,
           "C import does not leak into the public header");
}

void closuresLowerToDeterministicFunctionValues() {
    constexpr std::string_view source = R"(
fn double(value i32) i32 { value * 2 }

fn apply<T>(value T, operation view fn(T) T) T {
    operation(value)
}

fn main() i32 {
    let direct fn(i32) i32 = double
    let factor = 2
    let closure = fn(value i32) i32 capture factor {
        value * factor
    }
    apply(21, view direct) + apply(21, view closure) - 84
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "closure program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated closure program has no diagnostics");
    expect(first.program.functions.size() == 4 && first.program.functions[2].closure,
           "anonymous function remains a closure in the AST");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "closure program lowers to FIR");
        return;
    }

    const auto firstC = foundation::emitC(*first.fir, "closures.fdn");
    const auto secondC = foundation::emitC(*second.fir, "closures.fdn");
    expect(firstC == secondC, "closure C emission is deterministic");
    expect(firstC.find("fdn_call") != std::string::npos,
           "function values use a typed invocation pointer");
    expect(firstC.find("_environment_drop") != std::string::npos,
           "captured closure receives deterministic environment cleanup");
    expect(firstC.find("_value_adapter") != std::string::npos,
           "named function value receives an invocation adapter");
}

void projectDiagnosticsRetainTheirSource() {
    foundation::Diagnostics diagnostics;
    const auto project = foundation::loadProject(
        std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) / "tests/projects/multiple-main",
        diagnostics);
    expect(project.has_value(), "multiple-main project is loaded for diagnostics");
    if (!project.has_value()) {
        return;
    }
    for (const auto &diagnostic : diagnostics.all()) {
        if (diagnostic.code != "FDN3010") {
            continue;
        }
        expect(diagnostic.span.source < project->sources.size(),
               "multiple-main diagnostic has a valid source ID");
        if (diagnostic.span.source < project->sources.size()) {
            expect(project->sources[diagnostic.span.source].path == "second/main.fdn",
                   "multiple-main diagnostic points to the second entry point");
        }
        return;
    }
    expect(false, "multiple-main project reports FDN3010");
}

} // namespace

int main() {
    typedProgramLowersToDeterministicC();
    structValuesLowerToDeterministicC();
    deepStructGraphsStayIterative();
    enumMatchesLowerToDeterministicC();
    genericValuesMonomorphizeDeterministically();
    ownershipLowersToDeterministicC();
    sequenceValuesLowerToDeterministicC();
    methodsAndContractsLowerToDeterministicC();
    lightweightSyntaxCarriesVisibilityAndContext();
    panicLowersWithSourceFrames();
    divergingCallsCloseGeneratedControlFlow();
    syntaxFailuresHaveStableDiagnostics();
    semanticFailuresHaveStableDiagnostics();
    diagnosticsBoundLongSourceExcerpts();
    packageHeadersAndSourceDiagnosticsStayStable();
    cAbiFunctionsLowerToDeterministicBoundaries();
    closuresLowerToDeterministicFunctionValues();
    projectDiagnosticsRetainTheirSource();

    if (failures != 0) {
        std::cerr << failures << " test assertions failed\n";
        return 1;
    }
    std::cout << "stage0 compiler tests passed\n";
    return 0;
}
