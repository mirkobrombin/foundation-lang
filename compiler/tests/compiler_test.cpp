#include "foundation/application.hpp"
#include "foundation/backend.hpp"
#include "foundation/codegen.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/documentation.hpp"
#include "foundation/driver.hpp"
#include "foundation/fsm.hpp"
#include "foundation/lexer.hpp"
#include "foundation/lower.hpp"
#include "foundation/metadata.hpp"
#include "foundation/parser.hpp"
#include "foundation/package_interface.hpp"
#include "foundation/project.hpp"
#include "foundation/sema.hpp"
#include "foundation/target.hpp"

#include <algorithm>
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

CheckedProgram check(std::string_view source,
                     foundation::TargetPlatform target = foundation::hostTargetPlatform()) {
    CheckedProgram result;
    foundation::Lexer lexer(source, result.diagnostics);
    foundation::Parser parser(lexer.scan(), result.diagnostics, true, target);
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

bool hasCode(const foundation::Diagnostics &diagnostics, std::string_view code);
void expect(bool condition, std::string_view message);

void llvmIsTheDefaultNativeBackend() {
    expect(foundation::defaultBackendKind() == foundation::BackendKind::Llvm,
           "LLVM is the default native backend");
    expect(foundation::parseBackendKind("c") == foundation::BackendKind::C,
           "the C11 backend remains selectable");
}

void targetAttributesSelectOneDeclaration() {
    constexpr std::string_view source = R"(
attribute Marker(value i32) targets(fn)

@Marker(11)
@target(linux)
fn selected() i32 {
    11
}

@target(macos)
@Marker(22)
fn selected() i32 {
    22
}

@Marker(33)
@target(windows)
fn selected() i32 {
    33
}

fn main() i32 {
    selected()
}
)";
    const auto linux = check(source, foundation::TargetPlatform::Linux);
    const auto macos = check(source, foundation::TargetPlatform::MacOS);
    const auto windows = check(source, foundation::TargetPlatform::Windows);
    expect(!linux.diagnostics.hasErrors(), "linux target declaration is selected");
    expect(!macos.diagnostics.hasErrors(), "macos target declaration is selected");
    expect(!windows.diagnostics.hasErrors(), "windows target declaration is selected");
    expect(linux.program.functions.size() == 2 && macos.program.functions.size() == 2 &&
               windows.program.functions.size() == 2,
           "inactive target declarations and their bodies leave no AST entries");
    expect(linux.program.expressions.size() == macos.program.expressions.size() &&
               macos.program.expressions.size() == windows.program.expressions.size(),
           "inactive target attributes leave no AST expressions");
    if (linux.program.functions.size() == 2 && macos.program.functions.size() == 2 &&
        windows.program.functions.size() == 2) {
        const auto selectedValue = [](const CheckedProgram &program) {
            const auto block = program.program.functions.front().body;
            const auto statement = program.program.blocks[block].statements.front();
            const auto returned = std::get<foundation::ReturnStatement>(
                program.program.statements[statement].value);
            return std::get<foundation::IntegerExpression>(
                       program.program.expressions[*returned.value].value)
                .magnitude;
        };
        expect(selectedValue(linux) == 11, "linux target keeps the linux body");
        expect(selectedValue(macos) == 22, "macos target keeps the macos body");
        expect(selectedValue(windows) == 33, "windows target keeps the windows body");
    }

    constexpr std::string_view unknown = R"(
@target(freebsd)
fn selected() i32 { 1 }
fn main() i32 { 0 }
)";
    expect(hasCode(check(unknown).diagnostics, "FDN1142"),
           "unknown target has a stable diagnostic");

    constexpr std::string_view duplicate = R"(
@target(linux)
@target(macos)
fn selected() i32 { 1 }
fn main() i32 { 0 }
)";
    expect(hasCode(check(duplicate).diagnostics, "FDN1144"),
           "duplicate target attributes have a stable diagnostic");
}

void typedAttributesEmitMetadataWithoutRuntimeCode() {
    constexpr std::string_view annotated = R"(
enum Method {
    GET
}

attribute Route(method Method, path String, weight f64, code i8) targets(fn)

@Route(.GET, "/health", 1.25, 7)
fn main() i32 {
    0
}
)";
    constexpr std::string_view plain = R"(
enum Method {
    GET
}




fn main() i32 {
    0
}
)";
    const auto first = check(annotated);
    const auto second = check(annotated);
    const auto baseline = check(plain);
    expect(!first.diagnostics.hasErrors(), "typed attributes pass semantic analysis");
    expect(first.fir.has_value() && second.fir.has_value() && baseline.fir.has_value(),
           "typed attributes lower to FIR");
    if (!first.fir.has_value() || !second.fir.has_value() || !baseline.fir.has_value()) {
        return;
    }
    const auto metadata = foundation::emitMetadata(*first.fir);
    expect(metadata == foundation::emitMetadata(*second.fir),
           "attribute metadata emission is deterministic");
    expect(metadata.find("foundation.metadata/v1") != std::string::npos &&
               metadata.find("\"name\":\"Route\"") != std::string::npos &&
               metadata.find("\"case\":\"Method.GET\"") != std::string::npos &&
               metadata.find("\"weight\":1.25") != std::string::npos &&
               metadata.find("\"code\":7") != std::string::npos,
           "attribute metadata preserves schema and scalar values");
    expect(foundation::emitC(*first.fir, "<memory>") ==
               foundation::emitC(*baseline.fir, "<memory>"),
           "attributes add no generated C or runtime tables");
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
    const message String = "hello\nworld"
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

void immutableBindingsAndCommentsLexDeterministically() {
    constexpr std::string_view source = R"(
// Public entry point.
// Nested documentation detail.
fn main() i32 {
    // The immutable answer.
    const answer = 21
    // The implementation remains ordinary source.
    /* A nested block /* keeps its contents. */ */
    answer * 2 - 42
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "const and both comment forms are accepted");
    expect(first.fir.has_value() && second.fir.has_value(),
           "const source lowers to FIR repeatedly");
    if (first.fir.has_value() && second.fir.has_value()) {
        expect(foundation::emitC(*first.fir, "comments.fn") ==
                   foundation::emitC(*second.fir, "comments.fn"),
               "comments do not affect deterministic C emission");
    }

    foundation::Diagnostics tokensDiagnostics;
    foundation::Lexer lexer("const value = 1", tokensDiagnostics);
    const auto tokens = lexer.scan();
    expect(!tokensDiagnostics.hasErrors() && !tokens.empty() &&
               tokens.front().kind == foundation::TokenKind::Const,
           "const has a distinct compiler token");

    foundation::Diagnostics commentDiagnostics;
    foundation::Lexer unterminated("/* outer /* nested */", commentDiagnostics);
    static_cast<void>(unterminated.scan());
    expect(hasCode(commentDiagnostics, "FDN0006"),
           "unterminated block comments report FDN0006");
}

void tasksLowerToOwnedRuntimeHandles() {
    constexpr std::string_view source = R"(
task announce($message String) String {
    print(message)
    message
}

fn main() i32 {
    const pending = spawn announce("task")
    const result = $pending.wait()
    print(result)
    0
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "task source is accepted");
    expect(first.fir.has_value() && second.fir.has_value(), "task source lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }
    const auto firstC = foundation::emitC(*first.fir, "tasks.fn");
    const auto secondC = foundation::emitC(*second.fir, "tasks.fn");
    expect(firstC == secondC, "task C emission is deterministic");
    expect(firstC.find("fdn_task_spawn") != std::string::npos,
           "spawn lowers to the runtime executor");
    expect(firstC.find("fdn_task_wait") != std::string::npos,
           "transferred wait lowers to consuming runtime wait");
    expect(firstC.find("fdn_task_drop") != std::string::npos,
           "task locals retain deterministic cleanup");
    expect(firstC.find("fdn_task_cancellation_enter") != std::string::npos &&
               firstC.find("fdn_task_cancellation_leave") != std::string::npos,
           "task polls expose structured cancellation to standard tokens");
}

void taskWaitsLowerToStacklessStates() {
    constexpr std::string_view source = R"(
task child(value i32) i32 {
    value
}

task parent() i32 {
    const pending = spawn child(41)
    const value = $pending.wait()
    value + 1
}

fn main() i32 {
    const pending = spawn parent()
    $pending.wait()
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "nested task wait is accepted");
    expect(first.fir.has_value() && second.fir.has_value(),
           "nested task wait lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }
    const auto firstC = foundation::emitC(*first.fir, "task-suspension.fn");
    const auto secondC = foundation::emitC(*second.fir, "task-suspension.fn");
    expect(firstC == secondC, "stackless task C emission is deterministic");
    expect(firstC.find("fdn_task_poll_wait") != std::string::npos,
           "nested wait uses non-blocking runtime polling");
    expect(firstC.find("fdn_state") != std::string::npos &&
               firstC.find("goto fdn_task_state_1") != std::string::npos,
           "task poll resumes from an explicit state");
    const auto taskWarning = firstC.find("#pragma warning(disable : 4702)");
    const auto taskPoll = firstC.find("_task_poll(void *fdn_raw", taskWarning);
    expect(taskWarning != std::string::npos && taskPoll != std::string::npos &&
               taskWarning < taskPoll,
           "MSVC unreachable suppression wraps the complete suspended task poll");
}

void dynamicSelectTimeoutsLowerToStoredDeadlines() {
    constexpr std::string_view source = R"(
task waitFor(delay u64, $receiver Receiver<void>) bool {
    select {
        receiver.receive(): return true
        timeout timeoutValue(delay): return false
        else error: return false
    }
}

fn timeoutValue(value u64) u64 { value }

fn main() i32 {
    const Channel { sender receiver } = channel<void>(0)
    const pending = spawn waitFor(1, $receiver)
    discard sender
    discard $pending.wait()
    0
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "dynamic select timeout is accepted");
    expect(first.fir.has_value() && second.fir.has_value(),
           "dynamic select timeout lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }
    const auto firstC = foundation::emitC(*first.fir, "dynamic-timeout.fn");
    const auto secondC = foundation::emitC(*second.fir, "dynamic-timeout.fn");
    expect(firstC == secondC, "dynamic timeout C emission is deterministic");
    expect(firstC.find("UINT64_MAX -") != std::string::npos,
           "dynamic timeout addition saturates before suspension");

    constexpr std::string_view invalid = R"(
task waitFor(delay i32, $receiver Receiver<void>) bool {
    select {
        receiver.receive(): return true
        timeout delay: return false
        else error: return false
    }
}

fn main() i32 { 0 }
)";
    const auto rejected = check(invalid);
    expect(hasCode(rejected.diagnostics, "FDN2011"),
           "dynamic select timeout requires u64 nanoseconds");

    constexpr std::string_view unknownUnit = R"(
task waitFor($receiver Receiver<void>) bool {
    select {
        receiver.receive(): return true
        timeout 1.fortnight: return false
        else error: return false
    }
}

fn main() i32 { 0 }
)";
    expect(hasCode(check(unknownUnit).diagnostics, "FDN1144"),
           "literal select timeout rejects unknown units");
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
    const start = Point { x = 1 y = 2 }
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
    const value = Value.Number(3)
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
    expect(firstC.find(".fdn_tag ==") != std::string::npos,
           "match expression emits tagged C branches");
}

void guardedMatchesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
enum Value {
    Empty
    Number(i32)
}

fn read($value Value) i32 {
    match value {
        Number(number) if number > 0: number
        Empty if false: 1
        _: 0
    }
}

fn main() i32 {
    read(Value.Number(3)) - 3
}
)";
    auto first = check(source);
    auto second = check(source);

    expect(!first.diagnostics.hasErrors(), "guarded match has no diagnostics");
    expect(first.fir.has_value(), "guarded match lowers to FIR");
    expect(second.fir.has_value(), "repeated guarded match lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "guarded match C emission is deterministic");
    expect(firstC.find("bool fdn_tmp_") != std::string::npos &&
               firstC.find("if (!fdn_tmp_") != std::string::npos,
           "guarded match preserves source-order fallthrough");
    expect(firstC.find("if (false)") != std::string::npos,
           "unit variants retain their guards");
}

