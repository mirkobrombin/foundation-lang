#include "foundation/formatter.hpp"

#include "foundation/lexer.hpp"
#include "foundation/parser.hpp"
#include "foundation/token.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace foundation {

namespace {

bool closes(TokenKind kind) {
    return kind == TokenKind::RightBrace || kind == TokenKind::RightParen ||
           kind == TokenKind::RightBracket;
}

bool endsExpression(TokenKind kind) {
    return kind == TokenKind::Identifier || kind == TokenKind::Integer ||
           kind == TokenKind::Floating ||
           kind == TokenKind::String || kind == TokenKind::True ||
           kind == TokenKind::False || closes(kind);
}

bool binary(TokenKind kind) {
    return kind == TokenKind::Equal || kind == TokenKind::EqualEqual ||
           kind == TokenKind::BangEqual || kind == TokenKind::Plus ||
           kind == TokenKind::Star || kind == TokenKind::Slash ||
           kind == TokenKind::Percent || kind == TokenKind::LessEqual ||
           kind == TokenKind::GreaterEqual || kind == TokenKind::AndAnd ||
           kind == TokenKind::OrOr;
}

bool unaryMinus(const std::vector<Token> &tokens, std::size_t index) {
    return tokens[index].kind == TokenKind::Minus &&
           (index == 0 || !endsExpression(tokens[index - 1].kind));
}

bool unaryStar(const std::vector<Token> &tokens, std::size_t index,
               const std::unordered_set<std::size_t> &typeStarts) {
    return tokens[index].kind == TokenKind::Star &&
           (typeStarts.contains(tokens[index].span.offset) || index == 0 ||
            tokens[index - 1].span.line != tokens[index].span.line ||
            !endsExpression(tokens[index - 1].kind));
}

bool comparisonDelimiter(TokenKind kind) {
    return kind == TokenKind::Less || kind == TokenKind::Greater;
}

bool continuesExpression(TokenKind kind, bool genericDelimiter) {
    return binary(kind) || kind == TokenKind::Minus ||
           (comparisonDelimiter(kind) && !genericDelimiter);
}

void collectTypeStarts(const TypeSyntax &type, std::unordered_set<std::size_t> &starts) {
    starts.insert(type.span.offset);
    for (const auto &argument : type.arguments) {
        collectTypeStarts(argument, starts);
    }
}

std::unordered_set<std::size_t> typeStarts(const Program &program) {
    std::unordered_set<std::size_t> starts;
    const auto collectParameters = [&starts](const auto &parameters) {
        for (const auto &parameter : parameters) {
            collectTypeStarts(parameter.type, starts);
        }
    };
    for (const auto &structure : program.structs) {
        for (const auto &implementation : structure.implementations) {
            collectTypeStarts(implementation.contract, starts);
        }
        for (const auto &field : structure.fields) {
            collectTypeStarts(field.type, starts);
        }
    }
    for (const auto &enumeration : program.enums) {
        for (const auto &variant : enumeration.variants) {
            if (variant.payloadType.has_value()) {
                collectTypeStarts(*variant.payloadType, starts);
            }
        }
    }
    for (const auto &contract : program.contracts) {
        for (const auto &parent : contract.parents) {
            collectTypeStarts(parent, starts);
        }
        for (const auto &method : contract.methods) {
            collectParameters(method.parameters);
            collectTypeStarts(method.returnType, starts);
        }
    }
    for (const auto &attribute : program.attributeDeclarations) {
        collectParameters(attribute.parameters);
    }
    for (const auto &function : program.functions) {
        collectParameters(function.parameters);
        collectTypeStarts(function.returnType, starts);
        if (function.workflow.has_value()) {
            collectTypeStarts(function.workflow->successType, starts);
            collectTypeStarts(function.workflow->errorType, starts);
        }
    }
    for (const auto &statement : program.statements) {
        if (const auto *variable = std::get_if<VariableStatement>(&statement.value);
            variable != nullptr && variable->type.has_value()) {
            collectTypeStarts(*variable->type, starts);
        } else if (const auto *destructure =
                       std::get_if<StructDestructureStatement>(&statement.value)) {
            collectTypeStarts(destructure->type, starts);
        }
    }
    for (const auto &expression : program.expressions) {
        if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            for (const auto &argument : name->typeArguments) {
                collectTypeStarts(argument, starts);
            }
        } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
            for (const auto &argument : call->typeArguments) {
                collectTypeStarts(argument, starts);
            }
        } else if (const auto *structure = std::get_if<StructExpression>(&expression.value)) {
            collectTypeStarts(structure->type, starts);
        } else if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
            for (const auto &argument : member->typeArguments) {
                collectTypeStarts(argument, starts);
            }
        }
    }

    return starts;
}

