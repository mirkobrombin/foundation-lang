#include "foundation/lint.hpp"

#include "foundation/driver.hpp"
#include "foundation/language_service.hpp"
#include "foundation/lexer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace foundation {

namespace {

struct Finding {
    std::string code;
    std::string message;
    SourceSpan span;
};

struct SourceLine {
    std::string_view text;
    std::size_t offset{};
    std::size_t line{};
};

using Suppressions = std::map<std::pair<std::size_t, std::size_t>, std::set<std::string>>;

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::vector<SourceLine> sourceLines(std::string_view source) {
    std::vector<SourceLine> result;
    auto offset = std::size_t{};
    auto line = std::size_t{1};
    while (offset <= source.size()) {
        const auto end = source.find('\n', offset);
        const auto lineEnd = end == std::string_view::npos ? source.size() : end;
        result.push_back({source.substr(offset, lineEnd - offset), offset, line});
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
        ++line;
    }
    return result;
}

std::string documentationBefore(std::string_view source, std::size_t offset) {
    const auto lines = sourceLines(source);
    auto line = std::size_t{};
    while (line + 1 < lines.size() && lines[line + 1].offset <= offset) {
        ++line;
    }
    while (line != 0 && trim(lines[line - 1].text).starts_with("@")) {
        --line;
    }
    std::vector<std::string_view> documentation;
    while (line != 0) {
        const auto previous = trim(lines[line - 1].text);
        if (!previous.starts_with("//")) {
            break;
        }
        documentation.push_back(previous.substr(2));
        --line;
    }
    std::string result;
    for (auto iterator = documentation.rbegin(); iterator != documentation.rend(); ++iterator) {
        if (!result.empty()) {
            result.push_back('\n');
        }
        result.append(trim(*iterator));
    }
    return result;
}

bool generatedSource(std::string_view source) {
    const auto start = source.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return false;
    }
    source.remove_prefix(start);
    return source.starts_with("// foundation:generated package/v1") ||
           source.starts_with("// foundation:generated application/v1");
}

bool projectSource(const ProjectAnalysis &analysis, std::size_t source,
                   const std::set<std::size_t> &projectSources) {
    if (source >= analysis.sources.size()) {
        return false;
    }
    const auto &input = analysis.sources[source];
    if (input.path.ends_with(".foundation.generated.fn") || generatedSource(input.contents)) {
        return false;
    }
    if (!projectSources.empty()) {
        return projectSources.contains(source);
    }
    return !input.path.starts_with("std/") && !input.path.starts_with("foundation/") &&
           !input.path.starts_with("packages/");
}

std::string typeKey(std::string_view packageName, std::string_view typeName) {
    std::string result;
    result.reserve(packageName.size() + typeName.size() + 1);
    result.append(packageName);
    result.push_back('\0');
    result.append(typeName);
    return result;
}

std::size_t nextColumn(std::size_t column, unsigned char byte) {
    if (byte == '\t') {
        return ((column - 1) / 4 + 1) * 4 + 1;
    }
    return (byte & 0xc0U) == 0x80U ? column : column + 1;
}