void genericValuesMonomorphizeDeterministically() {
    constexpr std::string_view source = R"(
struct Box<T> { value T }
struct Nested { value Box<Box<i32>> }
enum Choice<T> { None Some(T) }
fn identity<T>($value T) T { return value }
fn main() i32 {
    const nested = Box { value = Box { value = 3 } }
    const number = Choice.Some(identity(3))
    const flag = Choice.Some(identity(true))
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

void copyGenericCallbackOptionsInternOnce() {
    constexpr std::string_view source = R"(
enum Failure<E> {
    Value(E)
}

struct Options<E> {
    RetryIf Option<fn(E) bool> = .None
}

enum Leaf {
    Failed
}

fn main() i32 {
    const options Options<Failure<Leaf>> = Options<Failure<Leaf>> { RetryIf = .None }
    discard options
    0
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(),
           "copy generic callback option has no diagnostics");
    expect(first.fir.has_value() && second.fir.has_value(),
           "copy generic callback option lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*first.fir);
    expect(generated == foundation::emitC(*second.fir),
           "copy generic callback option emits deterministic C");
    const auto payload = generated.find("fdn_function_bool_enum_");
    expect(payload != std::string::npos,
           "copy generic callback option emits a callback payload");
    if (payload == std::string::npos) {
        return;
    }
    const auto payloadEnd = generated.find(' ', payload);
    expect(payloadEnd != std::string::npos,
           "copy generic callback payload has a complete C type");
    if (payloadEnd == std::string::npos) {
        return;
    }
    const auto payloadType = generated.substr(payload, payloadEnd - payload);
    const auto field = payloadType + " fdn_payload_1;";
    const auto firstField = generated.find(field);
    expect(firstField != std::string::npos,
           "copy generic callback option emits its payload field");
    expect(firstField != std::string::npos &&
               generated.find(field, firstField + field.size()) == std::string::npos,
           "copy generic callback option reuses one enum specialization");
}

void genericLookaheadStaysTypeAware() {
    constexpr std::string_view source = R"(
struct Box<T> { value T }
fn identity<T>($value T) T { value }
fn typeMarker<T>() void { return }
fn apply(value i32, operation fn(i32) i32) i32 { operation(value) }
fn main() i32 {
    const x = 1
    const y = 2
    const p = 5
    const c = 4
    const before = x < y
    const after = p > (c)
    const direct fn(i32) i32 = identity<i32>
    const result = apply(42, identity<i32>)
    typeMarker<own Box<i32>>()
    typeMarker<view [i32]>()
    typeMarker<edit [i32]>()
    typeMarker<[2]i32>()
    typeMarker<fn(i32, String) bool>()
    typeMarker<Result<Option<i32>, bool>>()
    if before && after { return direct(result) - 42 } else { return 1 }
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(),
           "generic lookahead does not cross comparison statements");
    expect(result.fir.has_value(),
           "explicit generic function values lower to FIR");
    if (!result.fir.has_value()) {
        return;
    }
    const auto generated = foundation::emitC(*result.fir);
    expect(generated.find("fdn_fn_identity_0_g") != std::string::npos,
           "explicit generic function value selects a specialization");

    constexpr std::string_view malformed = R"(
fn identity<T>($value T) T { value }
fn main() i32 {
    identity<i32,>(1)
}
)";
    auto rejected = check(malformed);
    expect(rejected.diagnostics.hasErrors(),
           "malformed generic type list is rejected by bounded lookahead");

    constexpr std::string_view functionValue = R"(
fn identity<T>($value T) T { value }
fn main() i32 {
    const selected fn(i32) i32 = identity<i32>
    selected(42) - 42
}
)";
    auto selected = check(functionValue);
    expect(!selected.diagnostics.hasErrors() && selected.fir.has_value(),
           "specialized function can be selected as a typed value");
}

void ownershipLowersToDeterministicC() {
    constexpr std::string_view source = R"(
struct User { id i32 }
struct Holder { user own User }

fn read(user User) i32 { user.id }
fn updateUser(&user User, id i32) void { user.id = id }

fn main() i32 {
    var user = own User { id = 3 }
    updateUser(&user, 4)
    const value = read(user)
    const holder = Holder { user = $user }
    const moved = holder
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
    expect(firstC.find("fdn_fn_read_0(fdn_struct_0 ") != std::string::npos,
           "copyable read parameters lower by value");
    expect(firstC.find("FOUNDATION_VERIFY_ALLOCATIONS") != std::string::npos,
           "main can verify that deterministic cleanup reaches zero live allocations");
}

void ownedPlacesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
struct Item {
    value String
}

struct Box {
    item own Item

    fn drop(&self) void {
        const previous = replace self.item with own Item { value = "released" }
        discard previous
    }
}

struct Packet {
    item own Item
}

fn unpack($packet own Packet) String {
    const Packet { item } = packet
    const Item { value } = item
    value
}

fn main() i32 {
    var box = own Box { item = own Item { value = "first" } }
    const previous = replace box.item with own Item { value = "next" }
    discard previous
    discard box
    print(unpack(own Packet { item = own Item { value = "payload" } }))
    0
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "owned place program has no diagnostics");
    expect(first.fir.has_value(), "owned place program lowers to FIR");
    expect(second.fir.has_value(), "repeated owned place program lowers to FIR");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        return;
    }

    auto hasReplace = false;
    auto hasDestructure = false;
    for (const auto &function : first.fir->functions) {
        for (const auto &expression : function.expressions) {
            hasReplace = hasReplace ||
                         std::holds_alternative<foundation::FirReplaceExpression>(
                             expression.value);
        }
        for (const auto &statement : function.statements) {
            hasDestructure = hasDestructure ||
                             std::holds_alternative<
                                 foundation::FirStructDestructureStatement>(statement.value);
        }
    }
    expect(hasReplace, "replace survives in typed FIR");
    expect(hasDestructure, "struct destructuring survives in typed FIR");
    expect(std::any_of(first.fir->structs.begin(), first.fir->structs.end(),
                       [](const foundation::FirStruct &type) {
                           return type.dropFunction.has_value();
                       }),
           "custom deterministic drop is recorded on the FIR struct");

    const auto firstC = foundation::emitC(*first.fir);
    const auto secondC = foundation::emitC(*second.fir);
    expect(firstC == secondC, "owned place C emission is deterministic");
    expect(firstC.find("fdn_dealloc") != std::string::npos,
           "owner destructuring releases the outer allocation");
    expect(firstC.find("fdn_drop_active") != std::string::npos,
           "custom drop values carry a moved-state guard");
}

