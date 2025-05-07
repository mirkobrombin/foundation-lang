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
    Struct,
    Enum,
    Fn,
    Let,
    Var,
    Return,
    If,
    Else,
    While,
    Match,
    True,
    False,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Arrow,
    Semicolon,
    Comma,
    Colon,
    ColonColon,
    Dot,
    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    FatArrow,
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    std::string text;
    SourceSpan span;
};

[[nodiscard]] const char *tokenName(TokenKind kind);

} // namespace foundation

#endif