Suppressions collectSuppressions(std::string_view source, std::size_t sourceIndex,
                                 std::vector<Finding> &findings) {
    Suppressions result;
    const auto lines = sourceLines(source);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto text = trim(lines[index].text);
        constexpr std::string_view prefix = "// fcs:ignore";
        if (!text.starts_with(prefix)) {
            continue;
        }
        const auto remainder = trim(text.substr(prefix.size()));
        const auto separator = remainder.find_first_of(" \t");
        const auto code = separator == std::string_view::npos ? remainder
                                                               : remainder.substr(0, separator);
        const auto reason = separator == std::string_view::npos
                                ? std::string_view{}
                                : trim(remainder.substr(separator + 1));
        const auto validCode = configurableCodeStandardRule(code);
        if (!validCode || reason.empty()) {
            findings.push_back({"FCS9001",
                                "fcs:ignore requires one configurable FCS code and a non-empty reason",
                                {lines[index].offset, lines[index].text.size(), lines[index].line,
                                 1, sourceIndex}});
            continue;
        }
        auto target = index + 1;
        while (target < lines.size()) {
            const auto candidate = trim(lines[target].text);
            if (!candidate.empty() && !candidate.starts_with("//") &&
                !candidate.starts_with("/*")) {
                result[{sourceIndex, lines[target].line}].insert(std::string(code));
                break;
            }
            ++target;
        }
        if (target == lines.size()) {
            findings.push_back({"FCS9001", "fcs:ignore has no following source line to suppress",
                                {lines[index].offset, lines[index].text.size(), lines[index].line,
                                 1, sourceIndex}});
        }
    }
    return result;
}

bool suppressed(const Suppressions &suppressions, const Finding &finding) {
    const auto found = suppressions.find({finding.span.source, finding.span.line});
    return found != suppressions.end() && found->second.contains(finding.code);
}

void findLongLines(const ProjectAnalysis &analysis, std::size_t source, std::size_t width,
                   std::vector<Finding> &findings) {
    for (const auto &line : sourceLines(analysis.sources[source].contents)) {
        auto column = std::size_t{1};
        for (std::size_t cursor = 0; cursor < line.text.size(); ++cursor) {
            column = nextColumn(column, static_cast<unsigned char>(line.text[cursor]));
            if (column > width + 1) {
                findings.push_back({"FCS1001",
                                    "line exceeds the " + std::to_string(width) +
                                        " column profile width",
                                    {line.offset + cursor, line.text.size() - cursor, line.line,
                                     width + 1, source}});
                break;
            }
        }
    }
}

void findSignatureLayout(const ProjectAnalysis &analysis, std::size_t source,
                         std::vector<Finding> &findings) {
    const auto lines = sourceLines(analysis.sources[source].contents);
    Diagnostics lexerDiagnostics;
    const auto tokens = Lexer(analysis.sources[source].contents, lexerDiagnostics, source).scan();
    const auto declarationKeyword = [](TokenKind kind) {
        return kind == TokenKind::Fn || kind == TokenKind::Task ||
               kind == TokenKind::Ctor || kind == TokenKind::Action ||
               kind == TokenKind::Attribute;
    };
    for (std::size_t index = 0; index + 2 < tokens.size(); ++index) {
        if (!declarationKeyword(tokens[index].kind) ||
            tokens[index + 1].kind != TokenKind::Identifier) {
            continue;
        }
        auto open = index + 2;
        while (open < tokens.size() && tokens[open].kind != TokenKind::LeftParen &&
               tokens[open].kind != TokenKind::LeftBrace &&
               tokens[open].kind != TokenKind::Eof) {
            ++open;
        }
        if (open == tokens.size() || tokens[open].kind != TokenKind::LeftParen) {
            continue;
        }
        auto depth = std::size_t{};
        auto close = open;
        for (; close < tokens.size(); ++close) {
            if (tokens[close].kind == TokenKind::LeftParen) {
                ++depth;
            } else if (tokens[close].kind == TokenKind::RightParen) {
                if (--depth == 0) {
                    break;
                }
            }
        }
        if (close == tokens.size() || tokens[open].span.line == tokens[close].span.line) {
            continue;
        }
        const auto &openingLine = lines[tokens[open].span.line - 1];
        if (!trim(openingLine.text).ends_with("(")) {
            findings.push_back({"FCS1002",
                                "multiline signatures place the opening parenthesis last",
                                tokens[open].span});
        }
        const auto &closingLine = lines[tokens[close].span.line - 1];
        if (!trim(closingLine.text).starts_with(")")) {
            findings.push_back({"FCS1002",
                                "multiline signatures place the closing parenthesis first",
                                tokens[close].span});
        }

        depth = 1;
        auto genericDepth = std::size_t{};
        auto bracketDepth = std::size_t{};
        std::optional<std::size_t> parameterLine;
        std::set<std::size_t> occupiedLines;
        auto invalidParameterLine = false;
        for (auto cursor = open + 1; cursor < close; ++cursor) {
            if (tokens[cursor].kind == TokenKind::LeftParen) {
                ++depth;
                continue;
            }
            if (tokens[cursor].kind == TokenKind::RightParen) {
                --depth;
                continue;
            }
            if (depth != 1) {
                continue;
            }
            if (tokens[cursor].kind == TokenKind::Less) {
                ++genericDepth;
                continue;
            }
            if (tokens[cursor].kind == TokenKind::Greater && genericDepth != 0) {
                --genericDepth;
                continue;
            }
            if (tokens[cursor].kind == TokenKind::LeftBracket) {
                ++bracketDepth;
                continue;
            }
            if (tokens[cursor].kind == TokenKind::RightBracket && bracketDepth != 0) {
                --bracketDepth;
                continue;
            }
            if (tokens[cursor].kind == TokenKind::Comma && genericDepth == 0 &&
                bracketDepth == 0) {
                parameterLine.reset();
                continue;
            }
            if (!parameterLine.has_value()) {
                parameterLine = tokens[cursor].span.line;
                if (*parameterLine == tokens[open].span.line ||
                    *parameterLine == tokens[close].span.line ||
                    !occupiedLines.insert(*parameterLine).second) {
                    invalidParameterLine = true;
                }
            }
        }
        if (invalidParameterLine) {
            findings.push_back({"FCS1002",
                                "multiline signatures place one parameter on each line",
                                tokens[open].span});
        }
        index = close;
    }
}