void sequenceValuesLowerToDeterministicC() {
    constexpr std::string_view source = R"(
struct Batch<T> { values [2]T }

fn first(values [String]) void { print(values[0]) }

fn main() i32 {
    const numbers = Batch { values = [1, 2] }
    discard numbers
    const text = Batch { values = ["left", "right"] }
    discard text
    var values = ["A\0B", "C"]
    first(values)
    values[1] = values[0] + values[1]
    discard values
    const empty [0]i32 = []
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

void mainArgumentsLowerToPortableWrapper() {
    constexpr std::string_view source = R"(
fn main(args [String]) i32 {
    print(args[0])
    0
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "String slice main arguments are accepted");
    expect(result.fir.has_value(), "argument-aware main lowers to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "args.fn");
    expect(generated.find("static int32_t fdn_program_main(fdn_view_slice_string") !=
               std::string::npos,
           "Foundation main remains a typed internal function");
    expect(generated.find("int main(int fdn_argc, char **fdn_argv)") != std::string::npos,
           "generated C exposes the portable argc and argv entry point");
    expect(generated.find("fdn_argv[fdn_index + 1]") != std::string::npos,
           "program name is excluded from Foundation arguments");
    expect(generated.find("fdn_dealloc(fdn_argument_values)") != std::string::npos,
           "the argument adapter is released before process exit");

    const auto invalid = check("fn main(args [i32]) i32 { args[0] }");
    expect(hasCode(invalid.diagnostics, "FDN2007"),
           "non-String main arguments report FDN2007");
}

void sequenceLengthsLowerToU64() {
    constexpr std::string_view source = R"(
fn main(args [String]) i32 {
    const values = [1, 2]
    const label = "ok"
    if len(args) == 0 && len(values) == 2 && len(label) == 2 {
        return 0
    }
    1
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "len accepts slices, arrays, and Strings");
    if (!result.fir.has_value()) {
        expect(false, "len program lowers to FIR");
        return;
    }
    const auto generated = foundation::emitC(*result.fir, "len.fn");
    expect(generated.find(".fdn_length") != std::string::npos,
           "slice length reads the portable slice representation");
    expect(generated.find("UINT64_C(2)") != std::string::npos,
           "array length lowers to a u64 constant");
    expect(generated.find(".length") != std::string::npos,
           "String length reads its byte length");

    const auto invalid = check("fn main() i32 { len(42) 0 }");
    expect(hasCode(invalid.diagnostics, "FDN2011"),
           "len rejects values without a length");
}

void u64ValuesLowerToCheckedC() {
    constexpr std::string_view source = R"(
extern c fn nativeSize(value u64) u64 as foundation_native_size

fn add(left u64, right u64) u64 {
    left + right
}

fn main() i32 {
    const bytes u64 = 18446744073709551615
    if add(nativeSize(bytes), 0) == bytes { return 0 } else { return 1 }
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "u64 values and C ABI signatures are accepted");
    expect(result.fir.has_value(), "u64 values lower to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "u64.fn");
    expect(generated.find("UINT64_C(18446744073709551615)") != std::string::npos,
           "maximum u64 literal is emitted without truncation");
    expect(generated.find("fdn_u64_add") != std::string::npos,
           "u64 addition uses the checked runtime operation");
    expect(generated.find("foundation_native_size(uint64_t);") != std::string::npos,
           "u64 maps to uint64_t at the C ABI boundary");

    const auto negative = check("fn main() i32 { const value u64 = -1 discard value 0 }");
    expect(hasCode(negative.diagnostics, "FDN2005"),
           "negative u64 literal reports FDN2005");
}

void machineScalarsAndNeverLowerToPortableC() {
    constexpr std::string_view source = R"(
extern c fn FoundationScalars(
    signed8 i8,
    signed16 i16,
    signed32 i32,
    signed64 i64,
    signedSize isize,
    unsigned8 u8,
    unsigned16 u16,
    unsigned32 u32,
    unsigned64 u64,
    unsignedSize usize,
    decimal32 f32,
    decimal64 f64,
    flag bool
) f64 as foundation_scalars {
    decimal64
}

fn stop() never {
    panic("stop")
}

fn choose(flag bool) f32 {
    if flag { 1.25 } else { stop() }
}

fn main() i32 {
    const narrowed = i8.From(120) else error {
        discard error
        return 1
    }
    if choose(true) == 1.25 && narrowed == 120 { 0 } else { 1 }
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "machine scalar program has no diagnostics");
    if (!result.fir.has_value()) {
        expect(false, "machine scalar program lowers to FIR");
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "scalars.fn");
    const auto header = foundation::emitCHeader(*result.fir);
    for (const auto type : {"int8_t", "int16_t", "int32_t", "int64_t", "intptr_t",
                            "uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t",
                            "float", "double", "bool"}) {
        expect(header.find(type) != std::string::npos,
               std::string("C scalar header contains ") + type);
    }
    expect(header.find("double foundation_scalars(") != std::string::npos,
           "C scalar export preserves its f64 result");
    expect(generated.find("1.25f") != std::string::npos,
           "contextual f32 literals carry the C float suffix");
    expect(generated.find("INT8_MIN") != std::string::npos &&
               generated.find("INT8_MAX") != std::string::npos,
           "fallible numeric conversion emits checked target bounds");
    expect(generated.find("_Noreturn void fdn_fn_stop_") != std::string::npos,
           "an explicit never function lowers to a C noreturn function");
}

void floatingLiteralsRejectOverflow() {
    const auto f64Overflow = check("fn main() i32 { const value = 1e9999 discard value 0 }");
    expect(hasCode(f64Overflow.diagnostics, "FDN1016"),
           "f64 overflow reports FDN1016");

    const auto f32Overflow =
        check("fn main() i32 { const value f32 = 1e39 discard value 0 }");
    expect(hasCode(f32Overflow.diagnostics, "FDN2005"),
           "f32 overflow reports FDN2005");
}

void methodsAndContractsLowerToDeterministicC() {
    constexpr std::string_view source = R"(
contract Readable {
    fn read(self) i32
}

struct Value implements Readable {
    value i32

    fn read(self) i32 {
        self.value
    }
}

fn readAny(value Readable) i32 {
    value.read()
}

fn main() i32 {
    const value = Value { value = 42 }
    readAny(value) - value.read()
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

void taskContractConversionsReceiveEarlyVtableDeclarations() {
    constexpr std::string_view source = R"(
contract Reader {
    fn take($self) i32
}

struct Value implements Reader {
    stored i32

    fn take($self) i32 {
        self.stored
    }
}

task child() i32 { 1 }

task convert() own Reader {
    const pending = spawn child()
    const result = $pending.wait()
    discard result
    own Value { stored = 42 }
}

fn main() i32 {
    const pending = spawn convert()
    const value = $pending.wait()
    value.take() - 42
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(),
           "contract conversion inside a suspending task is accepted");
    expect(result.fir.has_value(),
           "contract conversion inside a suspending task lowers to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "task-contract.fn");
    const auto declaration = generated.find(
        "static const struct fdn_contract_0_vtable fdn_vtable_c0_s0;");
    const auto poll = generated.find("_task_poll");
    const auto definition = generated.find(
        "static const struct fdn_contract_0_vtable fdn_vtable_c0_s0 = {");
    expect(declaration != std::string::npos && poll != std::string::npos &&
               definition != std::string::npos && declaration < poll && poll < definition,
           "contract vtable is declared before suspending task poll emission");
}

void contractInheritanceFlattensDeterministically() {
    constexpr std::string_view source = R"(
contract Named<T> {
    fn value(self) T
}

contract Tagged<T> extends Named<T> {
    fn tag(self) i32
}

contract Audited<T> extends Named<T> {
    fn audited(self) bool
}

contract Principal<T> extends Tagged<T>, Audited<T> {}

struct Entry implements Principal<i32> {
    stored i32

    fn value(self) i32 { self.stored }
    fn tag(self) i32 { 2 }
    fn audited(self) bool { true }
}

fn readNamed(value Named<i32>) i32 {
    value.value()
}

fn main() i32 {
    const entry = Entry { stored = 40 }
    readNamed(entry) + entry.tag() - 42
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "contract inheritance program has no diagnostics");
    expect(first.semantic.has_value() && first.fir.has_value(),
           "contract inheritance program lowers to FIR");
    expect(second.fir.has_value(), "repeated contract inheritance program lowers to FIR");
    if (!first.semantic.has_value() || !first.fir.has_value() || !second.fir.has_value()) {
        return;
    }
    expect(first.semantic->contracts.size() == 4 &&
               first.semantic->contracts[3].methods.size() == 3,
           "diamond inheritance keeps one copy of an identical method");
    if (first.semantic->contracts.size() == 4 &&
        first.semantic->contracts[3].methods.size() == 3) {
        expect(first.semantic->contracts[3].methods[0].name == "value" &&
                   first.semantic->contracts[3].methods[1].name == "tag" &&
                   first.semantic->contracts[3].methods[2].name == "audited",
               "inherited method order is deterministic");
    }
    expect(foundation::emitC(*first.fir) == foundation::emitC(*second.fir),
           "contract inheritance C emission is deterministic");

    const auto cycle = check(R"cycle(
contract First extends Second {}
contract Second extends First {}
fn main() i32 { 0 }
)cycle");
    expect(hasCode(cycle.diagnostics, "FDN2142"),
           "contract inheritance cycle reports FDN2142");

    const auto conflict = check(R"conflict(
contract Numbered { fn value(self) i32 }
contract Flagged { fn value(self) bool }
contract Invalid extends Numbered, Flagged {}
fn main() i32 { 0 }
)conflict");
    expect(hasCode(conflict.diagnostics, "FDN2143"),
           "conflicting inherited methods report FDN2143");

    const auto ambiguousDefault = check(R"defaults(
contract First { fn value(self) i32 { 1 } }
contract Second { fn value(self) i32 { 2 } }
contract Invalid extends First, Second {}
fn main() i32 { 0 }
)defaults");
    expect(hasCode(ambiguousDefault.diagnostics, "FDN2144"),
           "ambiguous inherited defaults report FDN2144");

    const auto resolvedDefault = check(R"defaults(
contract First { fn value(self) i32 { 1 } }
contract Second { fn value(self) i32 { 2 } }
contract Valid extends First, Second { fn value(self) i32 { 3 } }
struct Number implements Valid {}
fn read(value Valid) i32 { value.value() }
fn main() i32 { const value = Number {} read(value) - 3 }
)defaults");
    expect(!resolvedDefault.diagnostics.hasErrors(),
           "a direct default resolves inherited default ambiguity");

    const auto unknownDelegate = check(R"delegate(
contract Named { fn value(self) i32 }
struct Identity implements Named { fn value(self) i32 { 1 } }
struct Invalid implements Named {
    identity Identity
    delegate missing as Named
}
fn main() i32 { 0 }
)delegate");
    expect(hasCode(unknownDelegate.diagnostics, "FDN2146"),
           "unknown delegation field reports FDN2146");

    const auto invalidDelegate = check(R"delegate(
contract Named { fn value(self) i32 }
struct Identity { value i32 }
struct Invalid implements Named {
    identity Identity
    delegate identity as Named
}
fn main() i32 { 0 }
)delegate");
    expect(hasCode(invalidDelegate.diagnostics, "FDN2147"),
           "non-conforming delegation field reports FDN2147");

    const auto undeclaredDelegation = check(R"delegate(
contract Named { fn value(self) i32 }
struct Identity implements Named { fn value(self) i32 { 1 } }
struct Invalid {
    identity Identity
    delegate identity as Named
}
fn main() i32 { 0 }
)delegate");
    expect(hasCode(undeclaredDelegation.diagnostics, "FDN1171"),
           "delegation requires the contract in the implements list");

    const auto duplicateDelegation = check(R"delegate(
contract Named { fn value(self) i32 }
struct Identity implements Named { fn value(self) i32 { 1 } }
struct Invalid implements Named {
    identity Identity
    delegate identity as Named
    delegate identity as Named
}
fn main() i32 { 0 }
)delegate");
    expect(hasCode(duplicateDelegation.diagnostics, "FDN1172"),
           "a contract accepts one explicit delegate");
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
    const Value = Holder { Value = 0 hidden = false }
    const none Choice<i32> = .None
    const chosen = MakeChoice()
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
    expect(result.program.enums.size() == 5 && result.program.enums[4].variants[0].exported &&
               !result.program.enums[4].variants[2].exported,
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
fn crash($message String) i32 {
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
    const auto generated = foundation::emitC(*result.fir, "panic.fn");
    expect(generated.find("fdn_panic") != std::string::npos,
           "panic lowers to the fatal runtime operation");
    expect(generated.find("fdn_frame_enter") != std::string::npos,
           "generated functions enter Foundation frames");
    expect(generated.find("\"panic.fn\"") != std::string::npos,
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

task halt() i32 {
    const operation fn() i32 = calculate
    operation()
}

fn main() i32 {
    const pending = spawn halt()
    $pending.wait()
}
)";
    auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "diverging call program has no diagnostics");
    expect(result.fir.has_value(), "diverging call program lowers to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "diverging.fn");
    expect(generated.find("_Noreturn void fdn_fn_stop_0") != std::string::npos,
           "direct panic marks its Foundation function as diverging");
    expect(generated.find("_Noreturn void fdn_fn_calculate_1") != std::string::npos,
           "divergence propagates through a user function call");
    expect(generated.find("fdn_i32_add") == std::string::npos,
           "code after a diverging operand is not emitted");
    expect(generated.find("return;\n    fdn_frame_leave") == std::string::npos,
           "explicit return does not emit a second frame epilogue");
    const auto adapter = generated.find("_value_adapter(void *fdn_env) {");
    const auto adapterWarning = generated.rfind("#pragma warning(disable : 4702)", adapter);
    expect(adapter != std::string::npos && adapterWarning != std::string::npos &&
               adapterWarning < adapter,
           "MSVC unreachable suppression wraps the complete diverging function adapter");
}

void exhaustiveMatchExitsCloseGeneratedControlFlow() {
    constexpr std::string_view source = R"(
enum Outcome {
    Ready
    Failed
}

fn requireVoid(value Outcome) void {
    match value {
        Ready: {
            return
        }
        Failed: panic("failed")
    }
}

fn fail(value Outcome) never {
    match value {
        Ready: panic("ready")
        Failed: panic("failed")
    }
}

fn selectValue(value Outcome) i32 {
    match value {
        Ready: 7
        Failed: fail(.Failed)
    }
}

task fill($sender Sender<void>) void {
    discard sender.send()
}

task exercise(value Outcome, $receiver Receiver<void>) i32 {
    requireVoid(.Ready)
    match value {
        Ready: {
            const received = receiver.receive()
            match received {
                Ok: {}
                Err(error): {
                    discard error
                    return 1
                }
            }
        }
        Failed: fail(.Failed)
    }
    0
}

fn main() i32 {
    if selectValue(.Ready) != 7 return 2
    const Channel { sender receiver } = channel<void>(1)
    const filling = spawn fill($sender)
    $filling.wait()
    const pending = spawn exercise(.Ready, $receiver)
    $pending.wait()
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "exhaustive exit matches have no diagnostics");
    expect(result.fir.has_value(), "exhaustive exit matches lower to FIR");
    if (!result.fir.has_value()) {
        return;
    }

    auto storedTaskMatch = false;
    for (const auto &function : result.fir->functions) {
        if (!function.task) {
            continue;
        }
        for (const auto &expression : function.expressions) {
            const auto *match = std::get_if<foundation::FirMatchExpression>(&expression.value);
            if (match != nullptr && match->valueStorage.has_value() &&
                *match->valueStorage < function.locals.size()) {
                storedTaskMatch = true;
            }
        }
    }
    expect(storedTaskMatch, "task matches retain their discriminant in the task frame");

    const auto generated = foundation::emitC(*result.fir, "match-exits.fn");
    expect(generated.find("_Noreturn void fdn_fn_requireVoid_") == std::string::npos,
           "a returning match arm does not make its function diverging");
    expect(generated.find("_Noreturn void fdn_fn_fail_") != std::string::npos,
           "a fully diverging match retains the noreturn contract");
    expect(generated.find("goto fdn_task_state_1") != std::string::npos,
           "code after a returning helper remains in the task state machine");
    expect(generated.find("goto fdn_match_done_") != std::string::npos,
           "a resumed match arm bypasses transient matcher state");
    expect(generated.find(" = {0};\n    bool fdn_tmp_") != std::string::npos,
           "match results have a portable definite initializer");
    expect(generated.find("fdn_panic_cstr(\"unreachable match\")") != std::string::npos,
           "exhaustive exiting matches close their generated C control flow");
}

void syntaxFailuresHaveStableDiagnostics() {
    const auto legacySemicolon = check("fn main() i32 { print(\"bad\"); 0 }");
    expect(hasCode(legacySemicolon.diagnostics, "FDN0001"),
           "legacy semicolon reports FDN0001");

    std::string invalid(100000, '#');
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
           "mutable const else binding reports FDN1067");

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
        check("fn main() i32 { const value i32 = 1 value = 2 return value }");
    expect(hasCode(immutable.diagnostics, "FDN2013"),
           "immutable assignment reports FDN2013");

    const auto mismatch = check("fn main() i32 { const value bool = 1 return 0 }");
    expect(hasCode(mismatch.diagnostics, "FDN2011"), "type mismatch reports FDN2011");

    const auto unknown = check("fn main() i32 { missing() return 0 }");
    expect(hasCode(unknown.diagnostics, "FDN2009"), "unknown function reports FDN2009");

    const auto duplicate =
        check("fn main() i32 { const value = 1 const value = 2 return value }");
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

    const auto fallthrough = check("fn main() i32 { const value = 1 }");
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

    const auto finiteRepeatedGeneric = check(R"(
struct Box<T> { value T }
struct Holder<T> { value Option<Box<Option<T>>> }
fn main() i32 { return 0 }
)");
    expect(!finiteRepeatedGeneric.diagnostics.hasErrors(),
           "finite repeated generic declarations are accepted");

    const auto unknownField = check(R"(
struct Item { value i32 }
fn main() i32 { const item = Item { value = 1 } return item.missing }
)");
    expect(hasCode(unknownField.diagnostics, "FDN2025"),
           "unknown struct field reports FDN2025");

    const auto duplicateInitializer = check(R"(
struct Item { value i32 }
fn main() i32 { const item = Item { value = 1 value = 2 } return 0 }
)");
    expect(hasCode(duplicateInitializer.diagnostics, "FDN2026"),
           "duplicate field initializer reports FDN2026");

    const auto nonStructAccess = check("fn main() i32 { const value = 1 return value.field }");
    expect(hasCode(nonStructAccess.diagnostics, "FDN2028"),
           "field access on a primitive reports FDN2028");

    const auto missingPayload = check(R"(
enum Value { Empty Number(i32) }
fn main() i32 { const value = Value.Number return 0 }
)");
    expect(hasCode(missingPayload.diagnostics, "FDN2036"),
           "missing enum payload reports FDN2036");

    const auto duplicatePattern = check(R"(
enum Value { Empty Number(i32) }
fn main() i32 {
    const value = Value.Empty
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
fn identity<T>($value T) T { return value }
fn main() i32 { return identity<i32, bool>(1) }
)");
    expect(hasCode(wrongFunctionTypeArity.diagnostics, "FDN2043"),
           "wrong function type argument count reports FDN2043");

    const auto unresolvedType = check(R"(
enum Maybe<T> { None Some(T) }
fn main() i32 { const value = Maybe.None return 0 }
)");
    expect(hasCode(unresolvedType.diagnostics, "FDN2045"),
           "unresolved constructor type reports FDN2045");

    const auto conflictingInference = check(R"(
struct Pair<T> { first T second T }
fn main() i32 { const value = Pair { first = 1 second = true } return 0 }
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
fn identity<T>($value T) T { return value }
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
    const result = failure()
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
    const result = failure()
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
    const result = failure()
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
    const value = failure() else error {
        print(error)
    }
    value
}
)");
    expect(hasCode(fallingThroughElse.diagnostics, "FDN2054"),
           "falling-through const else reports FDN2054");

    const auto omittedErrorBinding = check(R"(
fn value() Result<i32, String> { .Err("ignored") }
fn effect() Result<void, String> { .Err("ignored") }
fn main() i32 {
    const number = value() else {
        return 0
    }
    effect() else {
        return number
    }
    number
}
)");
    expect(!omittedErrorBinding.diagnostics.hasErrors(),
           "Result else accepts an omitted error binding");

    const auto absentErrorBinding = check(R"(
fn value() Result<i32, String> { .Err("ignored") }
fn main() i32 {
    const number = value() else {
        discard error
        return 0
    }
    number
}
)");
    expect(hasCode(absentErrorBinding.diagnostics, "FDN2004"),
           "Result else without a binding exposes no implicit error name");

    const auto panicElse = check(R"(
fn failure() Result<i32, String> { .Err("failed") }
fn main() i32 {
    const value = failure() else error {
        panic(error)
    }
    value
}
)");
    expect(!panicElse.diagnostics.hasErrors(), "panic exits a const else error path");
}

void diagnosticsBoundLongSourceExcerpts() {
    std::string source(4096, 'x');
    foundation::Diagnostics diagnostics;
    diagnostics.error("FDN9999", "bounded diagnostic", {4000, 4096, 1, 4001});

    const auto rendered = foundation::renderDiagnostics("long.fn", source, diagnostics);
    expect(rendered.size() < 512, "long diagnostic source excerpts are bounded");
    expect(rendered.find("...") != std::string::npos,
           "bounded diagnostic source excerpts mark omitted text");
    expect(rendered.find('^') != std::string::npos,
           "bounded diagnostic source excerpts retain the marker");

    foundation::Diagnostics newlineDiagnostics;
    newlineDiagnostics.error("FDN9998", "newline diagnostic", {5, 1, 1, 6});
    const auto newline = foundation::renderDiagnostics("newline.fn", "value\nnext",
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
        {"first.fn", "first\n"},
        {"second.fn", "second\n"},
    };
    const auto rendered = foundation::renderDiagnostics(sources, projectDiagnostics);
    expect(rendered.find("second.fn:1:1") != std::string::npos,
           "project diagnostic selects the originating file");
    expect(rendered.find("1 | second") != std::string::npos,
           "project diagnostic selects the originating source text");
}

void cAbiFunctionsLowerToDeterministicBoundaries() {
    constexpr std::string_view source = R"(
extern c fn nativeAdd(left i32, right i32) i32 as foundation_native_add
extern c fn nativeText() String as foundation_native_text
extern c fn nativeEdit(&value String) i32 as foundation_native_edit

extern c fn FoundationDouble(value i32) i32 as foundation_double {
    value * 2
}

extern c fn foundationIdentity(value i32) i32 as foundation_private_identity {
    value
}

fn main() i32 {
    var text = nativeText()
    discard nativeEdit(&text)
    discard text
    nativeAdd(FoundationDouble(20), 2)
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "C ABI program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated C ABI program has no diagnostics");
    expect(first.program.functions.size() == 6, "C ABI declarations remain functions in AST");
    if (first.program.functions.size() == 6) {
        expect(first.program.functions[0].cSymbol == "foundation_native_add" &&
                   !first.program.functions[0].hasBody,
               "bodyless C ABI declaration is an import");
        expect(first.program.functions[3].cSymbol == "foundation_double" &&
                   first.program.functions[3].hasBody,
               "C ABI declaration with a body is an export");
    }
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "C ABI program lowers to FIR");
        return;
    }

    const auto firstC = foundation::emitC(*first.fir, "ffi.fn");
    const auto secondC = foundation::emitC(*second.fir, "ffi.fn");
    const auto firstHeader = foundation::emitCHeader(*first.fir);
    const auto secondHeader = foundation::emitCHeader(*second.fir);
    expect(firstC == secondC, "C ABI source emission is deterministic");
    expect(firstHeader == secondHeader, "C ABI header emission is deterministic");
    expect(firstC.find("fdn_frame_enter_native") != std::string::npos,
           "C ABI wrappers add native trace frames");
    expect(firstC.find("foundation_native_add(int32_t, int32_t);") != std::string::npos,
           "C import receives a public C prototype");
    expect(firstC.find("fdn_string foundation_native_text(void);") != std::string::npos,
           "C import can transfer an owned String result");
    expect(firstC.find("foundation_native_edit(fdn_string *);") != std::string::npos,
           "C import can mutate a String through an exclusive borrow");
    expect(firstHeader.find("foundation_double(int32_t fdn_arg_0);") != std::string::npos,
           "C export appears in the public header");
    expect(firstC.find("foundation_private_identity(") != std::string::npos,
           "private C entry remains available to bundled native code");
    expect(firstHeader.find("foundation_private_identity") == std::string::npos,
           "private C entry does not leak into the public header");
    expect(firstHeader.find("foundation_native_add") == std::string::npos,
           "C import does not leak into the public header");
}

void cFunctionPointersRejectManagedCallablesAndOpaqueRecords() {
    const auto named = check(R"(
fn increment(value i32) i32 { value + 1 }
fn main() i32 {
    const callback extern c fn(i32) i32 = increment
    callback(1)
}
)");
    expect(hasCode(named.diagnostics, "FDN2215"),
           "managed named functions cannot become C function pointers");

    const auto anonymous = check(R"(
fn main() i32 {
    const callback extern c fn(i32) i32 = fn(value i32) i32 { value + 1 }
    callback(1)
}
)");
    expect(hasCode(anonymous.diagnostics, "FDN2215"),
           "anonymous functions cannot become C function pointers");

    const auto privateRecord = check(R"(
struct privatePoint {
    X i32
}

extern c fn nativeRead(point *const privatePoint) i32 as sample_native_read

fn main() i32 { 0 }
)");
    expect(hasCode(privateRecord.diagnostics, "FDN2214"),
           "raw C pointers reject records without an exported layout");

    const auto rawCall = check(R"(
fn invoke(
    callback extern c fn(*const i32) i32,
    value *const i32
) i32 {
    callback(value)
}

fn main() i32 { 0 }
)");
    expect(hasCode(rawCall.diagnostics, "FDN2213"),
           "C function pointer calls keep raw pointer use inside unsafe blocks");
}

void packageInterfacesUseReachableMonomorphizedCBoundaries() {
    constexpr std::string_view source = R"(
extern c fn nativeLabel(label String) bool as sample_native_label
extern c fn nativeEdit(&label String) i32 as sample_native_edit
extern c fn nativeUnused(value i32) i32 as sample_native_unused

extern c fn FoundationLabel(label String) String as sample_label {
    if nativeLabel(label) return "yes"
    "no"
}

extern c fn FoundationEdit(&label String) i32 as sample_edit {
    nativeEdit(&label)
}

fn main() i32 { 0 }
)";
    auto checked = check(source);
    expect(!checked.diagnostics.hasErrors() && checked.fir.has_value(),
           "package interface fixture lowers to FIR");
    if (!checked.fir.has_value())
        return;
    for (auto &function : checked.fir->functions) {
        function.packageName = "sample.native";
        function.sourcePath = "src/interface.fn";
    }
    foundation::PackageManifest manifest;
    manifest.name = "sample.native";
    manifest.version = *foundation::parsePackageVersion("1.0.0");
    manifest.sdk = *foundation::parsePackageRequirement("^0.1.0");
    manifest.source = "src";
    manifest.nativeLibrary = true;
    manifest.nativeName = "sample_native";
    foundation::PackageLock lock;
    lock.rootName = manifest.name;
    lock.rootVersion = manifest.version;
    lock.target = foundation::TargetPlatform::Linux;

    foundation::Diagnostics diagnostics;
    const auto packageInterface = foundation::buildPackageInterface(
        *checked.fir, manifest, lock, diagnostics);
    expect(packageInterface.has_value() && !diagnostics.hasErrors(),
           "valid C ABI v1 boundaries build a package interface");
    if (!packageInterface.has_value())
        return;
    expect(packageInterface->exports.size() == 2 && packageInterface->imports.size() == 2,
           "PII includes root exports and only their reachable C imports");
    const auto imported = [&](std::string_view symbol) {
        return std::find_if(packageInterface->imports.begin(), packageInterface->imports.end(),
                            [&](const auto &function) { return function.cSymbol == symbol; });
    };
    const auto exported = [&](std::string_view symbol) {
        return std::find_if(packageInterface->exports.begin(), packageInterface->exports.end(),
                            [&](const auto &function) { return function.cSymbol == symbol; });
    };
    const auto labelImport = imported("sample_native_label");
    const auto editImport = imported("sample_native_edit");
    const auto labelExport = exported("sample_label");
    const auto editExport = exported("sample_edit");
    expect(labelImport != packageInterface->imports.end() &&
               labelImport->parameters.front().ownership ==
                   foundation::PiiOwnership::Borrowed &&
               labelExport != packageInterface->exports.end() &&
               labelExport->parameters.front().ownership ==
                   foundation::PiiOwnership::Borrowed,
           "plain String C parameters are borrowed on imports and exports");
    expect(editImport != packageInterface->imports.end() &&
               editImport->parameters.front().ownership ==
                   foundation::PiiOwnership::ExclusiveBorrow &&
               editExport != packageInterface->exports.end() &&
               editExport->parameters.front().ownership ==
                   foundation::PiiOwnership::ExclusiveBorrow,
           "edited String C parameters are exclusive borrows on imports and exports");
    expect(labelExport != packageInterface->exports.end() &&
               labelExport->resultOwnership ==
                   foundation::PiiOwnership::CallerOwnedResult &&
               labelExport->source->path == "src/interface.fn",
           "String results transfer to the caller and source paths stay package-relative");
    expect(foundation::renderPackageInterfaceJson(*packageInterface)
               .find("sample_native_unused") ==
               std::string::npos,
           "unused root C imports are intentionally omitted from PII link dependencies");

    auto invalid = *checked.fir;
    for (auto &function : invalid.functions) {
        if (function.cSymbol == "sample_label") {
            function.returnType = foundation::Type{
                foundation::TypeKind::Function, 0,
                {foundation::i32Type, foundation::i32Type}};
        }
    }
    foundation::Diagnostics invalidDiagnostics;
    expect(!foundation::buildPackageInterface(invalid, manifest, lock,
                                               invalidDiagnostics)
                .has_value() &&
               hasCode(invalidDiagnostics, "FDN2120"),
           "PII emission enforces C ABI v1 on every emitted result");

    auto invalidParameter = *checked.fir;
    for (auto &function : invalidParameter.functions) {
        if (function.cSymbol == "sample_native_label") {
            const auto parameter = function.parameters.front();
            function.locals[parameter].type = foundation::Type{
                foundation::TypeKind::Function, 0,
                {foundation::i32Type, foundation::i32Type}};
        }
    }
    foundation::Diagnostics parameterDiagnostics;
    expect(!foundation::buildPackageInterface(invalidParameter, manifest, lock,
                                               parameterDiagnostics)
                .has_value() &&
               hasCode(parameterDiagnostics, "FDN2120"),
           "PII emission enforces C ABI v1 on every emitted parameter");

    auto absoluteSource = *checked.fir;
    for (auto &function : absoluteSource.functions)
        function.sourcePath = "/tmp/interface.fn";
    foundation::Diagnostics sourceDiagnostics;
    expect(!foundation::buildPackageInterface(absoluteSource, manifest, lock,
                                               sourceDiagnostics)
                .has_value() &&
               hasCode(sourceDiagnostics, "FDN2121"),
           "PII rejects absolute source paths before canonical hashing");

    auto escapingSource = *checked.fir;
    for (auto &function : escapingSource.functions)
        function.sourcePath = "../interface.fn";
    foundation::Diagnostics escapingDiagnostics;
    expect(!foundation::buildPackageInterface(escapingSource, manifest, lock,
                                               escapingDiagnostics)
                .has_value() &&
               hasCode(escapingDiagnostics, "FDN2121"),
           "PII rejects source path traversal before canonical hashing");

    auto callback = *checked.fir;
    for (auto &function : callback.functions) {
        if (function.cSymbol == "sample_native_label") {
            function.callback = true;
            function.returnType = foundation::i32Type;
            function.callbackCancelSymbol = "sample_native_cancel";
        }
    }
    foundation::Diagnostics callbackDiagnostics;
    const auto callbackInterface = foundation::buildPackageInterface(
        callback, manifest, lock, callbackDiagnostics);
    const auto callbackImport =
        callbackInterface.has_value()
            ? std::find_if(callbackInterface->imports.begin(), callbackInterface->imports.end(),
                           [](const auto &function) {
                               return function.cSymbol == "sample_native_label";
                           })
            : std::vector<foundation::PiiFunction>::const_iterator{};
    expect(callbackInterface.has_value() && !callbackDiagnostics.hasErrors() &&
               callbackImport != callbackInterface->imports.end() &&
               callbackImport->callback.has_value() &&
               callbackImport->result.kind == foundation::PiiTypeKind::Void &&
               callbackImport->errors == foundation::PiiErrorConvention::Infallible &&
               callbackImport->callback->errors ==
                   foundation::PiiErrorConvention::ForeignStatus &&
               callbackImport->callback->protocol ==
                   foundation::PiiCallbackProtocol::FoundationReactorV1 &&
               callbackImport->callback->lifetime == foundation::PiiCallbackLifetime::Once &&
               callbackImport->callback->contextHandle == "foundation.reactor.operation" &&
               callbackImport->callback->cancelSymbol == "sample_native_cancel" &&
               callbackImport->callback->parameters.size() == 1 &&
               callbackImport->callback->parameters.front().type.kind ==
                   foundation::PiiTypeKind::I32,
           "PII records the complete Foundation reactor callback protocol");

    for (auto &function : callback.functions) {
        if (function.cSymbol == "sample_native_label")
            function.callbackCancelSymbol.reset();
    }
    foundation::Diagnostics callbackWithoutCancelDiagnostics;
    const auto callbackWithoutCancel = foundation::buildPackageInterface(
        callback, manifest, lock, callbackWithoutCancelDiagnostics);
    const auto callbackWithoutCancelImport =
        callbackWithoutCancel.has_value()
            ? std::find_if(callbackWithoutCancel->imports.begin(),
                           callbackWithoutCancel->imports.end(), [](const auto &function) {
                               return function.cSymbol == "sample_native_label";
                           })
            : std::vector<foundation::PiiFunction>::const_iterator{};
    expect(callbackWithoutCancel.has_value() &&
               !callbackWithoutCancelDiagnostics.hasErrors() &&
               callbackWithoutCancelImport != callbackWithoutCancel->imports.end() &&
               callbackWithoutCancelImport->callback.has_value() &&
               !callbackWithoutCancelImport->callback->cancelSymbol.has_value(),
           "PII preserves a reactor callback without a cancel symbol");
}

void rawPointersLowerToExplicitCBoundaries() {
    constexpr std::string_view source = R"(
extern c fn nativeValues() *i32 as foundation_raw_values

extern c fn FoundationRaw(value *const *i32) *const *i32 as foundation_raw_export {
    value
}

fn main() i32 {
    // SAFETY: the fixture contract provides two live, aligned i32 values.
    unsafe {
        const values = nativeValues()
        *values = 41
        const next = values + 1
        discard *next
        const empty = null<*void>()
        if isNull(empty) return 0
    }
    1
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "raw pointer program has no diagnostics");
    if (!result.fir.has_value()) {
        expect(false, "raw pointer program lowers to FIR");
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "raw.fn");
    const auto header = foundation::emitCHeader(*result.fir);
    expect(generated.find("foundation_raw_values(void);") != std::string::npos,
           "raw pointer import receives a C prototype");
    expect(generated.find(" = NULL;") != std::string::npos,
           "typed raw null construction lowers to C NULL");
    expect(header.find("int32_t * const * foundation_raw_export(") != std::string::npos &&
               header.find("int32_t * const * fdn_arg_0") != std::string::npos,
           "nested raw const qualification survives public header emission");
}

void nativeSystemsPrimitivesLowerToBothBoundaries() {
    constexpr std::string_view source = R"(
struct NativePair {
    Left u32
    Right u32
}

extern c fn nativeFill(&pair NativePair) void as foundation_native_fill
extern c fn nativeRead(pair NativePair) u32 as foundation_native_read_pair
extern c fn nativeSymbol() *void as foundation_native_symbol
extern c fn nativeCheckCString(value *const u8) bool as foundation_native_check_cstring

fn main() i32 {
    const flags u32 = (5 | 2) ^ 1
    if (flags & 2) != 2 return 1
    const one u8 = 1
    const inverted u8 = 254
    if ~one != inverted return 2
    var pair = NativePair { Left = 1 Right = 2 }
    nativeFill(&pair)
    if nativeRead(pair) != 42 return 3
    if sizeOf<NativePair>() != 8 return 4
    // SAFETY: the fixture symbol names a matching C callback and the literal has static storage.
    unsafe {
        const callback = pointerCast<extern c fn(i32) i32>(nativeSymbol())
        if isNull(callback) return 5
        if callback(41) != 42 return 6
        if !nativeCheckCString(cString("foundation")) return 7
    }
    0
}
)";
    const auto result = check(source);
    expect(!result.diagnostics.hasErrors(), "native systems primitives have no diagnostics");
    if (!result.fir.has_value()) {
        expect(false, "native systems primitives lower to FIR");
        return;
    }

    const auto generated = foundation::emitC(*result.fir, "native-systems.fn");
    expect(generated.find("foundation_native_fill(fdn_struct_0 *") != std::string::npos &&
               generated.find("foundation_native_read_pair(const fdn_struct_0 *") !=
                   std::string::npos,
           "borrowed C-layout structs lower to typed pointers");
    expect(generated.find("sizeof(fdn_struct_0)") != std::string::npos,
           "sizeOf lowers to the target C type");
    expect(generated.find("_Static_assert(sizeof(") != std::string::npos &&
               generated.find("memcpy(&") != std::string::npos,
           "function pointer casts preserve the platform representation");
    expect(generated.find("(const uint8_t *)\"foundation\"") != std::string::npos,
           "C string literals lower to static null-terminated storage");

    const auto unsafeCast = check(R"(
extern c fn nativeSymbol() *void as foundation_native_symbol
fn main() i32 {
    discard pointerCast<*i32>(nativeSymbol())
    0
}
)");
    expect(hasCode(unsafeCast.diagnostics, "FDN2213"),
           "pointerCast requires an unsafe block");

    const auto nonLiteral = check(R"(
fn main() i32 {
    const value = "foundation"
    // SAFETY: this fixture checks literal enforcement only.
    unsafe { discard cString(value) }
    0
}
)");
    expect(hasCode(nonLiteral.diagnostics, "FDN2214"),
           "cString rejects non-literal storage");
}

void blockingImportsLowerToTaskSuspension() {
    constexpr std::string_view source = R"(
@blocking
extern c fn nativeRead(value i32) String as foundation_native_read

task read(value i32) String {
    const result = nativeRead(value)
    result
}

fn main() i32 {
    const pending = spawn read(42)
    discard $pending.wait()
    0
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "blocking C import has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated blocking C import has no diagnostics");
    expect(!first.program.functions.empty() && first.program.functions.front().blocking,
           "blocking C import is retained in the AST");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "blocking C import lowers to FIR");
        return;
    }

    bool foundBlockingCall{};
    for (const auto &function : first.fir->functions) {
        for (const auto &expression : function.expressions) {
            foundBlockingCall =
                foundBlockingCall ||
                std::holds_alternative<foundation::FirBlockingCallExpression>(expression.value);
        }
    }
    expect(foundBlockingCall, "blocking C call remains an explicit FIR suspension point");

    const auto firstC = foundation::emitC(*first.fir, "blocking.fn");
    const auto secondC = foundation::emitC(*second.fir, "blocking.fn");
    expect(firstC == secondC, "blocking C emission is deterministic");
    expect(firstC.find("fdn_blocking_poll") != std::string::npos &&
               firstC.find("fdn_blocking_job_") != std::string::npos &&
               firstC.find("_blocking_") != std::string::npos,
           "blocking C call emits a job slot, worker callback, and task poll");
}

void callbackImportsLowerToReactorSuspension() {
    constexpr std::string_view source = R"(
@callback(cancel = foundation_native_cancel)
extern c fn nativeRead(value u64, &result i32) i32 as foundation_native_read

task read(value u64) i32 {
    var result = 0
    const status = nativeRead(value, &result)
    status + result
}

fn main() i32 {
    const pending = spawn read(21)
    $pending.wait()
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "callback C import has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated callback C import has no diagnostics");
    expect(!first.program.functions.empty() && first.program.functions.front().callback,
           "callback C import is retained in the AST");
    expect(first.semantic.has_value() &&
               first.semantic->functions.front().callbackCancelSymbol ==
                   "foundation_native_cancel",
           "callback cancellation symbol is retained by semantic analysis");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "callback C import lowers to FIR");
        return;
    }

    bool foundCallbackCall{};
    for (const auto &function : first.fir->functions) {
        for (const auto &expression : function.expressions) {
            foundCallbackCall =
                foundCallbackCall ||
                std::holds_alternative<foundation::FirCallbackCallExpression>(expression.value);
        }
    }
    expect(foundCallbackCall, "callback C call remains an explicit FIR suspension point");

    const auto firstC = foundation::emitC(*first.fir, "callback.fn");
    const auto secondC = foundation::emitC(*second.fir, "callback.fn");
    expect(firstC == secondC, "callback C emission is deterministic");
    expect(firstC.find(
               "void foundation_native_read(uint64_t, int32_t *, fdn_reactor_operation *);") !=
               std::string::npos,
           "callback start symbol receives ABI-safe arguments and an operation token");
    expect(firstC.find("void foundation_native_cancel(fdn_reactor_operation *);") !=
               std::string::npos,
           "callback cancellation symbol receives the active operation token");
    expect(firstC.find("fdn_reactor_poll") != std::string::npos &&
               firstC.find("_callback_start_") != std::string::npos &&
               firstC.find("_callback_cancel_") != std::string::npos &&
               firstC.find("fdn_callback_operation_") != std::string::npos,
           "callback C call emits an operation slot, native adapters, and task poll");
    expect(firstC.find("int32_t foundation_native_read(") == std::string::npos,
           "callback start symbol is not emitted as a synchronous import");
}

