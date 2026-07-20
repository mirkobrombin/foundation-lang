#include "foundation/formatter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures{};

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void formatsFoundationSource() {
    constexpr std::string_view source =
        "package   sample\r\n"
        "// pair values   \r\n"
        "contract Stored<T>{fn read(self)T}\r\n"
        "struct Pair<T>implements Stored<T>{\r\n"
        "left   T // first   \r\n"
        "right T\r\n"
        "}\r\n"
        "\r\n"
        "fn choose<T> (value T,fallback T)T{\r\n"
        "if true{\r\n"
        "return value\r\n"
        "}else{\r\n"
        "return fallback\r\n"
        "}\r\n"
        "}\r\n"
        "\r\n"
        "fn main()i32{\r\n"
        "const result=choose<i32> (1,2)\r\n"
        "return result\r\n"
        "}\r\n";
    constexpr std::string_view expected =
        "package sample\n"
        "// pair values\n"
        "contract Stored<T> { fn read(self) T }\n"
        "struct Pair<T> implements Stored<T> {\n"
        "    left T // first\n"
        "    right T\n"
        "}\n"
        "\n"
        "fn choose<T>(value T, fallback T) T {\n"
        "    if true {\n"
        "        return value\n"
        "    } else {\n"
        "        return fallback\n"
        "    }\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    const result = choose<i32>(1, 2)\n"
        "    return result\n"
        "}\n";

    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "valid source formats without diagnostics");
    if (formatted.contents != expected) {
        std::cerr << formatted.contents;
    }
    expect(formatted.contents == expected,
           "formatter applies canonical whitespace and indentation");
    const auto repeated = foundation::formatSource(formatted.contents);
    expect(!repeated.diagnostics.hasErrors(), "formatted source remains valid");
    expect(repeated.contents == formatted.contents, "formatter is idempotent");
}

void preservesLineSensitiveSyntax() {
    constexpr std::string_view source =
        "fn identity < T > (value T) T {\n"
        "value\n"
        "}\n"
        "fn main() i32 {\n"
        "const typed fn(i32) i32 = identity < i32 >\n"
        "const low = 1\n"
        "const high = 2\n"
        "const values=[1,2]\n"
        "const first=values [0]\n"
        "const fixed[2]i32=[1,2]\n"
        "const ordered = low<high\n"
        "const chained = low<high>low\n"
        "const continued = low<\n"
        "high\n"
        "if ordered && continued {\n"
        "typed(4)\n"
        "} else {\n"
        "0\n"
        "}\n"
        "}\n";
    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "generic and comparison source formats");
    expect(formatted.contents.find("identity<i32>") != std::string::npos,
           "generic application stays compact");
    expect(formatted.contents.find("low < high") != std::string::npos,
           "comparison operators receive canonical spaces");
    expect(formatted.contents.find("low < high > low") != std::string::npos,
           "comparison chains do not become generic applications");
    expect(formatted.contents.find("low <\n        high") != std::string::npos,
           "multiline comparisons receive continuation indentation");
    expect(formatted.contents.find("values[0]") != std::string::npos,
           "index expressions stay attached to their base");
    expect(formatted.contents.find("fixed [2]i32") != std::string::npos,
           "fixed array types stay separated from binding names");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "line-sensitive source remains idempotent");
}

void preservesLineAndNestedBlockComments() {
    constexpr std::string_view source =
        "/* Long comment.\r\n"
        "No leading stars are required.\r\n"
        "/* Nested detail. */\r\n"
        "*/\r\n"
        "fn main()i32{\r\n"
        "// Immutable answer.   \r\n"
        "const answer=21 /* keep this prose */\r\n"
        "// implementation note   \r\n"
        "answer*2-42\r\n"
        "}\r\n";
    constexpr std::string_view expected =
        "/* Long comment.\n"
        "No leading stars are required.\n"
        "/* Nested detail. */\n"
        "*/\n"
        "fn main() i32 {\n"
        "    // Immutable answer.\n"
        "    const answer = 21 /* keep this prose */\n"
        "    // implementation note\n"
        "    answer * 2 - 42\n"
        "}\n";
    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "line and nested block comments format without errors");
    expect(formatted.contents == expected,
           "formatter preserves line comments and nested block prose");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "comment formatting is idempotent");
}

void keepsTaskOwnershipCompact() {
    constexpr std::string_view source =
        "struct Session{label String}\n"
        "fn rename(&session Session,$label String)void{session.label=label}\n"
        "task double(value i32)i32{value*2}\n"
        "fn main()i32{\n"
        "var session=new Session{label=\"draft\"}\n"
        "const label=\"ready\"\n"
        "rename(& session,$ label)\n"
        "const pending=spawn double(21)\n"
        "const result=$ pending.wait()\n"
        "result-42\n"
        "}\n";
    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "task ownership source formats");
    expect(formatted.contents.find("fn rename(&session Session, $label String) void") !=
               std::string::npos,
           "parameter ownership markers stay attached to names");
    expect(formatted.contents.find("var session = new Session { label = \"draft\" }") !=
               std::string::npos,
           "new construction keeps canonical spacing");
    expect(formatted.contents.find("rename(&session, $label)") != std::string::npos,
           "call ownership markers stay attached to arguments");
    expect(formatted.contents.find("const result = $pending.wait()") != std::string::npos,
           "task transfer marker stays attached to its handle");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "task formatting is idempotent");
}