void findCommentDensity(const ProjectAnalysis &analysis, std::size_t source,
                        std::vector<Finding> &findings) {
    std::size_t comments{};
    std::size_t code{};
    SourceLine last;
    for (const auto &line : sourceLines(analysis.sources[source].contents)) {
        const auto text = trim(line.text);
        if (text.empty()) {
            continue;
        }
        last = line;
        if (text.starts_with("//") || text.starts_with("/*") || text.starts_with("*")) {
            ++comments;
        } else {
            ++code;
        }
    }
    if (comments > 12 && comments > code) {
        findings.push_back({"FCS2002",
                            "comment lines outnumber source lines; keep implementation notes focused",
                            {last.offset, last.text.size(), last.line, 1, source}});
    }
}

void findCommentLabels(const ProjectAnalysis &analysis, std::size_t source,
                       std::vector<Finding> &findings) {
    constexpr std::array labels{
        std::pair{std::string_view{"NOTE"}, std::string_view{"FCS7001"}},
        std::pair{std::string_view{"TODO"}, std::string_view{"FCS7002"}},
        std::pair{std::string_view{"FIXME"}, std::string_view{"FCS7003"}},
        std::pair{std::string_view{"SAFETY"}, std::string_view{"FCS7004"}},
    };
    for (const auto &line : sourceLines(analysis.sources[source].contents)) {
        const auto text = trim(line.text);
        if (!text.starts_with("//")) {
            continue;
        }
        const auto comment = trim(text.substr(2));
        for (const auto &[label, code] : labels) {
            if (comment == label || comment.starts_with(std::string(label) + ":")) {
                findings.push_back({std::string(code), std::string(label) + " comment marker",
                                    {line.offset, line.text.size(), line.line, 1, source}});
                break;
            }
        }
    }
}