std::unordered_set<std::size_t> closureBlockStarts(const Program &program) {
    std::unordered_set<std::size_t> starts;
    for (const auto &function : program.functions) {
        if (function.closure && function.body < program.blocks.size()) {
            starts.insert(program.blocks[function.body].span.offset);
        }
    }
    return starts;
}

std::vector<bool> typeOpeningBrackets(const std::vector<Token> &tokens,
                                      const std::unordered_set<std::size_t> &starts) {
    std::vector<bool> result(tokens.size());
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        result[index] = tokens[index].kind == TokenKind::LeftBracket &&
                        starts.contains(tokens[index].span.offset);
    }
    return result;
}

bool spaceBetween(const std::vector<Token> &tokens, std::size_t index,
                  const std::vector<bool> &genericDelimiters,
                  const std::vector<bool> &typeBrackets,
                  const std::unordered_set<std::size_t> &typeStarts) {
    const auto previous = tokens[index - 1].kind;
    const auto current = tokens[index].kind;
    if (current == TokenKind::RightParen || current == TokenKind::RightBracket ||
        current == TokenKind::Comma || current == TokenKind::Colon) {
        return false;
    }
    if (current == TokenKind::Dot) {
        if (previous == TokenKind::Greater) {
            return !genericDelimiters[index - 1];
        }
        return !endsExpression(previous) && previous != TokenKind::LeftParen &&
               previous != TokenKind::LeftBracket && previous != TokenKind::LeftBrace &&
               previous != TokenKind::At && previous != TokenKind::Dot;
    }
    if (previous == TokenKind::LeftParen || previous == TokenKind::LeftBracket ||
        previous == TokenKind::At || previous == TokenKind::Dot ||
        previous == TokenKind::Bang || previous == TokenKind::Ampersand ||
        previous == TokenKind::Dollar ||
        unaryMinus(tokens, index - 1) || unaryStar(tokens, index - 1, typeStarts)) {
        return false;
    }
    if (current == TokenKind::LeftParen) {
        if (previous == TokenKind::Greater) {
            return !genericDelimiters[index - 1];
        }
        return previous != TokenKind::Identifier && previous != TokenKind::Fn &&
               previous != TokenKind::RightParen && previous != TokenKind::RightBracket;
    }
    if (current == TokenKind::LeftBracket) {
        if (typeBrackets[index]) {
            return previous != TokenKind::LeftParen && previous != TokenKind::LeftBracket;
        }
        return !endsExpression(previous) && previous != TokenKind::LeftParen &&
               previous != TokenKind::LeftBracket;
    }
    if (previous == TokenKind::RightBracket && current == TokenKind::Identifier) {
        return false;
    }
    if (current == TokenKind::LeftBrace) {
        return true;
    }
    if (comparisonDelimiter(previous)) {
        if (previous == TokenKind::Greater && genericDelimiters[index - 1]) {
            return current != TokenKind::Greater || !genericDelimiters[index];
        }
        return !genericDelimiters[index - 1];
    }
    if (comparisonDelimiter(current)) {
        return !genericDelimiters[index];
    }
    if (current == TokenKind::Bang || unaryMinus(tokens, index)) {
        return true;
    }
    if (previous == TokenKind::Comma || previous == TokenKind::Colon ||
        previous == TokenKind::LeftBrace || current == TokenKind::RightBrace ||
        current == TokenKind::LeftBrace || binary(previous) || binary(current) ||
        previous == TokenKind::Minus || current == TokenKind::Minus) {
        return previous != TokenKind::LeftBrace || current != TokenKind::RightBrace;
    }
    return true;
}

std::vector<bool> genericDelimiters(const std::vector<Token> &tokens) {
    std::vector<bool> result(tokens.size());
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].kind != TokenKind::Less) {
            continue;
        }
        const auto closing = typeArgumentListClosingToken(tokens, index);
        if (!closing.has_value() || *closing + 1 >= tokens.size()) {
            continue;
        }
        const auto &close = tokens[*closing];
        const auto &next = tokens[*closing + 1];
        const auto continues = next.span.line == close.span.line;
        const auto accepted = !continues || next.kind == TokenKind::Eof ||
                              next.kind == TokenKind::Dot ||
                              next.kind == TokenKind::LeftBrace ||
                              next.kind == TokenKind::LeftParen ||
                              next.kind == TokenKind::Comma ||
                              next.kind == TokenKind::RightParen ||
                              next.kind == TokenKind::RightBracket ||
                              next.kind == TokenKind::RightBrace ||
                              next.kind == TokenKind::Equal ||
                              next.kind == TokenKind::Capture ||
                              next.kind == TokenKind::Implements ||
                              next.kind == TokenKind::Extends;
        if (accepted) {
            for (auto current = index; current <= *closing; ++current) {
                if (comparisonDelimiter(tokens[current].kind)) {
                    result[current] = true;
                }
            }
        }
    }
    return result;
}