void closuresLowerToDeterministicFunctionValues() {
    constexpr std::string_view source = R"(
fn double(value i32) i32 { value * 2 }

fn apply<T>($value T, operation fn(T) T) T {
    operation(value)
}

fn main() i32 {
    const portable transferable fn(i32) i32 = double
    const direct fn(i32) i32 = portable
    const factor = 2
    const closure = fn(value i32) i32 capture(factor) {
        value * factor
    }
    apply(21, direct) + apply(21, closure) - 84
}
)";
    auto first = check(source);
    auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "closure program has no diagnostics");
    expect(!second.diagnostics.hasErrors(), "repeated closure program has no diagnostics");
    expect(first.program.functions.size() == 4 && first.program.functions[2].closure,
           "anonymous function remains a closure in the AST");
    if (!first.semantic.has_value()) {
        expect(false, "closure program retains semantic function qualifiers");
        return;
    }
    const auto &main = first.semantic->functions[first.semantic->main];
    const auto portable = std::find_if(main.locals.begin(), main.locals.end(), [](const auto &local) {
        return local.name == "portable";
    });
    const auto direct = std::find_if(main.locals.begin(), main.locals.end(), [](const auto &local) {
        return local.name == "direct";
    });
    expect(portable != main.locals.end() && foundation::isTransferableFunction(portable->type),
           "transferable function guarantee remains in the semantic type");
    expect(direct != main.locals.end() && direct->type.kind == foundation::TypeKind::Function &&
               !foundation::isTransferableFunction(direct->type),
           "transferable function values can weaken to ordinary function types");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "closure program lowers to FIR");
        return;
    }
    const auto &firMain = first.fir->functions[first.fir->main];
    const auto firPortable =
        std::find_if(firMain.locals.begin(), firMain.locals.end(), [](const auto &local) {
            return local.name == "portable";
        });
    const auto firDirect =
        std::find_if(firMain.locals.begin(), firMain.locals.end(), [](const auto &local) {
            return local.name == "direct";
        });
    expect(firPortable != firMain.locals.end() &&
               foundation::isTransferableFunction(firPortable->type),
           "transferable function guarantee reaches FIR validation");
    expect(firDirect != firMain.locals.end() &&
               !foundation::isTransferableFunction(firDirect->type),
           "function guarantee weakening reaches FIR validation");

    const auto firstC = foundation::emitC(*first.fir, "closures.fn");
    const auto secondC = foundation::emitC(*second.fir, "closures.fn");
    expect(firstC == secondC, "closure C emission is deterministic");
    expect(firstC.find("fdn_call") != std::string::npos,
           "function values use a typed invocation pointer");
    expect(firstC.find("_environment_drop") != std::string::npos,
           "captured closure receives deterministic environment cleanup");
    expect(firstC.find("_value_adapter") != std::string::npos,
           "named function value receives an invocation adapter");
}

