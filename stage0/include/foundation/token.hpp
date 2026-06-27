#ifndef FOUNDATION_TOKEN_HPP
#define FOUNDATION_TOKEN_HPP

#include "foundation/diagnostic.hpp"

#include <string>

namespace foundation {

enum class TokenKind {
    Eof,
    Identifier,
    Integer,
    Floating,
    String,
    Package,
    Import,
    As,
    Extern,
    Struct,
    Service,
    Methods,
    Enum,
    StateMachine,
    Pipeline,
    Saga,
    Contract,
    Attribute,
    Implements,
    Extends,
    Delegate,
    Fn,
    Action,
    Task,
    Test,
    Unsafe,
    Spawn,
    Let,
    Const,
    Var,
    Return,
    Discard,
    If,
    Else,
    While,
    For,
    In,
    Break,
    Continue,
    Select,
    Timeout,
    Match,
    Capture,
    Replace,
    With,
    New,
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
    Ampersand,
    Dollar,
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    std::string text;
    SourceSpan span;
    bool leadingSafetyProof{};
};

[[nodiscard]] const char *tokenName(TokenKind kind);

} // namespace foundation

#endif
