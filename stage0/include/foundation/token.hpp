#ifndef FOUNDATION_TOKEN_HPP
#define FOUNDATION_TOKEN_HPP

#include "foundation/diagnostic.hpp"

#include <string>

namespace foundation {

enum class TokenKind {
    Eof,
    Identifier,
    Integer,
    String,
    Fn,
    Print,
    Return,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Arrow,
    Semicolon,
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    std::string text;
    SourceSpan span;
};

[[nodiscard]] const char *tokenName(TokenKind kind);

} // namespace foundation

#endif