void taskFunctionValuesReceiveEarlyAdapterPrototypes() {
    constexpr std::string_view source = R"(
task idle() void {}

fn make() Task<void> {
    spawn idle()
}

task invoke($factory transferable fn() Task<void>) void {
    const pending = factory()
    $pending.wait()
}

fn main() i32 {
    const factory transferable fn() Task<void> = make
    const pending = spawn invoke($factory)
    $pending.wait()
    0
}
)";
    const auto checked = check(source);
    expect(!checked.diagnostics.hasErrors(),
           "task function value adapter fixture has no diagnostics");
    if (!checked.fir.has_value()) {
        expect(false, "task function value adapter fixture lowers to FIR");
        return;
    }
    const auto generated = foundation::emitC(*checked.fir, "task-function-value.fn");
    const auto reference = generated.find(".fdn_call = &");
    const auto prototype = generated.find("_value_adapter(void *fdn_env);");
    expect(reference != std::string::npos && prototype != std::string::npos &&
               prototype < reference,
           "task poll sees a function value adapter prototype before first use");
}

void servicesAndActionsLowerToStaticApplicationMetadata() {
    constexpr std::string_view source = R"(
attribute Managed(value bool) targets(service)
attribute Handler(name String) targets(action)
attribute Factory() targets(ctor)

@Managed(true)
service CounterService {
    value i32

    @Factory()
    ctor New(initial i32) {
        CounterService { value = initial }
    }

    ctor Zero() {
        CounterService { value = 0 }
    }

    @Handler("counter.add")
    action Add(&self, amount i32) i32 {
        self.value = self.value + amount
        self.value
    }
}

fn main() i32 {
    var counter = CounterService.New(40)
    counter.Add(2) - 42
}
)";
    const auto first = check(source);
    const auto second = check(source);
    expect(!first.diagnostics.hasErrors(), "service and action program has no diagnostics");
    expect(!second.diagnostics.hasErrors(),
           "repeated service and action program has no diagnostics");
    expect(!first.program.structs.empty() &&
               first.program.structs.front().kind == foundation::StructKind::Service,
           "service identity remains explicit in the AST");
    const auto action = std::find_if(first.program.functions.begin(),
                                     first.program.functions.end(),
                                     [](const auto &function) { return function.action; });
    expect(action != first.program.functions.end() && action->receiver.has_value(),
           "action remains a receiver method in the AST");
    const auto constructor =
        std::find_if(first.program.functions.begin(), first.program.functions.end(),
                     [](const auto &function) { return function.constructor; });
    expect(constructor != first.program.functions.end() && !constructor->receiver.has_value(),
           "constructor remains an associated declaration in the AST");
    if (!first.fir.has_value() || !second.fir.has_value()) {
        expect(false, "service and action program lowers to FIR");
        return;
    }
    expect(!first.fir->structs.empty() && first.fir->structs.front().service,
           "service identity survives into FIR");
    expect(std::any_of(first.fir->functions.begin(), first.fir->functions.end(),
                       [](const auto &function) { return function.action; }),
           "action identity survives into FIR");
    expect(std::any_of(first.fir->functions.begin(), first.fir->functions.end(),
                       [](const auto &function) { return function.constructor; }),
           "constructor identity survives into FIR");
    const auto metadata = foundation::emitMetadata(*first.fir);
    expect(metadata.find("\"kind\":\"service\"") != std::string::npos,
           "service emits static application metadata");
    expect(metadata.find("\"kind\":\"ctor\"") != std::string::npos,
           "constructor emits static application metadata");
    expect(metadata.find("\"id\":\"Zero\"") != std::string::npos,
           "unannotated constructor emits static application metadata");
    expect(metadata.find("\"kind\":\"action\"") != std::string::npos,
           "action emits static application metadata");
    expect(foundation::emitC(*first.fir, "service.fn") ==
               foundation::emitC(*second.fir, "service.fn"),
           "service and action C emission is deterministic");
}

