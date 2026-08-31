#include "foundation/imports.hpp"

#include "foundation/driver.hpp"
#include "foundation/lexer.hpp"
#include "foundation/parser.hpp"
#include "foundation/token.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace foundation {

namespace {

struct ImportLine {
    std::string packageName;
    std::string alias;
    std::string prefix;
    std::string suffix;
};

struct ImportRange {
    std::size_t start{};
    std::size_t end{};
    std::size_t syntaxEnd{};
};

std::string shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return std::string(name.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

std::string packageAlias(std::string_view packageName) { return shortName(packageName); }

std::vector<std::size_t> lineStarts(std::string_view source) {
    std::vector<std::size_t> result{0};
    for (std::size_t offset = 0; offset < source.size(); ++offset) {
        if (source[offset] == '\n') {
            result.push_back(offset + 1);
        }
    }
    return result;
}

std::size_t lineStart(const std::vector<std::size_t> &starts, std::size_t line) {
    return line == 0 || line > starts.size() ? 0 : starts[line - 1];
}

std::size_t lineEnd(const std::vector<std::size_t> &starts, std::size_t line,
                    std::size_t sourceSize) {
    return line < starts.size() ? starts[line] : sourceSize;
}

std::size_t contentEnd(std::string_view source, std::size_t start, std::size_t end) {
    while (end > start && (source[end - 1] == '\r' || source[end - 1] == '\n')) {
        --end;
    }
    return end;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool whitespace(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    });
}

std::string newlineFor(std::string_view source) {
    return source.find("\r\n") == std::string_view::npos ? "\n" : "\r\n";
}

std::map<std::string, std::set<std::string>> packageSymbols(const ProjectAnalysis &analysis) {
    std::map<std::string, std::set<std::string>> result;
    const auto add = [&result](std::string_view packageName, std::string_view name, bool exported) {
        if (!exported || packageName.empty() || packageName == "std.prelude") {
            return;
        }
        result[std::string(packageName)].insert(shortName(name));
    };
    for (const auto &declaration : analysis.program.structs) {
        add(declaration.packageName, declaration.name, declaration.exported);
    }
    for (const auto &declaration : analysis.program.enums) {
        add(declaration.packageName, declaration.name, declaration.exported);
    }
    for (const auto &declaration : analysis.program.contracts) {
        add(declaration.packageName, declaration.name, declaration.exported);
    }
    for (const auto &declaration : analysis.program.attributeDeclarations) {
        add(declaration.packageName, declaration.name, declaration.exported);
    }
    for (const auto &function : analysis.program.functions) {
        if (function.ownerType.empty() && !function.closure && !function.testName.has_value()) {
            add(function.packageName, function.name, function.exported);
        }
    }
    return result;
}

std::optional<std::string> unknownAlias(const Diagnostic &diagnostic) {
    constexpr std::string_view unknownImport = "unknown import alias ";
    constexpr std::string_view unknownBinding = "unknown binding ";
    if (diagnostic.code == "FDN3009" && diagnostic.message.starts_with(unknownImport)) {
        return diagnostic.message.substr(unknownImport.size());
    }
    if (diagnostic.code == "FDN2004" && diagnostic.message.starts_with(unknownBinding)) {
        return diagnostic.message.substr(unknownBinding.size());
    }
    return std::nullopt;
}

std::optional<std::string> memberAfterAlias(const std::vector<Token> &tokens,
                                            const Diagnostic &diagnostic, std::string_view alias) {
    const auto diagnosticEnd = diagnostic.span.offset + diagnostic.span.length;
    for (std::size_t index = 0; index + 2 < tokens.size(); ++index) {
        const auto &token = tokens[index];
        const auto selected =
            diagnostic.span.length == 0
                ? token.span.offset == diagnostic.span.offset
                : token.span.offset >= diagnostic.span.offset && token.span.offset < diagnosticEnd;
        if (!selected || token.text != alias || tokens[index + 1].kind != TokenKind::Dot ||
            tokens[index + 2].kind != TokenKind::Identifier) {
            continue;
        }
        return tokens[index + 2].text;
    }
    return std::nullopt;
}

std::set<std::string>
inferredPackages(const ProjectAnalysis &analysis, std::size_t sourceId,
                 const std::vector<Token> &tokens,
                 const std::unordered_set<std::string> &importedPackages,
                 const std::map<std::string, std::set<std::string>> &symbols) {
    std::map<std::string, std::set<std::string>> aliases;
    for (const auto &diagnostic : analysis.diagnostics.all()) {
        if (diagnostic.span.source != sourceId) {
            continue;
        }
        const auto alias = unknownAlias(diagnostic);
        if (!alias.has_value()) {
            continue;
        }
        const auto member = memberAfterAlias(tokens, diagnostic, *alias);
        if (!member.has_value()) {
            continue;
        }
        std::set<std::string> candidates;
        for (const auto &[packageName, names] : symbols) {
            if (packageName != analysis.sources[sourceId].packageName &&
                !importedPackages.contains(packageName) && packageAlias(packageName) == *alias &&
                names.contains(*member)) {
                candidates.insert(packageName);
            }
        }
        const auto found = aliases.find(*alias);
        if (found == aliases.end()) {
            aliases.emplace(*alias, std::move(candidates));
            continue;
        }
        std::set<std::string> intersection;
        std::set_intersection(found->second.begin(), found->second.end(), candidates.begin(),
                              candidates.end(), std::inserter(intersection, intersection.begin()));
        found->second = std::move(intersection);
    }

    std::set<std::string> result;
    for (const auto &[alias, candidates] : aliases) {
        static_cast<void>(alias);
        if (candidates.size() == 1) {
            result.insert(*candidates.begin());
        }
    }
    return result;
}

std::optional<std::vector<ImportRange>> importRanges(const Program &program,
                                                     const std::vector<Token> &tokens,
                                                     const std::vector<std::size_t> &starts,
                                                     std::size_t sourceSize) {
    std::vector<ImportRange> result;
    std::size_t tokenIndex{};
    std::size_t previousEnd{};
    for (const auto &imported : program.imports) {
        while (tokenIndex < tokens.size() &&
               (tokens[tokenIndex].kind != TokenKind::Import ||
                tokens[tokenIndex].span.offset >= imported.span.offset)) {
            ++tokenIndex;
        }
        if (tokenIndex == tokens.size()) {
            return std::nullopt;
        }
        const auto opening = tokenIndex;
        auto closing = opening + 1;
        if (closing == tokens.size()) {
            return std::nullopt;
        }
        const auto segments = static_cast<std::size_t>(std::count(
                                  imported.packageName.begin(), imported.packageName.end(), '.')) +
                              1;
        for (std::size_t segment = 1; segment < segments; ++segment) {
            if (closing + 2 >= tokens.size() || tokens[closing + 1].kind != TokenKind::Dot) {
                return std::nullopt;
            }
            closing += 2;
        }
        if (closing + 2 < tokens.size() && tokens[closing + 1].kind == TokenKind::As &&
            tokens[closing + 2].kind == TokenKind::Identifier) {
            closing += 2;
        }
        const auto start = lineStart(starts, tokens[opening].span.line);
        const auto end = lineEnd(starts, tokens[closing].span.line, sourceSize);
        if (start < previousEnd) {
            return std::nullopt;
        }
        result.push_back({start, end, tokens[closing].span.offset + tokens[closing].span.length});
        previousEnd = end;
        tokenIndex = closing + 1;
    }
    return result;
}

std::size_t packageLineEnd(const Program &program, const std::vector<Token> &tokens,
                           const std::vector<std::size_t> &starts, std::size_t sourceSize) {
    if (!program.hasPackageDeclaration) {
        return 0;
    }
    const auto package = std::find_if(tokens.begin(), tokens.end(), [](const Token &token) {
        return token.kind == TokenKind::Package;
    });
    return package == tokens.end() ? 0 : lineEnd(starts, package->span.line, sourceSize);
}

} // namespace