bool simpleExpression(const Program &program, AstExpressionId expression) {
    if (expression >= program.expressions.size()) {
        return false;
    }
    const auto &value = program.expressions[expression].value;
    if (const auto *unary = std::get_if<UnaryExpression>(&value)) {
        return (unary->operation == UnaryOperator::Not ||
                unary->operation == UnaryOperator::BitwiseNot ||
                unary->operation == UnaryOperator::Negate) &&
               simpleExpression(program, unary->operand);
    }
    if (const auto *binary = std::get_if<BinaryExpression>(&value)) {
        return simpleExpression(program, binary->left) &&
               simpleExpression(program, binary->right);
    }
    if (const auto *member = std::get_if<MemberExpression>(&value)) {
        return !member->invoked && member->arguments.empty() &&
               (!member->base.has_value() || simpleExpression(program, *member->base));
    }
    return std::holds_alternative<IntegerExpression>(value) ||
           std::holds_alternative<FloatingExpression>(value) ||
           std::holds_alternative<BooleanExpression>(value) ||
           std::holds_alternative<StringExpression>(value) ||
           std::holds_alternative<NameExpression>(value);
}

void findPostfixComplexity(const ProjectAnalysis &analysis,
                           const std::set<std::size_t> &projectSources,
                           std::vector<Finding> &findings) {
    for (const auto &expression : analysis.program.expressions) {
        if (!projectSource(analysis, expression.span.source, projectSources)) {
            continue;
        }
        const auto *conditional = std::get_if<ConditionalExpression>(&expression.value);
        if (conditional == nullptr || !conditional->postfix) {
            continue;
        }
        if (!simpleExpression(analysis.program, conditional->condition) ||
            !simpleExpression(analysis.program, conditional->thenValue) ||
            !simpleExpression(analysis.program, conditional->elseValue)) {
            findings.push_back({"FCS3001",
                                "postfix conditional requires simple condition and value expressions",
                                expression.span});
        }
    }
}

bool resultType(const ProjectAnalysis &analysis, const SemanticModel &semantic,
                AstExpressionId expression) {
    if (expression >= semantic.expressionTypes.size()) {
        return false;
    }
    const auto &type = semantic.expressionTypes[expression];
    return type.kind == TypeKind::Enum && type.declaration < analysis.program.enums.size() &&
           analysis.program.enums[type.declaration].name == "Result";
}

bool discardReason(std::string_view source, SourceSpan span) {
    const auto lines = sourceLines(source);
    if (span.line == 0 || span.line > lines.size()) {
        return false;
    }
    const auto current = trim(lines[span.line - 1].text);
    if (const auto comment = current.find("//"); comment != std::string_view::npos &&
        !trim(current.substr(comment + 2)).empty()) {
        return true;
    }
    if (span.line <= 1) {
        return false;
    }
    const auto previous = trim(lines[span.line - 2].text);
    return previous.starts_with("//") && !trim(previous.substr(2)).empty();
}

void findDiscardReasons(const ProjectAnalysis &analysis, const SemanticModel &semantic,
                        const std::set<std::size_t> &projectSources,
                        std::vector<Finding> &findings) {
    for (const auto &statement : analysis.program.statements) {
        if (!projectSource(analysis, statement.span.source, projectSources)) {
            continue;
        }
        const auto *discarded = std::get_if<DiscardStatement>(&statement.value);
        if (discarded == nullptr || !resultType(analysis, semantic, discarded->value) ||
            statement.span.source >= analysis.sources.size() ||
            discardReason(analysis.sources[statement.span.source].contents, statement.span)) {
            continue;
        }
        findings.push_back({"FCS4001", "discarding a Result requires an adjacent reason comment",
                            statement.span});
    }
}

bool rawType(const TypeSyntax &type) {
    return type.name == "[raw]" || type.name == "[raw-const]" ||
           std::any_of(type.arguments.begin(), type.arguments.end(), rawType);
}