void applicationPlanValidatesStaticDependencyGraph() {
    const auto source = std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) /
                        "tests/cases/accept/application-plan.fn";
    auto analysis = foundation::analyzeProject(source);
    expect(!analysis.diagnostics.hasErrors(), "application plan fixture has no diagnostics");
    expect(analysis.semantic.has_value(), "application plan fixture has a semantic model");
    if (!analysis.semantic.has_value()) {
        return;
    }
    const auto fir = foundation::lower(analysis.program, *analysis.semantic);
    foundation::Diagnostics firstDiagnostics;
    foundation::Diagnostics secondDiagnostics;
    const auto first = foundation::emitApplicationPlan(fir, firstDiagnostics);
    const auto second = foundation::emitApplicationPlan(fir, secondDiagnostics);
    expect(!firstDiagnostics.hasErrors() && !secondDiagnostics.hasErrors(),
           "application plan validates its providers and lifetimes");
    expect(first == second, "application plan emission is deterministic");
    expect(first.find("\"provider\":\"SystemClock\"") != std::string::npos,
           "application plan resolves a contract to its concrete provider");
    expect(first.find("\"input\":true") != std::string::npos,
           "application plan keeps explicit boundary inputs");
    expect(first.find("\"type\":\"Fallible\",\"lifetime\":\"transient\","
                      "\"constructor\":\"Fallible.New\",\"fallible\":true") !=
               std::string::npos,
           "application plan retains fallible constructors");
    expect(first.find("\"name\":\"greeter.greet\"") != std::string::npos &&
               first.find("\"keys\":[\"ctrl+g\"]") != std::string::npos,
           "application plan keeps typed action dispatch metadata");
}

