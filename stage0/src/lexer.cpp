#include "foundation/lexer.hpp"

#include <cctype>
#include <string>

namespace foundation {

namespace {

bool isIdentifierStart(char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalpha(byte) != 0 || value == '_';
}

bool isIdentifierPart(char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_';
}

} // namespace

const char *tokenName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Eof:
        return "end of file";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Integer:
        return "integer";
    case TokenKind::String:
        return "string";
    case TokenKind::Fn:
        return "fn";
    case TokenKind::Print:
        return "print";
    case TokenKind::Return:
        return "return";
    case TokenKind::LeftParen:
        return "(";
    case TokenKind::RightParen:
        return ")";
    case TokenKind::LeftBrace:
        return "{";
    case TokenKind::RightBrace:
        return "}";
    case TokenKind::Arrow:
        return "->";
    case TokenKind::Semicolon:
        return ";";
    }
    return "token";
}

Lexer::Lexer(std::string_view source, Diagnostics &diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::scan() {
    std::vector<Token> tokens;
    while (true) {
        auto token = next();
        tokens.push_back(token);
        if (token.kind == TokenKind::Eof) {
            return tokens;
        }
    }
}

bool Lexer::atEnd() const { return offset_ >= source_.size(); }

char Lexer::peek(std::size_t distance) const {
    const auto index = offset_ + distance;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
    const auto value = source_[offset_++];
    if (value == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return value;
}

void Lexer::skipIgnored() {
    while (!atEnd()) {
        const auto value = peek();
        if (std::isspace(static_cast<unsigned char>(value)) != 0) {
            advance();
            continue;
        }
        if (value == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        return;
    }
}

Token Lexer::next() {
    while (true) {
        skipIgnored();

        const auto start = offset_;
        const auto line = line_;
        const auto column = column_;
        if (atEnd()) {
            return {TokenKind::Eof, {}, {offset_, 0, line_, column_}};
        }

        const auto value = advance();
        if (isIdentifierStart(value)) {
            return identifier();
        }
        if (std::isdigit(static_cast<unsigned char>(value)) != 0) {
            return integer();
        }

        switch (value) {
        case '(':
            return {TokenKind::LeftParen, "(", spanFrom(start, line, column)};
        case ')':
            return {TokenKind::RightParen, ")", spanFrom(start, line, column)};
        case '{':
            return {TokenKind::LeftBrace, "{", spanFrom(start, line, column)};
        case '}':
            return {TokenKind::RightBrace, "}", spanFrom(start, line, column)};
        case ';':
            return {TokenKind::Semicolon, ";", spanFrom(start, line, column)};
        case '-':
            if (peek() == '>') {
                advance();
                return {TokenKind::Arrow, "->", spanFrom(start, line, column)};
            }
            break;
        case '"':
            return string();
        default:
            break;
        }

        diagnostics_.error("FDN0001", "unexpected character", spanFrom(start, line, column));
    }
}

Token Lexer::identifier() {
    const auto start = offset_ - 1;
    const auto column = column_ - 1;
    while (isIdentifierPart(peek())) {
        advance();
    }

    const auto text = std::string(source_.substr(start, offset_ - start));
    auto kind = TokenKind::Identifier;
    if (text == "fn") {
        kind = TokenKind::Fn;
    } else if (text == "print") {
        kind = TokenKind::Print;
    } else if (text == "return") {
        kind = TokenKind::Return;
    }
    return {kind, text, spanFrom(start, line_, column)};
}

Token Lexer::integer() {
    const auto start = offset_ - 1;
    const auto column = column_ - 1;
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }
    return {TokenKind::Integer, std::string(source_.substr(start, offset_ - start)),
            spanFrom(start, line_, column)};
}

Token Lexer::string() {
    const auto start = offset_ - 1;
    const auto line = line_;
    const auto column = column_ - 1;
    std::string value;

    while (!atEnd() && peek() != '"') {
        auto current = advance();
        if (current != '\\') {
            value.push_back(current);
            continue;
        }

        if (atEnd()) {
            break;
        }
        const auto escaped = advance();
        switch (escaped) {
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        default:
            diagnostics_.error("FDN0003", "invalid string escape",
                               {offset_ - 2, 2, line_, column_ - 2});
            value.push_back(escaped);
            break;
        }
    }

    if (atEnd()) {
        diagnostics_.error("FDN0002", "unterminated string", spanFrom(start, line, column));
    } else {
        advance();
    }
    return {TokenKind::String, std::move(value), spanFrom(start, line, column)};
}

SourceSpan Lexer::spanFrom(std::size_t offset, std::size_t line, std::size_t column) const {
    return {offset, offset_ - offset, line, column};
}

} // namespace foundation
