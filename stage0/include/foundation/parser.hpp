#ifndef FOUNDATION_PARSER_HPP
#define FOUNDATION_PARSER_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/token.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace foundation {

class Parser {
  public:
    Parser(std::vector<Token> tokens, Diagnostics &diagnostics, bool installBuiltins = true);
    [[nodiscard]] Program parse();

  private:
    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] const Token &current() const;
    [[nodiscard]] const Token &previous() const;
    [[nodiscard]] const Token &peek(std::size_t distance) const;
    const Token &advance();
    [[nodiscard]] bool check(TokenKind kind) const;
    [[nodiscard]] bool continuesLine() const;
    [[nodiscard]] bool startsGenericPrimary() const;
    bool match(TokenKind kind);
    const Token &expect(TokenKind kind, const char *code, const char *message);
    std::pair<std::string, SourceSpan> qualifiedName(const char *code, const char *message);
    std::vector<std::string> typeParameters();
    TypeSyntax typeSyntax(const char *code, const char *message);
    StructDeclaration structDeclaration();
    EnumDeclaration enumDeclaration();
    Function function();
    Parameter parameter();
    AstBlockId block(bool tailResult = false);
    AstStatementId statement();
    AstStatementId variableStatement(const Token &start, bool mutableBinding);
    AstStatementId returnStatement(const Token &start);
    AstStatementId discardStatement(const Token &start);
    AstStatementId ifStatement(const Token &start);
    AstStatementId whileStatement(const Token &start);
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
    AstExpressionId finishCall(const Token &callee, std::vector<TypeSyntax> typeArguments = {});
    AstExpressionId finishStruct(TypeSyntax type);
    AstExpressionId finishMember(std::optional<AstExpressionId> base);
    AstExpressionId finishArray(const Token &start);
    AstExpressionId matchExpression(const Token &start);
    AstExpressionId addExpression(ExpressionValue value, SourceSpan span);
    AstStatementId addStatement(StatementValue value, SourceSpan span);
    AstBlockId skipNestedBlock(SourceSpan span);
    void installBuiltins();

    std::vector<Token> tokens_;
    Diagnostics &diagnostics_;
    Program program_;
    std::size_t current_{};
    std::size_t blockDepth_{};
    std::size_t expressionDepth_{};
    std::size_t expressionCalls_{};
    std::size_t expressionNodes_{};
    std::size_t typeDepth_{};
    bool expressionLimitReported_{};
    bool structLiteralsAllowed_{true};
    bool installBuiltins_{};
};

} // namespace foundation

#endif