void openAPIGenerationUsesValidatedRouteGraph() {
    const auto source = std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) /
                        "tests/projects/openapi-document";
    auto analysis = foundation::analyzeProject(source);
    expect(!analysis.diagnostics.hasErrors(), "OpenAPI fixture has no diagnostics");
    expect(analysis.semantic.has_value(), "OpenAPI fixture has a semantic model");
    if (!analysis.semantic.has_value()) {
        return;
    }
    const auto fir = foundation::lower(analysis.program, *analysis.semantic);
    foundation::Diagnostics firstDiagnostics;
    foundation::Diagnostics secondDiagnostics;
    const auto first =
        foundation::emitOpenAPI(fir, firstDiagnostics, "Test API", "1.2.3");
    const auto second =
        foundation::emitOpenAPI(fir, secondDiagnostics, "Test API", "1.2.3");
    expect(!firstDiagnostics.hasErrors() && !secondDiagnostics.hasErrors(),
           "OpenAPI generation reuses the validated application graph");
    expect(first == second, "OpenAPI emission is deterministic");
    expect(first.find("\"openapi\": \"3.0.3\"") != std::string::npos &&
               first.find("\"title\": \"Test API\"") != std::string::npos,
           "OpenAPI emission keeps document identity");
    expect(first.find("\"/users/{id}\"") != std::string::npos &&
               first.find("\"minimum\": 18") != std::string::npos &&
               first.find("\"enum\": [\"admin\", \"member\"]") !=
                   std::string::npos,
           "OpenAPI emission normalizes paths and carries typed parameter metadata");
    expect(first.find("\"404\": {") != std::string::npos &&
               first.find("\"description\": \"User not found\"") !=
                   std::string::npos,
           "OpenAPI emission carries explicit responses");
}

void stateMachineDiagramsUseTypedTransitionMetadata() {
    constexpr std::string_view source = R"(
state_machine Order {
    state Draft
    state Submitted
    state Paid
    state Cancelled
    state Expired

    on Submit from Draft to Submitted
    on Pay from Submitted to Paid
    on Cancel from Draft, Submitted to Cancelled
    on Expire from Submitted to Expired after 50.milliseconds
}

fn main() i32 {
    discard Order.TimeoutForExpire(.Submitted)
    0
}
)";
    const auto checked = check(source);
    expect(!checked.diagnostics.hasErrors(), "state-machine diagram fixture is valid");
    if (!checked.fir.has_value()) {
        expect(false, "state-machine diagram fixture lowers to FIR");
        return;
    }

    foundation::Diagnostics firstDiagnostics;
    foundation::Diagnostics secondDiagnostics;
    const auto first = foundation::emitStateMachineDiagram(
        *checked.fir, firstDiagnostics, std::string("Order"),
        foundation::StateMachineDiagramFormat::Mermaid);
    const auto second = foundation::emitStateMachineDiagram(
        *checked.fir, secondDiagnostics, std::nullopt,
        foundation::StateMachineDiagramFormat::Mermaid);
    expect(!firstDiagnostics.hasErrors() && !secondDiagnostics.hasErrors(),
           "one state machine can be selected explicitly or by default");
    expect(first == second, "state-machine diagram emission is deterministic");
    expect(first == "stateDiagram-v2\n"
                    "    Draft --> Cancelled : Cancel\n"
                    "    Draft --> Submitted : Submit\n"
                    "    Submitted --> Cancelled : Cancel\n"
                    "    Submitted --> Expired : Expire after 50.milliseconds\n"
                    "    Submitted --> Paid : Pay\n",
           "Mermaid transitions are sorted and complete");

    const auto metadata = foundation::emitMetadata(*checked.fir);
    expect(metadata.find("\"timeoutNanoseconds\":50000000") != std::string::npos,
           "state timeout remains available in typed metadata");
    const auto generated = foundation::emitC(*checked.fir, "state-timeout.fn");
    expect(generated.find("fdn_timeout_result") != std::string::npos &&
               generated.find("UINT64_C(50000000)") != std::string::npos,
           "state timeout accessor lowers to a typed Option value");

    foundation::Diagnostics graphvizDiagnostics;
    const auto graphviz = foundation::emitStateMachineDiagram(
        *checked.fir, graphvizDiagnostics, std::string("Order"),
        foundation::StateMachineDiagramFormat::Graphviz);
    expect(!graphvizDiagnostics.hasErrors() &&
               graphviz.find("digraph FSM {\n") == 0 &&
               graphviz.find("Draft -> Submitted [label=\"Submit\"];") !=
                   std::string::npos &&
               graphviz.find(
                   "Submitted -> Expired [label=\"Expire after 50.milliseconds\"];") !=
                   std::string::npos &&
               graphviz.ends_with("}\n"),
           "Graphviz emission uses the same typed transition graph");

    foundation::Diagnostics missingDiagnostics;
    const auto missing = foundation::emitStateMachineDiagram(
        *checked.fir, missingDiagnostics, std::string("Missing"),
        foundation::StateMachineDiagramFormat::Mermaid);
    expect(missing.empty() && hasCode(missingDiagnostics, "FDN2422"),
           "unknown state-machine selection has a stable diagnostic");

    constexpr std::string_view multipleSource = R"(
state_machine First {
    state Idle
}

state_machine Second {
    state Idle
}