void findPublicUnsafeBoundaries(const ProjectAnalysis &analysis,
                                const std::set<std::size_t> &projectSources,
                                std::vector<Finding> &findings) {
    for (const auto &function : analysis.program.functions) {
        if (!projectSource(analysis, function.span.source, projectSources)) {
            continue;
        }
        const auto rawBoundary = rawType(function.returnType) ||
                                 std::any_of(function.parameters.begin(), function.parameters.end(),
                                             [](const auto &parameter) { return rawType(parameter.type); });
        if (!function.exported || !rawBoundary || function.span.source >= analysis.sources.size()) {
            continue;
        }
        const auto documentation = documentationBefore(
            analysis.sources[function.span.source].contents, function.span.offset);
        if (documentation.find("SAFETY") == std::string::npos) {
            findings.push_back({"FCS5001",
                                "public raw-pointer boundary requires documentation with a SAFETY contract",
                                function.span});
        }
    }
}

std::size_t blockComplexity(const Program &program, AstBlockId block, std::size_t depth,
                            std::size_t &maximumDepth) {
    if (block >= program.blocks.size()) {
        return 0;
    }
    maximumDepth = std::max(maximumDepth, depth);
    auto result = std::size_t{};
    for (const auto statementId : program.blocks[block].statements) {
        if (statementId >= program.statements.size()) {
            continue;
        }
        const auto &statement = program.statements[statementId].value;
        if (const auto *branch = std::get_if<IfStatement>(&statement)) {
            result += 1 + blockComplexity(program, branch->thenBlock, depth + 1, maximumDepth);
            if (branch->elseBlock.has_value()) {
                result += blockComplexity(program, *branch->elseBlock, depth + 1, maximumDepth);
            }
        } else if (const auto *whileLoop = std::get_if<WhileStatement>(&statement)) {
            result += 1 + blockComplexity(program, whileLoop->body, depth + 1, maximumDepth);
        } else if (const auto *forLoop = std::get_if<ForStatement>(&statement)) {
            result += 1 + blockComplexity(program, forLoop->body, depth + 1, maximumDepth);
        } else if (const auto *resultElse = std::get_if<ResultElseStatement>(&statement)) {
            result += 1 + blockComplexity(program, resultElse->elseBlock, depth + 1, maximumDepth);
        } else if (const auto *unsafe = std::get_if<UnsafeStatement>(&statement)) {
            result += blockComplexity(program, unsafe->body, depth + 1, maximumDepth);
        } else if (const auto *select = std::get_if<SelectStatement>(&statement)) {
            result += select->operations.size();
            for (const auto &operation : select->operations) {
                result += blockComplexity(program, operation.body, depth + 1, maximumDepth);
            }
            result += blockComplexity(program, select->errorBlock, depth + 1, maximumDepth);
        }
    }
    return result;
}

void findComplexity(const ProjectAnalysis &analysis, CodeStandardProfile profile,
                    const std::set<std::size_t> &projectSources,
                    std::vector<Finding> &findings) {
    const auto limit = profile == CodeStandardProfile::Strict ? 7U : 12U;
    for (const auto &function : analysis.program.functions) {
        if (!function.hasBody ||
            !projectSource(analysis, function.span.source, projectSources)) {
            continue;
        }
        auto depth = std::size_t{};
        const auto complexity = blockComplexity(analysis.program, function.body, 1, depth);
        if (complexity > limit || depth > 4) {
            findings.push_back({"FCS6001",
                                "function control-flow complexity exceeds the active FCS threshold",
                                function.span});
        }
    }
}

