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

bool isContinuationByte(unsigned char value) { return value >= 0x80 && value <= 0xbf; }

bool isValidUtf8(std::string_view value) {
    std::size_t offset{};
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first <= 0x7f) {
            ++offset;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            if (offset + 1 >= value.size() ||
                !isContinuationByte(static_cast<unsigned char>(value[offset + 1]))) {
                return false;
            }
            offset += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (offset + 2 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1]);
            const auto third = static_cast<unsigned char>(value[offset + 2]);
            const auto secondValid = first == 0xe0   ? second >= 0xa0 && second <= 0xbf
                                     : first == 0xed ? second >= 0x80 && second <= 0x9f
                                                     : isContinuationByte(second);
            if (!secondValid || !isContinuationByte(third)) {
                return false;
            }
            offset += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (offset + 3 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1]);
            const auto third = static_cast<unsigned char>(value[offset + 2]);
            const auto fourth = static_cast<unsigned char>(value[offset + 3]);
            const auto secondValid = first == 0xf0   ? second >= 0x90 && second <= 0xbf
                                     : first == 0xf4 ? second >= 0x80 && second <= 0x8f
                                                     : isContinuationByte(second);
            if (!secondValid || !isContinuationByte(third) || !isContinuationByte(fourth)) {
                return false;
            }
            offset += 4;
            continue;
        }
        return false;
    }
    return true;
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
    case TokenKind::Package:
        return "package";
    case TokenKind::Import:
        return "import";
    case TokenKind::As:
        return "as";
    case TokenKind::Extern:
        return "extern";
    case TokenKind::Struct:
        return "struct";
    case TokenKind::Methods:
        return "methods";
    case TokenKind::Enum:
        return "enum";
    case TokenKind::Contract:
        return "contract";
    case TokenKind::Attribute:
        return "attribute";
    case TokenKind::Implements:
        return "implements";
    case TokenKind::Extends:
        return "extends";
    case TokenKind::Delegate:
        return "delegate";
    case TokenKind::Fn:
        return "fn";
    case TokenKind::Task:
        return "task";
    case TokenKind::Spawn:
        return "spawn";
    case TokenKind::Let:
        return "let";
    case TokenKind::Const:
        return "const";
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
    case TokenKind::Select:
        return "select";
    case TokenKind::Timeout:
        return "timeout";
    case TokenKind::Match:
        return "match";
    case TokenKind::Capture:
        return "capture";
    case TokenKind::Replace:
        return "replace";
    case TokenKind::With:
        return "with";
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
    case TokenKind::LeftBracket:
        return "[";
    case TokenKind::RightBracket:
        return "]";
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
    case TokenKind::At:
        return "@";
    case TokenKind::Dollar:
        return "$";
    }
    return "token";
}

Lexer::Lexer(std::string_view source, Diagnostics &diagnostics, std::size_t sourceId)
    : source_(source), diagnostics_(diagnostics), sourceId_(sourceId) {}

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
        if (value == '/' && peek(1) == '*') {
            const auto start = offset_;
            const auto line = line_;
            const auto column = column_;
            advance();
            advance();
            std::size_t depth{1};
            while (!atEnd() && depth != 0) {
                if (peek() == '/' && peek(1) == '*') {
                    advance();
                    advance();
                    ++depth;
                } else if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    --depth;
                } else {
                    advance();
                }
            }
            if (depth != 0) {
                diagnostics_.error("FDN0006", "unterminated block comment",
                                   spanFrom(start, line, column));
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
            return {TokenKind::Eof, {}, {offset_, 0, line_, column_, sourceId_}};
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
        case '[':
            return simple(TokenKind::LeftBracket, "[");
        case ']':
            return simple(TokenKind::RightBracket, "]");
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
        case '@':
            return simple(TokenKind::At, "@");
        case '$':
            return simple(TokenKind::Dollar, "$");
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
    if (text == "package") {
        kind = TokenKind::Package;
    } else if (text == "import") {
        kind = TokenKind::Import;
    } else if (text == "as") {
        kind = TokenKind::As;
    } else if (text == "extern") {
        kind = TokenKind::Extern;
    } else if (text == "struct") {
        kind = TokenKind::Struct;
    } else if (text == "methods") {
        kind = TokenKind::Methods;
    } else if (text == "enum") {
        kind = TokenKind::Enum;
    } else if (text == "contract") {
        kind = TokenKind::Contract;
    } else if (text == "attribute") {
        kind = TokenKind::Attribute;
    } else if (text == "implements") {
        kind = TokenKind::Implements;
    } else if (text == "extends") {
        kind = TokenKind::Extends;
    } else if (text == "delegate") {
        kind = TokenKind::Delegate;
    } else if (text == "fn") {
        kind = TokenKind::Fn;
    } else if (text == "task") {
        kind = TokenKind::Task;
    } else if (text == "spawn") {
        kind = TokenKind::Spawn;
    } else if (text == "let") {
        kind = TokenKind::Let;
    } else if (text == "const") {
        kind = TokenKind::Const;
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
    } else if (text == "select") {
        kind = TokenKind::Select;
    } else if (text == "timeout") {
        kind = TokenKind::Timeout;
    } else if (text == "match") {
        kind = TokenKind::Match;
    } else if (text == "capture") {
        kind = TokenKind::Capture;
    } else if (text == "replace") {
        kind = TokenKind::Replace;
    } else if (text == "with") {
        kind = TokenKind::With;
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
                               {offset_ - 1, 1, line_, column_ - 1, sourceId_});
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
        case '0':
            value.push_back('\0');
            break;
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        default:
            diagnostics_.error("FDN0003", "invalid string escape",
                               {escapeStart, 2, escapeLine, escapeColumn, sourceId_});
            value.push_back(escaped);
            break;
        }
    }

    if (atEnd()) {
        diagnostics_.error("FDN0002", "unterminated string", spanFrom(start, line, column));
    } else {
        advance();
    }
    if (!isValidUtf8(value)) {
        diagnostics_.error("FDN0005", "string literal is not valid UTF-8",
                           spanFrom(start, line, column));
    }
    return {TokenKind::String, std::move(value), spanFrom(start, line, column)};
}

SourceSpan Lexer::spanFrom(std::size_t offset, std::size_t line, std::size_t column) const {
    return {offset, offset_ - offset, line, column, sourceId_};
}

} // namespace foundation