ImportOrganizationResult organizeImports(const ProjectAnalysis &analysis, std::size_t sourceId) {
    ImportOrganizationResult result;
    if (sourceId >= analysis.sources.size()) {
        return result;
    }
    const auto &source = analysis.sources[sourceId];
    result.contents = source.contents;

    Lexer lexer(source.contents, result.diagnostics, sourceId);
    auto tokens = lexer.scan();
    Parser parser(tokens, result.diagnostics, false, hostTargetPlatform());
    const auto program = parser.parse();
    if (result.diagnostics.hasErrors()) {
        return result;
    }

    const auto starts = lineStarts(source.contents);
    const auto ranges = importRanges(program, tokens, starts, source.contents.size());
    if (!ranges.has_value()) {
        return result;
    }

    std::unordered_map<std::string, std::string> aliases;
    std::unordered_set<std::string> importedPackages;
    for (const auto &imported : program.imports) {
        const auto alias =
            imported.alias.empty() ? packageAlias(imported.packageName) : imported.alias;
        if (!aliases.emplace(alias, imported.packageName).second ||
            !importedPackages.insert(imported.packageName).second) {
            return result;
        }
    }

    std::unordered_set<std::string> usedAliases;
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index].kind != TokenKind::Identifier ||
            tokens[index + 1].kind != TokenKind::Dot || !aliases.contains(tokens[index].text)) {
            continue;
        }
        const auto inImport = std::any_of(ranges->begin(), ranges->end(), [&](ImportRange range) {
            return tokens[index].span.offset >= range.start &&
                   tokens[index].span.offset < range.end;
        });
        if (!inImport) {
            usedAliases.insert(tokens[index].text);
        }
    }

    std::vector<ImportLine> imports;
    imports.reserve(program.imports.size());
    std::size_t previousEnd = ranges->empty() ? 0 : ranges->front().start;
    std::string pendingPrefix;
    for (std::size_t index = 0; index < program.imports.size(); ++index) {
        const auto &imported = program.imports[index];
        const auto alias =
            imported.alias.empty() ? packageAlias(imported.packageName) : imported.alias;
        const auto prefix =
            source.contents.substr(previousEnd, (*ranges)[index].start - previousEnd);
        if (!whitespace(prefix)) {
            pendingPrefix += prefix;
        }
        previousEnd = (*ranges)[index].end;
        if (!usedAliases.contains(alias)) {
            continue;
        }
        const auto suffixStart = (*ranges)[index].syntaxEnd;
        const auto suffixEnd = contentEnd(source.contents, suffixStart, (*ranges)[index].end);
        imports.push_back(
            {imported.packageName, imported.alias, std::move(pendingPrefix),
             std::string(trim(source.contents.substr(suffixStart, suffixEnd - suffixStart)))});
        pendingPrefix.clear();
    }

    const auto symbols = packageSymbols(analysis);
    const auto inferred = inferredPackages(analysis, sourceId, tokens, importedPackages, symbols);
    if (program.imports.empty() && inferred.empty()) {
        return result;
    }
    for (const auto &packageName : inferred) {
        imports.push_back({packageName, {}, {}, {}});
    }

    std::sort(imports.begin(), imports.end(), [](const ImportLine &left, const ImportLine &right) {
        return std::tie(left.packageName, left.alias) < std::tie(right.packageName, right.alias);
    });

    const auto newline = newlineFor(source.contents);
    std::string replacement;
    for (const auto &imported : imports) {
        replacement += imported.prefix;
        replacement += "import " + imported.packageName;
        if (!imported.alias.empty()) {
            replacement += " as " + imported.alias;
        }
        if (!imported.suffix.empty()) {
            replacement += ' ';
            replacement += imported.suffix;
        }
        replacement += newline;
    }
    if (!imports.empty() && !pendingPrefix.empty()) {
        replacement += newline;
    }
    replacement += pendingPrefix;

    std::size_t replaceStart{};
    std::size_t replaceEnd{};
    const auto packageEnd = packageLineEnd(program, tokens, starts, source.contents.size());
    if (ranges->empty()) {
        replaceStart = packageEnd;
        replaceEnd = packageEnd;
        while (replaceEnd < source.contents.size() &&
               (source.contents[replaceEnd] == ' ' || source.contents[replaceEnd] == '\t' ||
                source.contents[replaceEnd] == '\r' || source.contents[replaceEnd] == '\n')) {
            ++replaceEnd;
        }
    } else {
        replaceStart = ranges->front().start;
        if (packageEnd <= replaceStart &&
            whitespace(source.contents.substr(packageEnd, replaceStart - packageEnd))) {
            replaceStart = packageEnd;
        }
        replaceEnd = ranges->back().end;
        while (replaceEnd < source.contents.size() &&
               (source.contents[replaceEnd] == ' ' || source.contents[replaceEnd] == '\t' ||
                source.contents[replaceEnd] == '\r' || source.contents[replaceEnd] == '\n')) {
            ++replaceEnd;
        }
    }

    if (packageEnd != 0 && replaceStart == packageEnd) {
        replacement.insert(0, newline);
    }
    if (!imports.empty() || !pendingPrefix.empty()) {
        replacement += newline;
    }

    result.contents.replace(replaceStart, replaceEnd - replaceStart, replacement);

    Diagnostics validation;
    Lexer validationLexer(result.contents, validation, sourceId);
    Parser validationParser(validationLexer.scan(), validation, false, hostTargetPlatform());
    static_cast<void>(validationParser.parse());
    if (validation.hasErrors()) {
        result.contents = source.contents;
        result.diagnostics = std::move(validation);
    }
    return result;
}

} // namespace foundation
