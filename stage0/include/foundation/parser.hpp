#ifndef FOUNDATION_PARSER_HPP
#define FOUNDATION_PARSER_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/token.hpp"

#include <cstddef>
#include <vector>

namespace foundation {

class Parser {
  public:
    Parser(std::vector<Token> tokens, Diagnostics &diagnostics);
    [[nodiscard]] Program parse();

  private:
    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] const Token &current() const;
    [[nodiscard]] const Token &previous() const;
    const Token &advance();
    [[nodiscard]] bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token &expect(TokenKind kind, const char *code, const char *message);
    Function function();
    Statement statement();
    PrintStatement printStatement(const Token &start);
    ReturnStatement returnStatement(const Token &start);
    void synchronizeStatement();

    std::vector<Token> tokens_;
    Diagnostics &diagnostics_;
    std::size_t current_{};
};

} // namespace foundation

#endif
