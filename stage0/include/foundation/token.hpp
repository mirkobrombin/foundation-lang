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
    Package,
    Import,
    As,
    Extern,
    Struct,
    Methods,
    Enum,
    Contract,
    Attribute,
    Implements,
    Extends,
    By,
    Fn,
    Task,
    Spawn,
    Let,
    Const,
    Var,
    Return,
    Discard,
    If,
    Else,
    While,
    Match,
    Capture,
    Replace,
    With,
    Own,
    View,
    Edit,
    True,
    False,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Colon,
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
    At,
    Dollar,
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    std::string text;
    SourceSpan span;
};

[[nodiscard]] const char *tokenName(TokenKind kind);

} // namespace foundation

#endif
