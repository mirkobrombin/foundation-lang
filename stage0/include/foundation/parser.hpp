#ifndef FOUNDATION_PARSER_HPP
#define FOUNDATION_PARSER_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/target.hpp"
#include "foundation/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace foundation {

[[nodiscard]] std::optional<std::size_t>
typeArgumentListClosingToken(const std::vector<Token> &tokens, std::size_t opening);

class Parser {
  public:
    Parser(std::vector<Token> tokens, Diagnostics &diagnostics, bool installBuiltins = true,
           TargetPlatform target = hostTargetPlatform());
    [[nodiscard]] Program parse();

  private:
    struct ParsedAttributes {
        bool selected{true};
        std::vector<AttributeApplication> applications;
    };

    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] const Token &current() const;
    [[nodiscard]] const Token &previous() const;
    [[nodiscard]] const Token &peek(std::size_t distance) const;
    const Token &advance();
    [[nodiscard]] bool check(TokenKind kind) const;
    [[nodiscard]] bool continuesLine() const;
    [[nodiscard]] bool startsGenericPrimary() const;
    [[nodiscard]] bool startsTailIfExpression() const;
    [[nodiscard]] ParsedAttributes attributes(bool allowTarget = true);
    [[nodiscard]] std::optional<AttributeTarget> attributeTarget();
    [[nodiscard]] TargetPlatform targetArgument(const Token &argument);
    void restoreProgram(std::size_t expressions, std::size_t statements,
                        std::size_t blocks, std::size_t functions);
    bool match(TokenKind kind);
    const Token &expect(TokenKind kind, const char *code, const char *message);
    std::pair<std::string, SourceSpan> qualifiedName(const char *code, const char *message);
    std::vector<std::string> typeParameters();
    TypeSyntax typeSyntax(const char *code, const char *message);
    StructDeclaration structDeclaration(bool service = false);
    void methodsDeclaration();
    EnumDeclaration enumDeclaration();
    void stateMachineDeclaration();
    void workflowDeclaration(WorkflowKind kind);
    ContractDeclaration contractDeclaration();
    AttributeDeclaration attributeDeclaration();
    Function function(bool external = false, bool task = false);
    Function testDeclaration();
    Function method(const std::string &owner, const std::vector<std::string> &typeParameters,
                    bool action = false);
    ContractMethod contractMethod(const std::string &owner,
                                  const std::vector<std::string> &typeParameters);
    ReceiverKind receiver(const char *code, const char *message);
    Parameter parameter(bool allowInferredType = false);
    AstBlockId block(bool tailResult = false);
    AstStatementId statement();
    AstStatementId variableStatement(const Token &start, bool mutableBinding);
    AstStatementId structDestructureStatement(const Token &start);
    AstStatementId returnStatement(const Token &start);
    AstStatementId discardStatement(const Token &start);
    AstStatementId ifStatement(const Token &start);
    AstStatementId whileStatement(const Token &start);
    AstStatementId forStatement(const Token &start);
    AstStatementId loopJumpStatement(const Token &start, bool continues);
    AstStatementId selectStatement(const Token &start);
    AstStatementId unsafeStatement(const Token &start);
    AstBlockId selectArmBlock();
    AstStatementId expressionStatement();
    AstExpressionId expression();
    AstExpressionId conditional();
    AstExpressionId logicalOr();
    AstExpressionId logicalAnd();
    AstExpressionId equality();
    AstExpressionId comparison();
    AstExpressionId term();
    AstExpressionId factor();
    AstExpressionId unary();
    AstExpressionId primary();
    AstExpressionId replaceExpression(const Token &start);
    AstExpressionId finishCall(const Token &callee, std::vector<TypeSyntax> typeArguments = {});
    AstExpressionId finishStruct(TypeSyntax type);
    AstExpressionId finishMember(std::optional<AstExpressionId> base);
    AstExpressionId finishArray(const Token &start);
    AstExpressionId matchExpression(const Token &start);
    AstExpressionId ifExpression(const Token &start);
    std::pair<AstBlockId, AstExpressionId> expressionBlock();
    AstExpressionId functionExpression(const Token &start);
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
    std::vector<std::string> activeTypeParameters_;
    bool expressionLimitReported_{};
    bool structLiteralsAllowed_{true};
    bool installBuiltins_{};
    TargetPlatform target_{TargetPlatform::Unknown};
};

} // namespace foundation

#endif