bool exportedSymbol(const ProjectAnalysis &analysis, const LanguageSymbol &symbol,
                    const std::set<std::string> &exportedTypes,
                    const std::set<std::string> &contracts) {
    switch (symbol.id.kind) {
    case LanguageSymbolKind::Struct:
        return symbol.id.owner < analysis.program.structs.size() && analysis.program.structs[symbol.id.owner].exported;
    case LanguageSymbolKind::Field:
        return symbol.id.owner < analysis.program.structs.size() && symbol.id.member < analysis.program.structs[symbol.id.owner].fields.size() && analysis.program.structs[symbol.id.owner].exported && analysis.program.structs[symbol.id.owner].fields[symbol.id.member].exported;
    case LanguageSymbolKind::Enum:
        return symbol.id.owner < analysis.program.enums.size() && !analysis.program.enums[symbol.id.owner].generated && analysis.program.enums[symbol.id.owner].exported;
    case LanguageSymbolKind::EnumVariant:
        return symbol.id.owner < analysis.program.enums.size() && symbol.id.member < analysis.program.enums[symbol.id.owner].variants.size() && !analysis.program.enums[symbol.id.owner].generated && analysis.program.enums[symbol.id.owner].exported && analysis.program.enums[symbol.id.owner].variants[symbol.id.member].exported;
    case LanguageSymbolKind::Contract:
        return symbol.id.owner < analysis.program.contracts.size() && analysis.program.contracts[symbol.id.owner].exported;
    case LanguageSymbolKind::ContractMethod:
        return symbol.id.owner < analysis.program.contracts.size() && symbol.id.member < analysis.program.contracts[symbol.id.owner].methods.size() && analysis.program.contracts[symbol.id.owner].exported && analysis.program.contracts[symbol.id.owner].methods[symbol.id.member].exported;
    case LanguageSymbolKind::Attribute:
        return symbol.id.owner < analysis.program.attributeDeclarations.size() && analysis.program.attributeDeclarations[symbol.id.owner].exported;
    case LanguageSymbolKind::Function:
        return symbol.id.owner < analysis.program.functions.size() && analysis.program.functions[symbol.id.owner].exported && analysis.program.functions[symbol.id.owner].ownerType.empty();
    case LanguageSymbolKind::Method: {
        if (symbol.id.owner >= analysis.program.functions.size()) return false;
        const auto &function = analysis.program.functions[symbol.id.owner];
        return function.exported && exportedTypes.contains(typeKey(function.packageName, function.ownerType)) && !contracts.contains(typeKey(function.packageName, function.ownerType));
    }
    case LanguageSymbolKind::EnumPayload:
    case LanguageSymbolKind::Parameter:
    case LanguageSymbolKind::Local:
        return false;
    }
    return false;
}

std::string_view symbolKind(LanguageSymbolKind kind) {
    switch (kind) {
    case LanguageSymbolKind::Struct: return "type";
    case LanguageSymbolKind::Field: return "field";
    case LanguageSymbolKind::Enum: return "enum";
    case LanguageSymbolKind::EnumVariant: return "variant";
    case LanguageSymbolKind::Contract: return "contract";
    case LanguageSymbolKind::ContractMethod:
    case LanguageSymbolKind::Method: return "method";
    case LanguageSymbolKind::Attribute: return "attribute";
    case LanguageSymbolKind::Function: return "function";
    case LanguageSymbolKind::EnumPayload:
    case LanguageSymbolKind::Parameter:
    case LanguageSymbolKind::Local: return "symbol";
    }
    return "symbol";
}

void findExportedDocumentation(const ProjectAnalysis &analysis,
                               const std::set<std::size_t> &projectSources,
                               std::vector<Finding> &findings) {
    const auto index = buildLanguageIndex(analysis);
    std::set<std::string> exportedTypes;
    std::set<std::string> contracts;
    for (const auto &declaration : analysis.program.structs) if (declaration.exported) exportedTypes.insert(typeKey(declaration.packageName, declaration.name));
    for (const auto &declaration : analysis.program.enums) if (declaration.exported) exportedTypes.insert(typeKey(declaration.packageName, declaration.name));
    for (const auto &declaration : analysis.program.contracts) {
        const auto key = typeKey(declaration.packageName, declaration.name);
        contracts.insert(key);
        if (declaration.exported) exportedTypes.insert(key);
    }
    for (const auto &symbol : index.symbols()) {
        const auto generatedStateTimeout = (symbol.id.kind == LanguageSymbolKind::Function || symbol.id.kind == LanguageSymbolKind::Method) && symbol.id.owner < analysis.program.functions.size() && analysis.program.functions[symbol.id.owner].stateTimeout.has_value();
        if (!projectSource(analysis, symbol.definition.source, projectSources) || !exportedSymbol(analysis, symbol, exportedTypes, contracts) || !symbol.documentation.empty() || generatedStateTimeout) continue;
        findings.push_back({"FCS2001", "strict profile requires documentation for exported " + std::string(symbolKind(symbol.id.kind)) + " " + symbol.name, symbol.definition});
    }
}

