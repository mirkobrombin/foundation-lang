#include "foundation/parser.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <utility>

namespace foundation {

Parser::Parser(std::vector<Token> tokens, Diagnostics &diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

Program Parser::parse() {
    Program program;
    while (!atEnd()) {
        if (!check(TokenKind::Fn)) {
            diagnostics_.error("FDN1001", "expected function declaration", current().span);
            advance();
            continue;
        }
        program.functions.push_back(function());
    }
    return program;
}

bool Parser::atEnd() const { return current().kind == TokenKind::Eof; }

const Token &Parser::current() const { return tokens_[current_]; }

const Token &Parser::previous() const { return tokens_[current_ - 1]; }

const Token &Parser::advance() {
    if (!atEnd()) {
        ++current_;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const { return current().kind == kind; }

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

const Token &Parser::expect(TokenKind kind, const char *code, const char *message) {
    if (check(kind)) {
        return advance();
    }
    diagnostics_.error(code, message, current().span);
    return current();
}

Function Parser::function() {
    const auto start = expect(TokenKind::Fn, "FDN1002", "expected fn");
    const auto name = expect(TokenKind::Identifier, "FDN1003", "expected function name");
    expect(TokenKind::LeftParen, "FDN1004", "expected ( after function name");
    expect(TokenKind::RightParen, "FDN1005", "expected ) after function parameters");
    expect(TokenKind::Arrow, "FDN1006", "expected -> before return type");
    const auto returnType =
        expect(TokenKind::Identifier, "FDN1007", "expected function return type");
    expect(TokenKind::LeftBrace, "FDN1008", "expected { before function body");

    Function result{name.text, returnType.text, {}, start.span};
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        result.statements.push_back(statement());
    }
    expect(TokenKind::RightBrace, "FDN1009", "expected } after function body");
    return result;
}

Statement Parser::statement() {
    if (match(TokenKind::Print)) {
        return printStatement(previous());
    }
    if (match(TokenKind::Return)) {
        return returnStatement(previous());
    }

    const auto bad = current();
    diagnostics_.error("FDN1010", "expected statement", bad.span);
    synchronizeStatement();
    return ReturnStatement{0, bad.span};
}

PrintStatement Parser::printStatement(const Token &start) {
    expect(TokenKind::LeftParen, "FDN1011", "expected ( after print");
    const auto value = expect(TokenKind::String, "FDN1012", "expected string argument");
    expect(TokenKind::RightParen, "FDN1013", "expected ) after print argument");
    expect(TokenKind::Semicolon, "FDN1014", "expected ; after print statement");
    return {value.text, start.span};
}

ReturnStatement Parser::returnStatement(const Token &start) {
    const auto value = expect(TokenKind::Integer, "FDN1015", "expected integer return value");
    std::int64_t parsed{};
    const auto conversion = std::from_chars(value.text.data(), value.text.data() + value.text.size(),
                                            parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != value.text.data() + value.text.size()) {
        diagnostics_.error("FDN1016", "integer is outside the supported range", value.span);
    }
    expect(TokenKind::Semicolon, "FDN1017", "expected ; after return statement");
    return {parsed, start.span};
}

void Parser::synchronizeStatement() {
    while (!atEnd() && !check(TokenKind::RightBrace)) {
        if (match(TokenKind::Semicolon)) {
            return;
        }
        advance();
    }
}

} // namespace foundation