void formatsNestedCallableSignatures() {
    constexpr std::string_view source =
        "fn chain<E>(\n"
        "$callback fn(\n"
        "$i32,\n"
        "fn($i32)Result<i32,E>\n"
        ")Result<i32,E>\n"
        ")Result<i32,E>{\n"
        "const seed=1\n"
        "callback(\n"
        "1,\n"
        "fn(value i32)Result<i32,E>capture seed{\n"
        ".Ok(value+seed)\n"
        "}\n"
        ")\n"
        "}\n";
    constexpr std::string_view expected =
        "fn chain<E>(\n"
        "    $callback fn(\n"
        "        $i32,\n"
        "        fn($i32) Result<i32, E>\n"
        "    ) Result<i32, E>\n"
        ") Result<i32, E> {\n"
        "    const seed = 1\n"
        "    callback(\n"
        "        1,\n"
        "        fn(value i32) Result<i32, E> capture seed {\n"
        "            .Ok(value + seed)\n"
        "        }\n"
        "    )\n"
        "}\n";

    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "nested callable source formats");
    if (formatted.contents != expected) {
        std::cerr << formatted.contents;
    }
    expect(formatted.contents == expected,
           "nested callable types and captures retain type spacing and indentation");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "nested callable formatting is idempotent");
}

void formatsWorkflowCompensation() {
    constexpr std::string_view source =
        "fn reserve(value i32)Result<void,bool>{\n"
        "discard value\n"
        ".Ok\n"
        "}\n"
        "fn release(value i32)Result<void,bool>{\n"
        "discard value\n"
        ".Ok\n"
        "}\n"
        "fn finish(value i32)Result<i32,bool>{\n"
        ".Ok(value)\n"
        "}\n"
        "saga Checkout(input i32)Result<i32,bool>{\n"
        "step reserve using reserve retry exponential(max=2)\n"
        "compensate release\n"
        "step finish using finish\n"
        "}\n";
    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "workflow source formats");
    expect(formatted.contents.find(
               "    step reserve using reserve retry exponential(max = 2)\n"
               "        compensate release\n"
               "    step finish using finish") != std::string::npos,
           "workflow steps and compensations use canonical indentation");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "workflow formatting is idempotent");
}

void rejectsInvalidSourceWithoutChangingIt() {
    constexpr std::string_view source = "fn main( {\n";
    const auto formatted = foundation::formatSource(source);
    expect(formatted.diagnostics.hasErrors(), "invalid source reports parser diagnostics");
    expect(formatted.contents == source, "invalid source is never rewritten");
}

void preservesEstablishedFoundationStyle() {
    const auto root = std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR);
    const std::vector<std::filesystem::path> sources{
        root / "examples/language-tour/main.fdn",
        root / "std/json/json.fdn",
        root / "tests/cases/accept/primitive-values.fdn",
        root / "tests/cases/accept/sequences.fdn",
        root / "tests/cases/accept/typed-attributes.fdn",
        root / "tests/cases/accept/raw-pointers.fdn",
        root / "tests/cases/accept/service-actions.fdn",
        root / "tests/cases/accept/text-path.fdn",
    };
    for (const auto &source : sources) {
        std::ifstream input(source, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        const auto original = contents.str();
        const auto formatted = foundation::formatSource(original);
        expect(!formatted.diagnostics.hasErrors(), "canonical source remains valid");
        if (formatted.contents != original) {
            std::cerr << "FAIL: formatter changed canonical style in " << source.string() << '\n';
            ++failures;
        }
    }
}

void formatsRepositorySources() {
    const auto root = std::filesystem::path(FOUNDATION_TEST_SOURCE_DIR);
    const std::vector<std::filesystem::path> sourceRoots{
        root / "examples", root / "foundation", root / "std", root / "tests/cases/accept",
        root / "tests/projects"};
    std::vector<std::filesystem::path> sources;
    for (const auto &sourceRoot : sourceRoots) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
            const auto path = entry.path().generic_string();
            if (entry.is_regular_file() && entry.path().extension() == ".fdn" &&
                path.find("/malformed-") == std::string::npos) {
                sources.push_back(entry.path());
            }
        }
    }
    std::sort(sources.begin(), sources.end());
    expect(!sources.empty(), "repository formatter corpus is not empty");
    for (const auto &source : sources) {
        std::ifstream input(source, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        const auto formatted = foundation::formatSource(contents.str());
        if (formatted.diagnostics.hasErrors()) {
            std::cerr << "FAIL: formatter rejected " << source.string() << '\n';
            ++failures;
            continue;
        }
        const auto repeated = foundation::formatSource(formatted.contents);
        if (repeated.diagnostics.hasErrors() || repeated.contents != formatted.contents) {
            std::cerr << "FAIL: formatter is not idempotent for " << source.string() << '\n';
            ++failures;
        }
    }
}

} // namespace

int main() {
    formatsFoundationSource();
    preservesLineSensitiveSyntax();
    preservesLineAndNestedBlockComments();
    keepsTaskOwnershipCompact();
    formatsNestedCallableSignatures();
    formatsWorkflowCompensation();
    rejectsInvalidSourceWithoutChangingIt();
    preservesEstablishedFoundationStyle();
    formatsRepositorySources();
    if (failures != 0) {
        std::cerr << failures << " formatter test(s) failed\n";
        return 1;
    }
    return 0;
}
