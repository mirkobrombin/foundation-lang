#include "foundation/lexer.hpp"

#include <cctype>
#include <string>
#include <utility>

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
    case TokenKind::Struct:
        return "struct";
    case TokenKind::Enum:
        return "enum";
    case TokenKind::Fn:
        return "fn";
    case TokenKind::Let:
        return "let";
    case TokenKind::Var:
        return "var";
    case TokenKind::Return:
        return "return";
    case TokenKind::Discard:
        return "discard";
    case TokenKind::If:
        return "if";
    case TokenKind::Else:
        return "else";
    case TokenKind::While:
        return "while";
    case TokenKind::Match:
        return "match";
    case TokenKind::Own:
        return "own";
    case TokenKind::View:
        return "view";
    case TokenKind::Edit:
        return "edit";
    case TokenKind::True:
        return "true";
    case TokenKind::False:
        return "false";
    case TokenKind::LeftParen:
        return "(";
    case TokenKind::RightParen:
        return ")";
    case TokenKind::LeftBrace:
        return "{";
    case TokenKind::RightBrace:
        return "}";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Colon:
        return ":";
    case TokenKind::Dot:
        return ".";
    case TokenKind::Equal:
        return "=";
    case TokenKind::EqualEqual:
        return "==";
    case TokenKind::Bang:
        return "!";
    case TokenKind::BangEqual:
        return "!=";
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";
    case TokenKind::Percent:
        return "%";
    case TokenKind::Less:
        return "<";
    case TokenKind::LessEqual:
        return "<=";
    case TokenKind::Greater:
        return ">";
    case TokenKind::GreaterEqual:
        return ">=";
    case TokenKind::AndAnd:
        return "&&";
    case TokenKind::OrOr:
        return "||";
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

        const auto simple = [this, start, line, column](TokenKind kind, std::string text) {
            return Token{kind, std::move(text), spanFrom(start, line, column)};
        };
        switch (value) {
        case '(':
            return simple(TokenKind::LeftParen, "(");
        case ')':
            return simple(TokenKind::RightParen, ")");
        case '{':
            return simple(TokenKind::LeftBrace, "{");
        case '}':
            return simple(TokenKind::RightBrace, "}");
        case ',':
            return simple(TokenKind::Comma, ",");
        case ':':
            return simple(TokenKind::Colon, ":");
        case '.':
            return simple(TokenKind::Dot, ".");
        case '+':
            return simple(TokenKind::Plus, "+");
        case '*':
            return simple(TokenKind::Star, "*");
        case '/':
            return simple(TokenKind::Slash, "/");
        case '%':
            return simple(TokenKind::Percent, "%");
        case '-':
            return simple(TokenKind::Minus, "-");
        case '=':
            if (peek() == '=') {
                advance();
                return simple(TokenKind::EqualEqual, "==");
            }
            return simple(TokenKind::Equal, "=");
        case '!':
            if (peek() == '=') {
                advance();
                return simple(TokenKind::BangEqual, "!=");
            }
            return simple(TokenKind::Bang, "!");
        case '<':
            if (peek() == '=') {
                advance();
                return simple(TokenKind::LessEqual, "<=");
            }
            return simple(TokenKind::Less, "<");
        case '>':
            if (peek() == '=') {
                advance();
                return simple(TokenKind::GreaterEqual, ">=");
            }
            return simple(TokenKind::Greater, ">");
        case '&':
            if (peek() == '&') {
                advance();
                return simple(TokenKind::AndAnd, "&&");
            }
            break;
        case '|':
            if (peek() == '|') {
                advance();
                return simple(TokenKind::OrOr, "||");
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
    if (text == "struct") {
        kind = TokenKind::Struct;
    } else if (text == "enum") {
        kind = TokenKind::Enum;
    } else if (text == "fn") {
        kind = TokenKind::Fn;
    } else if (text == "let") {
        kind = TokenKind::Let;
    } else if (text == "var") {
        kind = TokenKind::Var;
    } else if (text == "return") {
        kind = TokenKind::Return;
    } else if (text == "discard") {
        kind = TokenKind::Discard;
    } else if (text == "if") {
        kind = TokenKind::If;
    } else if (text == "else") {
        kind = TokenKind::Else;
    } else if (text == "while") {
        kind = TokenKind::While;
    } else if (text == "match") {
        kind = TokenKind::Match;
    } else if (text == "own") {
        kind = TokenKind::Own;
    } else if (text == "view") {
        kind = TokenKind::View;
    } else if (text == "edit") {
        kind = TokenKind::Edit;
    } else if (text == "true") {
        kind = TokenKind::True;
    } else if (text == "false") {
        kind = TokenKind::False;
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
        if (current == '\0') {
            diagnostics_.error("FDN0004", "NUL is not allowed in a string literal",
                               {offset_ - 1, 1, line_, column_ - 1});
            continue;
        }
        if (current != '\\') {
            value.push_back(current);
            continue;
        }

        if (atEnd()) {
            break;
        }
        const auto escapeStart = offset_ - 1;
        const auto escapeLine = line_;
        const auto escapeColumn = column_ - 1;
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
                               {escapeStart, 2, escapeLine, escapeColumn});
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