class Writer {
  public:
    explicit Writer(std::string_view source) : source_(source) {}

    [[nodiscard]] std::string format(const std::vector<Token> &tokens, const Program &program) {
        const auto angleDelimiters = genericDelimiters(tokens);
        const auto starts = typeStarts(program);
        const auto closureBlocks = closureBlockStarts(program);
        const auto typeBrackets = typeOpeningBrackets(tokens, starts);
        for (const auto &function : program.functions) {
            if (!function.workflow.has_value()) {
                continue;
            }
            for (const auto &step : function.workflow->steps) {
                if (step.compensationSpan.has_value()) {
                    compensationLines_.insert(step.compensationSpan->line);
                }
            }
        }
        std::size_t previousEnd{};
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto &token = tokens[index];
            if (token.kind == TokenKind::Eof) {
                trivia(source_.substr(previousEnd));
                break;
            }
            const auto gap = source_.substr(previousEnd, token.span.offset - previousEnd);
            trivia(gap);
            if (lineStart_) {
                indent(token.kind, token.kind == TokenKind::Identifier &&
                                       token.text == "compensate" &&
                                       compensationLines_.contains(token.span.line));
            } else if (index != 0 && gap.find("//") == std::string_view::npos &&
                       gap.find_first_of("\r\n") == std::string_view::npos &&
                       spaceBetween(tokens, index, angleDelimiters, typeBrackets, starts)) {
                output_.push_back(' ');
            }
            const auto raw = source_.substr(token.span.offset, token.span.length);
            output_.append(raw);
            lineStart_ = !raw.empty() && (raw.back() == '\n' || raw.back() == '\r');
            updateDepth(token.kind, angleDelimiters[index],
                        closureBlocks.contains(token.span.offset));
            previousEnd = token.span.offset + token.span.length;
        }
        return output_;
    }

  private:
    void trimLine() {
        while (!output_.empty() && (output_.back() == ' ' || output_.back() == '\t')) {
            output_.pop_back();
        }
    }

    void newline() {
        const auto continuation =
            !lineStart_ && continuesExpression(lastToken_, lastTokenWasGenericDelimiter_);
        trimLine();
        output_.push_back('\n');
        lineStart_ = true;
        continuation_ = continuation;
    }

    void writeComment(std::string_view comment) {
        while (!comment.empty() && (comment.back() == ' ' || comment.back() == '\t')) {
            comment.remove_suffix(1);
        }
        if (lineStart_) {
            indent(TokenKind::Eof, false);
        } else {
            trimLine();
            output_.push_back(' ');
        }
        output_.append(comment);
        lineStart_ = false;
    }

    void writeBlockComment(std::string_view comment) {
        if (lineStart_) {
            indent(TokenKind::Eof, false);
        } else {
            trimLine();
            output_.push_back(' ');
        }
        for (std::size_t offset = 0; offset < comment.size(); ++offset) {
            if (comment[offset] == '\r') {
                if (offset + 1 < comment.size() && comment[offset + 1] == '\n') {
                    ++offset;
                }
                output_.push_back('\n');
            } else {
                output_.push_back(comment[offset]);
            }
        }
        lineStart_ = !output_.empty() && output_.back() == '\n';
    }

    void trivia(std::string_view value) {
        std::size_t offset{};
        while (offset < value.size()) {
            if (value[offset] == '/' && offset + 1 < value.size() && value[offset + 1] == '/') {
                auto end = offset + 2;
                while (end < value.size() && value[end] != '\r' && value[end] != '\n') {
                    ++end;
                }
                writeComment(value.substr(offset, end - offset));
                offset = end;
                continue;
            }
            if (value[offset] == '/' && offset + 1 < value.size() && value[offset + 1] == '*') {
                auto end = offset + 2;
                std::size_t depth{1};
                while (end < value.size() && depth != 0) {
                    if (end + 1 < value.size() && value[end] == '/' && value[end + 1] == '*') {
                        end += 2;
                        ++depth;
                    } else if (end + 1 < value.size() && value[end] == '*' &&
                               value[end + 1] == '/') {
                        end += 2;
                        --depth;
                    } else {
                        ++end;
                    }
                }
                writeBlockComment(value.substr(offset, end - offset));
                offset = end;
                continue;
            }
            if (value[offset] == '\r') {
                if (offset + 1 < value.size() && value[offset + 1] == '\n') {
                    ++offset;
                }
                newline();
            } else if (value[offset] == '\n') {
                newline();
            }
            ++offset;
        }
    }

    void indent(TokenKind next, bool workflowChild) {
        auto braces = braceDepth_;
        if (next == TokenKind::RightBrace && braces != 0) {
            --braces;
        }
        const auto threshold = next == TokenKind::RightBrace ? braceDepth_ : braces;
        const auto parenCount = parenBraceDepths_.size() -
                                (next == TokenKind::RightParen && !parenBraceDepths_.empty());
        const auto bracketCount = bracketBraceDepths_.size() -
                                  (next == TokenKind::RightBracket &&
                                   !bracketBraceDepths_.empty());
        const auto nestedParens = std::count_if(
            parenBraceDepths_.begin(), parenBraceDepths_.begin() + parenCount,
            [threshold](std::size_t depth) { return depth >= threshold; });
        const auto nestedBrackets = std::count_if(
            bracketBraceDepths_.begin(), bracketBraceDepths_.begin() + bracketCount,
            [threshold](std::size_t depth) { return depth >= threshold; });
        const auto nested = nestedParens != 0 || nestedBrackets != 0;
        const auto nestedFunctions = nestedParens + nestedBrackets > 1
                                         ? std::count(parenFunctions_.begin(),
                                                      parenFunctions_.begin() + parenCount,
                                                      true)
                                         : std::size_t{};
        const auto braceCount = braceClosureExtras_.size();
        const auto closureExtras = std::count(braceClosureExtras_.begin(),
                                              braceClosureExtras_.begin() + braceCount, true);
        output_.append(
            (braces + (nested ? 1 : 0) + nestedFunctions + closureExtras) * 4, ' ');
        if (continuation_ && !nested) {
            output_.append(4, ' ');
        }
        if (workflowChild) {
            output_.append(4, ' ');
        }
        lineStart_ = false;
        continuation_ = false;
    }

    void updateDepth(TokenKind kind, bool genericDelimiter, bool closureBlock) {
        if (kind == TokenKind::LeftBrace) {
            ++braceDepth_;
            braceClosureExtras_.push_back(closureBlock && !parenBraceDepths_.empty());
        } else if (kind == TokenKind::RightBrace && braceDepth_ != 0) {
            --braceDepth_;
            braceClosureExtras_.pop_back();
        } else if (kind == TokenKind::LeftParen) {
            parenBraceDepths_.push_back(braceDepth_);
            parenFunctions_.push_back(lastToken_ == TokenKind::Fn);
        } else if (kind == TokenKind::RightParen && !parenBraceDepths_.empty()) {
            parenBraceDepths_.pop_back();
            parenFunctions_.pop_back();
        } else if (kind == TokenKind::LeftBracket) {
            bracketBraceDepths_.push_back(braceDepth_);
        } else if (kind == TokenKind::RightBracket && !bracketBraceDepths_.empty()) {
            bracketBraceDepths_.pop_back();
        }
        lastToken_ = kind;
        lastTokenWasGenericDelimiter_ = genericDelimiter;
    }

    std::string_view source_;
    std::string output_;
    std::size_t braceDepth_{};
    std::vector<bool> braceClosureExtras_;
    std::vector<std::size_t> parenBraceDepths_;
    std::vector<bool> parenFunctions_;
    std::vector<std::size_t> bracketBraceDepths_;
    std::unordered_set<std::size_t> compensationLines_;
    TokenKind lastToken_{TokenKind::Eof};
    bool lastTokenWasGenericDelimiter_{};
    bool lineStart_{true};
    bool continuation_{};
};