fn main() i32 {
    0
}
)";
    const auto multiple = check(multipleSource);
    expect(multiple.fir.has_value(), "multiple state machines lower to FIR");
    if (multiple.fir.has_value()) {
        foundation::Diagnostics ambiguousDiagnostics;
        const auto ambiguous = foundation::emitStateMachineDiagram(
            *multiple.fir, ambiguousDiagnostics, std::nullopt,
            foundation::StateMachineDiagramFormat::Mermaid);
        expect(ambiguous.empty() && hasCode(ambiguousDiagnostics, "FDN2421"),
               "multiple state machines require an explicit selection");
    }

    const auto zeroTimeout = check(R"(
state_machine Invalid {
    state Pending
    state Expired
    on Expire from Pending to Expired after 0.seconds
}
fn main() i32 { 0 }
)");
    expect(hasCode(zeroTimeout.diagnostics, "FDN1255"),
           "state timeouts require a positive duration");

    const auto overflowingTimeout = check(R"(
state_machine Invalid {
    state Pending
    state Expired
    on Expire from Pending to Expired after 9223372036854775808.nanoseconds
}
fn main() i32 { 0 }
)");
    expect(hasCode(overflowingTimeout.diagnostics, "FDN1254"),
           "state timeouts fit the signed monotonic duration range");

    const auto parameterTimeout = check(R"(
state_machine Invalid {
    state Pending
    state Expired
    on Expire(reason i32) from Pending to Expired after 1.seconds
}
fn main() i32 { 0 }
)");
    expect(hasCode(parameterTimeout.diagnostics, "FDN1256"),
           "state timeouts reject caller-supplied parameters");

    const auto duplicateTimeout = check(R"(
state_machine Invalid {
    state Pending
    state First
    state Second
    on FirstTimeout from Pending to First after 1.seconds
    on SecondTimeout from Pending to Second after 2.seconds
}
fn main() i32 { 0 }
)");
    expect(hasCode(duplicateTimeout.diagnostics, "FDN1258"),
           "one source state accepts one declarative timeout");
}

void applicationHostEmitsTypedFoundationSource() {
    const auto source = std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) /
                        "examples/services";
    auto analysis = foundation::analyzeProject(source);
    expect(!analysis.diagnostics.hasErrors(), "application host example has no diagnostics");
    expect(analysis.semantic.has_value(), "application host example has a semantic model");
    if (!analysis.semantic.has_value()) {
        return;
    }
    const auto generatedSource = std::find_if(
        analysis.sources.begin(), analysis.sources.end(), [](const auto &candidate) {
            return candidate.path.ends_with(".foundation.generated.fn");
        });
    expect(generatedSource != analysis.sources.end(),
           "application host example includes its compiler-derived source");
    if (generatedSource == analysis.sources.end()) {
        return;
    }
    const auto fir = foundation::lower(analysis.program, *analysis.semantic);
    foundation::Diagnostics firstDiagnostics;
    foundation::Diagnostics secondDiagnostics;
    const auto first = foundation::emitApplicationHost(
        fir, firstDiagnostics, generatedSource->path);
    const auto second = foundation::emitApplicationHost(
        fir, secondDiagnostics, generatedSource->path);
    expect(!firstDiagnostics.hasErrors() && !secondDiagnostics.hasErrors(),
           "application host validates singleton construction");
    expect(first == second, "application host emission is deterministic");
    expect(first.find("struct FoundationApplication") != std::string::npos &&
               first.find("fn BuildFoundationApplication") != std::string::npos,
           "application host emits a typed application root");
    expect(first.find("fn Greet(self, name String) String") != std::string::npos,
           "application host emits a typed action method");
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
            expect(project->sources[diagnostic.span.source].path == "second/main.fn",
                   "multiple-main diagnostic points to the second entry point");
        }
        return;
    }
    expect(false, "multiple-main project reports FDN3010");
}

void standardLibrarySourceIsLoadedOnce() {
    const auto standardRoot =
        std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) / "std";
    std::size_t sourceCount{};
    for (const auto &entry : std::filesystem::recursive_directory_iterator(standardRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fn") {
            ++sourceCount;
        }
    }
    const auto frameworkRoot =
        std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR) / "foundation";
    for (const auto &entry : std::filesystem::recursive_directory_iterator(frameworkRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fn") {
            ++sourceCount;
        }
    }

    const auto source = std::filesystem::relative(
        standardRoot / "collections/list.fn", std::filesystem::current_path());
    foundation::Diagnostics diagnostics;
    const auto project = foundation::loadProject(source, diagnostics);
    expect(project.has_value(), "relative standard library source is loaded");
    if (!project.has_value()) {
        return;
    }
    expect(project->sources.size() == sourceCount,
           "relative standard library source is not loaded twice");
    expect(!hasCode(diagnostics, "FDN2020"),
           "relative standard library source has no duplicate types");
    expect(!hasCode(diagnostics, "FDN2095"),
           "relative standard library source has no duplicate methods");
    expect(!hasCode(diagnostics, "FDN2001"),
           "relative standard library source has no duplicate functions");
}

void documentationUsesCompilerSymbols() {
    constexpr std::string_view source = R"(
package sample.docs

// Marks public operations.
attribute PublicTag(value String) targets(fn, method) repeatable

// Supplies a visible name.
contract Named {
    // Prints the visible name.
    fn Display(self) void
}

// Stores one documented user.
struct User implements Named {
    // The visible name.
    Name String
    secret i32

    // Prints the visible name.
    fn Display(self) void {
        print(self.Name)
    }

    fn hidden(self) i32 {
        self.secret
    }
}

// Describes an operation result.
enum Outcome {
    // The operation succeeded.
    Ready
    hidden
}

// Creates a user.
@PublicTag("factory")
fn NewUser(
    // The visible name.
    $name String
) User {
    User { Name = name secret = 0 }
}

fn helper() i32 { 1 }
fn main() i32 { 0 }
)";
    auto checked = check(source);
    expect(!checked.diagnostics.hasErrors(), "documentation fixture is valid");
    for (auto &declaration : checked.program.structs) {
        declaration.packageName = "sample.docs";
    }
    for (auto &declaration : checked.program.enums) {
        declaration.packageName = "sample.docs";
    }
    for (auto &declaration : checked.program.contracts) {
        declaration.packageName = "sample.docs";
    }
    for (auto &declaration : checked.program.attributeDeclarations) {
        declaration.packageName = "sample.docs";
    }
    for (auto &function : checked.program.functions) {
        function.packageName = "sample.docs";
    }
    foundation::ProjectAnalysis analysis;
    analysis.sources.emplace_back("api.fn", std::string(source), "api.fn", "sample.docs");
    analysis.program = std::move(checked.program);
    analysis.semantic = std::move(checked.semantic);
    const auto first = foundation::emitDocumentation(analysis);
    const auto second = foundation::emitDocumentation(analysis);
    expect(first == second, "documentation emission is deterministic");
    expect(first.find("## Package `sample.docs`") != std::string::npos &&
               first.find("#### `User`") != std::string::npos &&
               first.find("Stores one documented user.") != std::string::npos &&
               first.find("Implements: `Named`.") != std::string::npos,
           "documentation emits exported type facts and prose");
    expect(first.find("- `Name String`\n\n  The visible name.") != std::string::npos &&
               first.find("###### `Display`") != std::string::npos,
           "documentation emits exported fields and methods");
    expect(first.find("#### `NewUser`") != std::string::npos &&
               first.find("- `name`\n\n  The visible name.") != std::string::npos,
           "documentation emits callable and optional parameter prose");
    expect(first.find("#### `@PublicTag`") != std::string::npos &&
               first.find("Targets: `function`, `method`. Repeatable.") != std::string::npos,
           "documentation emits typed attribute targets");
    expect(first.find("helper") == std::string::npos &&
               first.find("secret") == std::string::npos &&
               first.find("hidden") == std::string::npos &&
               first.find("main") == std::string::npos,
           "documentation omits internal declarations and members");
}

} // namespace

int main() {
    llvmIsTheDefaultNativeBackend();
    targetAttributesSelectOneDeclaration();
    typedAttributesEmitMetadataWithoutRuntimeCode();
    typedProgramLowersToDeterministicC();
    immutableBindingsAndCommentsLexDeterministically();
    tasksLowerToOwnedRuntimeHandles();
    taskWaitsLowerToStacklessStates();
    dynamicSelectTimeoutsLowerToStoredDeadlines();
    structValuesLowerToDeterministicC();
    deepStructGraphsStayIterative();
    enumMatchesLowerToDeterministicC();
    guardedMatchesLowerToDeterministicC();
    genericValuesMonomorphizeDeterministically();
    copyGenericCallbackOptionsInternOnce();
    genericLookaheadStaysTypeAware();
    ownershipLowersToDeterministicC();
    ownedPlacesLowerToDeterministicC();
    sequenceValuesLowerToDeterministicC();
    mainArgumentsLowerToPortableWrapper();
    sequenceLengthsLowerToU64();
    u64ValuesLowerToCheckedC();
    machineScalarsAndNeverLowerToPortableC();
    floatingLiteralsRejectOverflow();
    methodsAndContractsLowerToDeterministicC();
    taskContractConversionsReceiveEarlyVtableDeclarations();
    contractInheritanceFlattensDeterministically();
    lightweightSyntaxCarriesVisibilityAndContext();
    panicLowersWithSourceFrames();
    divergingCallsCloseGeneratedControlFlow();
    exhaustiveMatchExitsCloseGeneratedControlFlow();
    syntaxFailuresHaveStableDiagnostics();
    semanticFailuresHaveStableDiagnostics();
    diagnosticsBoundLongSourceExcerpts();
    packageHeadersAndSourceDiagnosticsStayStable();
    cAbiFunctionsLowerToDeterministicBoundaries();
    cFunctionPointersRejectManagedCallablesAndOpaqueRecords();
    packageInterfacesUseReachableMonomorphizedCBoundaries();
    rawPointersLowerToExplicitCBoundaries();
    nativeSystemsPrimitivesLowerToBothBoundaries();
    blockingImportsLowerToTaskSuspension();
    callbackImportsLowerToReactorSuspension();
    closuresLowerToDeterministicFunctionValues();
    taskFunctionValuesReceiveEarlyAdapterPrototypes();
    servicesAndActionsLowerToStaticApplicationMetadata();
    applicationPlanValidatesStaticDependencyGraph();
    openAPIGenerationUsesValidatedRouteGraph();
    stateMachineDiagramsUseTypedTransitionMetadata();
    applicationHostEmitsTypedFoundationSource();
    projectDiagnosticsRetainTheirSource();
    standardLibrarySourceIsLoadedOnce();
    documentationUsesCompilerSymbols();

    if (failures != 0) {
        std::cerr << failures << " test assertions failed\n";
        return 1;
    }
    std::cout << "compiler tests passed\n";
    return 0;
}