std::optional<CodeStandardSeverity> configuredSeverity(
    std::string_view code, const std::vector<CodeStandardRuleSetting> &settings) {
    for (auto iterator = settings.rbegin(); iterator != settings.rend(); ++iterator) {
        if (iterator->code == code) {
            return iterator->severity;
        }
    }
    return std::nullopt;
}

bool enabled(std::string_view code, CodeStandardProfile profile,
             const std::vector<CodeStandardRuleSetting> &settings) {
    if (code == "FCS9001") return true;
    if (profile == CodeStandardProfile::Valid) return false;
    if (configuredSeverity(code, settings).has_value()) return true;
    if (code == "FCS7001" || code == "FCS7002" || code == "FCS7003" ||
        code == "FCS7004") {
        return false;
    }
    if (code == "FCS2001" || code == "FCS2002" || code == "FCS4001" || code == "FCS5001") return profile == CodeStandardProfile::Strict;
    return true;
}

} // namespace

Diagnostics lintProject(const ProjectAnalysis &analysis, CodeStandardProfile profile,
                        const std::vector<std::size_t> &projectSourceList,
                        const std::vector<CodeStandardRuleSetting> &settings) {
    Diagnostics diagnostics;
    const std::set<std::size_t> projectSources(projectSourceList.begin(), projectSourceList.end());
    std::vector<Finding> findings;
    std::map<std::size_t, Suppressions> suppressions;
    for (std::size_t source = 0; source < analysis.sources.size(); ++source) {
        if (!projectSource(analysis, source, projectSources)) continue;
        suppressions.emplace(source, collectSuppressions(analysis.sources[source].contents, source, findings));
        if (profile == CodeStandardProfile::Valid) continue;
        findLongLines(analysis, source, codeStandardWidth(profile), findings);
        findSignatureLayout(analysis, source, findings);
        findCommentDensity(analysis, source, findings);
        findCommentLabels(analysis, source, findings);
    }
    if (profile != CodeStandardProfile::Valid) {
        findPostfixComplexity(analysis, projectSources, findings);
        if (analysis.semantic.has_value()) {
            findDiscardReasons(analysis, *analysis.semantic, projectSources, findings);
        }
        findPublicUnsafeBoundaries(analysis, projectSources, findings);
        findComplexity(analysis, profile, projectSources, findings);
        findExportedDocumentation(analysis, projectSources, findings);
    }
    std::sort(findings.begin(), findings.end(), [](const auto &left, const auto &right) {
        return std::tie(left.span.source, left.span.offset, left.code) < std::tie(right.span.source, right.span.offset, right.code);
    });
    for (auto &finding : findings) {
        if (!enabled(finding.code, profile, settings) ||
            (finding.code != "FCS9001" && suppressions.contains(finding.span.source) &&
             suppressed(suppressions.at(finding.span.source), finding))) {
            continue;
        }
        const auto severity = configuredSeverity(finding.code, settings)
                                  .value_or(CodeStandardSeverity::Warning);
        if (severity == CodeStandardSeverity::Off) continue;
        if (severity == CodeStandardSeverity::Error) diagnostics.error(std::move(finding.code), std::move(finding.message), finding.span);
        else diagnostics.warning(std::move(finding.code), std::move(finding.message), finding.span);
    }
    return diagnostics;
}

} // namespace foundation