bool sameTokens(const std::vector<Token> &left, const std::vector<Token> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].kind != right[index].kind || left[index].text != right[index].text) {
            return false;
        }
    }
    return true;
}

} // namespace

FormatResult formatSource(std::string_view source, std::size_t sourceId) {
    Diagnostics diagnostics;
    Lexer lexer(source, diagnostics, sourceId);
    const auto tokens = lexer.scan();
    if (diagnostics.hasErrors()) {
        return {std::string(source), std::move(diagnostics)};
    }

    Parser parser(tokens, diagnostics, false);
    const auto program = parser.parse();
    if (diagnostics.hasErrors()) {
        return {std::string(source), std::move(diagnostics)};
    }

    auto contents = Writer(source).format(tokens, program);
    Diagnostics verification;
    Lexer formattedLexer(contents, verification, sourceId);
    const auto formattedTokens = formattedLexer.scan();
    if (!verification.hasErrors()) {
        Parser formattedParser(formattedTokens, verification, false);
        [[maybe_unused]] const auto formattedProgram = formattedParser.parse();
    }
    if (verification.hasErrors() || !sameTokens(tokens, formattedTokens)) {
        diagnostics.error("FDN9996", "formatter did not preserve the token stream",
                          {0, source.empty() ? std::size_t{} : std::size_t{1}, 1, 1, sourceId});
        return {std::string(source), std::move(diagnostics)};
    }
    return {std::move(contents), std::move(diagnostics)};
}

} // namespace foundation
