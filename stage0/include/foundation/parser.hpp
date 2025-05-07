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
    [[nodiscard]] const Token &peek(std::size_t distance) const;
    const Token &advance();
    [[nodiscard]] bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token &expect(TokenKind kind, const char *code, const char *message);
    StructDeclaration structDeclaration();
    Function function();
    Parameter parameter();
    AstBlockId block();
    AstStatementId statement();
    AstStatementId variableStatement(const Token &start, bool mutableBinding);
    AstStatementId returnStatement(const Token &start);
    AstStatementId ifStatement(const Token &start);
    AstStatementId whileStatement(const Token &start);
    AstStatementId assignmentStatement();
    AstStatementId expressionStatement();
    AstExpressionId expression();
    AstExpressionId logicalOr();
    AstExpressionId logicalAnd();
    AstExpressionId equality();
    AstExpressionId comparison();
    AstExpressionId term();
    AstExpressionId factor();
    AstExpressionId unary();
    AstExpressionId primary();
    AstExpressionId finishCall(const Token &callee);
    AstExpressionId finishStruct(const Token &type);
    AstExpressionId addExpression(ExpressionValue value, SourceSpan span);
    AstStatementId addStatement(StatementValue value, SourceSpan span);
    AstBlockId skipNestedBlock(SourceSpan span);

    std::vector<Token> tokens_;
    Diagnostics &diagnostics_;
    Program program_;
    std::size_t current_{};
    std::size_t blockDepth_{};
    std::size_t expressionDepth_{};
    std::size_t expressionCalls_{};
    std::size_t expressionNodes_{};
    bool expressionLimitReported_{};
};

} // namespace foundation

#endif
