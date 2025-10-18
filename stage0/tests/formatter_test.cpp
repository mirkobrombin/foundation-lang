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
        "struct Pair<T>{\r\n"
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
        "let result=choose<i32> (1,2)\r\n"
        "return result\r\n"
        "}\r\n";
    constexpr std::string_view expected =
        "package sample\n"
        "// pair values\n"
        "struct Pair<T> {\n"
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
        "    let result = choose<i32>(1, 2)\n"
        "    return result\n"
        "}\n";

    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "valid source formats without diagnostics");
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
        "let typed fn(i32) i32 = identity < i32 >\n"
        "let low = 1\n"
        "let high = 2\n"
        "let values=[1,2]\n"
        "let first=values [0]\n"
        "let fixed[2]i32=[1,2]\n"
        "let ordered = low<high\n"
        "let chained = low<high>low\n"
        "let continued = low<\n"
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

void preservesDocumentationAndNestedComments() {
    constexpr std::string_view source =
        "/** Long documentation.\r\n"
        "No leading stars are required.\r\n"
        "/* Nested detail. */\r\n"
        "*/\r\n"
        "fn main()i32{\r\n"
        "/// Immutable answer.   \r\n"
        "const answer=21 /* keep this prose */\r\n"
        "// implementation note   \r\n"
        "answer*2-42\r\n"
        "}\r\n";
    constexpr std::string_view expected =
        "/** Long documentation.\n"
        "No leading stars are required.\n"
        "/* Nested detail. */\n"
        "*/\n"
        "fn main() i32 {\n"
        "    /// Immutable answer.\n"
        "    const answer = 21 /* keep this prose */\n"
        "    // implementation note\n"
        "    answer * 2 - 42\n"
        "}\n";
    const auto formatted = foundation::formatSource(source);
    expect(!formatted.diagnostics.hasErrors(), "documentation comments format without errors");
    expect(formatted.contents == expected,
           "formatter distinguishes comments while preserving block prose");
    expect(foundation::formatSource(formatted.contents).contents == formatted.contents,
           "comment formatting is idempotent");
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
        root / "examples", root / "std", root / "tests/cases/accept", root / "tests/projects"};
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
    preservesDocumentationAndNestedComments();
    rejectsInvalidSourceWithoutChangingIt();
    preservesEstablishedFoundationStyle();
    formatsRepositorySources();
    if (failures != 0) {
        std::cerr << failures << " formatter test(s) failed\n";
        return 1;
    }
    return 0;
}
