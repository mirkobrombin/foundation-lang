#include "foundation/parser.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>
#include <utility>

namespace foundation {

namespace {

constexpr std::size_t maxBlockDepth = 128;
constexpr std::size_t maxExpressionDepth = 256;
constexpr std::size_t maxExpressionNodes = 1024;
constexpr std::size_t maxTypeDepth = 128;

bool isExported(const std::string &name) {
    return !name.empty() && name.front() >= 'A' && name.front() <= 'Z';
}

} // namespace

Parser::Parser(std::vector<Token> tokens, Diagnostics &diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

Program Parser::parse() {
    installBuiltins();
    while (!atEnd()) {
        if (check(TokenKind::Struct)) {
            program_.structs.push_back(structDeclaration());
            continue;
        }
        if (check(TokenKind::Enum)) {
            program_.enums.push_back(enumDeclaration());
            continue;
        }
        if (check(TokenKind::Fn)) {
            program_.functions.push_back(function());
            continue;
        }
        diagnostics_.error("FDN1001", "expected type or function declaration",
                           current().span);
        advance();
    }
    return std::move(program_);
}

bool Parser::atEnd() const { return current().kind == TokenKind::Eof; }

const Token &Parser::current() const { return tokens_[current_]; }

const Token &Parser::previous() const { return tokens_[current_ - 1]; }

const Token &Parser::peek(std::size_t distance) const {
    const auto index = current_ + distance;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

const Token &Parser::advance() {
    if (!atEnd()) {
        ++current_;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const { return current().kind == kind; }

bool Parser::continuesLine() const {
    return current_ != 0 && current().span.line == previous().span.line;
}

bool Parser::startsGenericPrimary() const {
    if (!check(TokenKind::Less)) {
        return false;
    }
    std::size_t depth = 0;
    for (std::size_t distance = 0;; ++distance) {
        const auto kind = peek(distance).kind;
        if (kind == TokenKind::Eof) {
            return false;
        }
        if (kind == TokenKind::Less) {
            ++depth;
        } else if (kind == TokenKind::Greater) {
            if (--depth == 0) {
                const auto next = peek(distance + 1).kind;
                return next == TokenKind::Dot || next == TokenKind::LeftBrace ||
                       next == TokenKind::LeftParen;
            }
        }
    }
}

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

std::vector<std::string> Parser::typeParameters() {
    std::vector<std::string> parameters;
    if (!match(TokenKind::Less)) {
        return parameters;
    }
    do {
        parameters.push_back(
            expect(TokenKind::Identifier, "FDN1061", "expected type parameter").text);
    } while (match(TokenKind::Comma));
    expect(TokenKind::Greater, "FDN1062", "expected > after type parameters");
    return parameters;
}

TypeSyntax Parser::typeSyntax(const char *code, const char *message) {
    const auto name = expect(TokenKind::Identifier, code, message);
    TypeSyntax type{name.text, {}, name.span};
    if (!match(TokenKind::Less)) {
        return type;
    }
    if (typeDepth_ >= maxTypeDepth) {
        diagnostics_.error("FDN1065", "type nesting exceeds 128 levels", name.span);
        std::size_t depth = 1;
        while (!atEnd() && depth != 0) {
            if (match(TokenKind::Less)) {
                ++depth;
            } else if (match(TokenKind::Greater)) {
                --depth;
            } else {
                advance();
            }
        }
        return type;
    }
    ++typeDepth_;
    do {
        type.arguments.push_back(typeSyntax("FDN1063", "expected type argument"));
    } while (match(TokenKind::Comma));
    expect(TokenKind::Greater, "FDN1064", "expected > after type arguments");
    --typeDepth_;
    return type;
}

StructDeclaration Parser::structDeclaration() {
    const auto start = expect(TokenKind::Struct, "FDN1031", "expected struct");
    const auto name = expect(TokenKind::Identifier, "FDN1032", "expected struct name");
    auto parameters = typeParameters();
    expect(TokenKind::LeftBrace, "FDN1033", "expected { after struct name");

    std::vector<StructField> fields;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1034", "expected struct field name", current().span);
            advance();
            continue;
        }
        const auto field = advance();
        auto type = typeSyntax("FDN1036", "expected struct field type");
        fields.push_back({field.text, std::move(type), isExported(field.text), field.span});
    }
    expect(TokenKind::RightBrace, "FDN1037", "expected } after struct declaration");
    return {name.text, std::move(parameters), std::move(fields), isExported(name.text),
            start.span};
}

EnumDeclaration Parser::enumDeclaration() {
    const auto start = expect(TokenKind::Enum, "FDN1042", "expected enum");
    const auto name = expect(TokenKind::Identifier, "FDN1043", "expected enum name");
    auto parameters = typeParameters();
    expect(TokenKind::LeftBrace, "FDN1044", "expected { after enum name");

    std::vector<EnumVariant> variants;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1045", "expected enum variant name", current().span);
            advance();
            continue;
        }
        const auto variant = advance();
        std::optional<TypeSyntax> payloadType;
        if (match(TokenKind::LeftParen)) {
            payloadType = typeSyntax("FDN1048", "expected payload type");
            expect(TokenKind::RightParen, "FDN1049", "expected ) after enum payload");
        }
        variants.push_back(
            {variant.text, std::move(payloadType), isExported(variant.text), variant.span});
    }
    expect(TokenKind::RightBrace, "FDN1050", "expected } after enum declaration");
    return {name.text, std::move(parameters), std::move(variants), isExported(name.text),
            BuiltinEnumKind::None, start.span};
}

Function Parser::function() {
    const auto start = expect(TokenKind::Fn, "FDN1002", "expected fn");
    const auto name = expect(TokenKind::Identifier, "FDN1003", "expected function name");
    auto typeParameters = this->typeParameters();
    expect(TokenKind::LeftParen, "FDN1004", "expected ( after function name");

    std::vector<Parameter> parameters;
    if (!check(TokenKind::RightParen)) {
        do {
            parameters.push_back(parameter());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1005", "expected ) after function parameters");
    auto returnType = typeSyntax("FDN1007", "expected function return type");
    const auto tailResult = returnType.name != "void" || !returnType.arguments.empty();
    const auto body = block(tailResult);
    return {name.text, std::move(typeParameters), std::move(parameters), std::move(returnType), body,
            isExported(name.text), start.span};
}

Parameter Parser::parameter() {
    const auto name = expect(TokenKind::Identifier, "FDN1026", "expected parameter name");
    auto type = typeSyntax("FDN1028", "expected parameter type");
    return {name.text, std::move(type), name.span};
}

AstBlockId Parser::block(bool tailResult) {
    const auto start = expect(TokenKind::LeftBrace, "FDN1008", "expected { before block");
    if (blockDepth_ >= maxBlockDepth) {
        diagnostics_.error("FDN1030", "block nesting exceeds 128 levels", start.span);
        return skipNestedBlock(start.span);
    }

    ++blockDepth_;
    Block result{{}, start.span};
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        result.statements.push_back(statement());
    }
    expect(TokenKind::RightBrace, "FDN1009", "expected } after block");
    if (tailResult && !result.statements.empty()) {
        auto &last = program_.statements[result.statements.back()];
        if (const auto *expression = std::get_if<ExpressionStatement>(&last.value)) {
            last.value = ReturnStatement{expression->expression};
        }
    }
    --blockDepth_;
    program_.blocks.push_back(std::move(result));
    return program_.blocks.size() - 1;
}

AstStatementId Parser::statement() {
    if (match(TokenKind::Let)) {
        return variableStatement(previous(), false);
    }
    if (match(TokenKind::Var)) {
        return variableStatement(previous(), true);
    }
    if (match(TokenKind::Return)) {
        return returnStatement(previous());
    }
    if (match(TokenKind::Discard)) {
        return discardStatement(previous());
    }
    if (match(TokenKind::If)) {
        return ifStatement(previous());
    }
    if (match(TokenKind::While)) {
        return whileStatement(previous());
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Equal) {
        return assignmentStatement();
    }
    return expressionStatement();
}

AstStatementId Parser::variableStatement(const Token &start, bool mutableBinding) {
    const auto name = expect(TokenKind::Identifier, "FDN1018", "expected binding name");
    std::optional<TypeSyntax> type;
    if (!check(TokenKind::Equal)) {
        type = typeSyntax("FDN1019", "expected binding type");
    }
    expect(TokenKind::Equal, "FDN1020", "expected = before binding initializer");
    const auto initializer = expression();
    std::optional<std::string> elseBinding;
    std::optional<AstBlockId> elseBlock;
    if (match(TokenKind::Else)) {
        if (mutableBinding) {
            diagnostics_.error("FDN1067", "var binding cannot use else", start.span);
        }
        elseBinding =
            expect(TokenKind::Identifier, "FDN1066", "expected error binding after else").text;
        elseBlock = block();
    }
    return addStatement(VariableStatement{mutableBinding, name.text, std::move(type), initializer,
                                          std::move(elseBinding), elseBlock},
                        start.span);
}

AstStatementId Parser::returnStatement(const Token &start) {
    std::optional<AstExpressionId> value;
    if (!check(TokenKind::RightBrace) && !atEnd() && current().span.line == start.span.line) {
        value = expression();
    }
    return addStatement(ReturnStatement{value}, start.span);
}

AstStatementId Parser::discardStatement(const Token &start) {
    return addStatement(DiscardStatement{expression()}, start.span);
}

AstStatementId Parser::ifStatement(const Token &start) {
    const auto allowed = structLiteralsAllowed_;
    structLiteralsAllowed_ = false;
    const auto condition = expression();
    structLiteralsAllowed_ = allowed;
    const auto thenBlock = block();
    std::optional<AstBlockId> elseBlock;
    if (match(TokenKind::Else)) {
        elseBlock = block();
    }
    return addStatement(IfStatement{condition, thenBlock, elseBlock}, start.span);
}

AstStatementId Parser::whileStatement(const Token &start) {
    const auto allowed = structLiteralsAllowed_;
    structLiteralsAllowed_ = false;
    const auto condition = expression();
    structLiteralsAllowed_ = allowed;
    const auto body = block();
    return addStatement(WhileStatement{condition, body}, start.span);
}

AstStatementId Parser::assignmentStatement() {
    const auto name = advance();
    expect(TokenKind::Equal, "FDN1021", "expected = in assignment");
    const auto value = expression();
    return addStatement(AssignmentStatement{name.text, value}, name.span);
}

AstStatementId Parser::expressionStatement() {
    const auto start = current().span;
    const auto value = expression();
    return addStatement(ExpressionStatement{value}, start);
}

AstExpressionId Parser::expression() {
    const auto root = expressionCalls_ == 0;
    if (root) {
        expressionNodes_ = 0;
        expressionLimitReported_ = false;
    }
    ++expressionCalls_;
    const auto result = logicalOr();
    --expressionCalls_;
    return result;
}

AstExpressionId Parser::logicalOr() {
    auto value = logicalAnd();
    while (continuesLine() && match(TokenKind::OrOr)) {
        const auto right = logicalAnd();
        value = addExpression(BinaryExpression{value, BinaryOperator::Or, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::logicalAnd() {
    auto value = equality();
    while (continuesLine() && match(TokenKind::AndAnd)) {
        const auto right = equality();
        value = addExpression(BinaryExpression{value, BinaryOperator::And, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::equality() {
    auto value = comparison();
    while (continuesLine() &&
           (check(TokenKind::EqualEqual) || check(TokenKind::BangEqual))) {
        const auto operation =
            advance().kind == TokenKind::EqualEqual ? BinaryOperator::Equal
                                                    : BinaryOperator::NotEqual;
        const auto right = comparison();
        value = addExpression(BinaryExpression{value, operation, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::comparison() {
    auto value = term();
    while (continuesLine() &&
           (check(TokenKind::Less) || check(TokenKind::LessEqual) ||
            check(TokenKind::Greater) || check(TokenKind::GreaterEqual))) {
        BinaryOperator operation{};
        switch (advance().kind) {
        case TokenKind::Less:
            operation = BinaryOperator::Less;
            break;
        case TokenKind::LessEqual:
            operation = BinaryOperator::LessEqual;
            break;
        case TokenKind::Greater:
            operation = BinaryOperator::Greater;
            break;
        case TokenKind::GreaterEqual:
            operation = BinaryOperator::GreaterEqual;
            break;
        default:
            break;
        }
        const auto right = term();
        value = addExpression(BinaryExpression{value, operation, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::term() {
    auto value = factor();
    while (continuesLine() && (check(TokenKind::Plus) || check(TokenKind::Minus))) {
        const auto operation = advance().kind == TokenKind::Plus ? BinaryOperator::Add
                                                                 : BinaryOperator::Subtract;
        const auto right = factor();
        value = addExpression(BinaryExpression{value, operation, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::factor() {
    auto value = unary();
    while (continuesLine() &&
           (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent))) {
        BinaryOperator operation{};
        switch (advance().kind) {
        case TokenKind::Star:
            operation = BinaryOperator::Multiply;
            break;
        case TokenKind::Slash:
            operation = BinaryOperator::Divide;
            break;
        case TokenKind::Percent:
            operation = BinaryOperator::Remainder;
            break;
        default:
            break;
        }
        const auto right = unary();
        value = addExpression(BinaryExpression{value, operation, right},
                              program_.expressions[value].span);
    }
    return value;
}

AstExpressionId Parser::unary() {
    if (expressionDepth_ >= maxExpressionDepth) {
        const auto bad = current();
        diagnostics_.error("FDN1029", "expression nesting exceeds 256 levels", bad.span);
        if (!atEnd()) {
            advance();
        }
        return addExpression(IntegerExpression{0}, bad.span);
    }

    ++expressionDepth_;
    AstExpressionId result;
    if (match(TokenKind::Minus)) {
        const auto start = previous().span;
        const auto operand = unary();
        if (const auto *integer =
                std::get_if<IntegerExpression>(&program_.expressions[operand].value)) {
            result = addExpression(IntegerExpression{-integer->value}, start);
        } else {
            result = addExpression(UnaryExpression{UnaryOperator::Negate, operand}, start);
        }
    } else if (match(TokenKind::Bang)) {
        const auto start = previous().span;
        result = addExpression(UnaryExpression{UnaryOperator::Not, unary()}, start);
    } else {
        result = primary();
    }
    --expressionDepth_;
    return result;
}

AstExpressionId Parser::primary() {
    AstExpressionId result;
    if (match(TokenKind::Integer)) {
        const auto token = previous();
        std::int64_t value{};
        const auto conversion = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(), value);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != token.text.data() + token.text.size()) {
            diagnostics_.error("FDN1016", "integer is outside the supported range", token.span);
        }
        result = addExpression(IntegerExpression{value}, token.span);
    } else if (match(TokenKind::True)) {
        result = addExpression(BooleanExpression{true}, previous().span);
    } else if (match(TokenKind::False)) {
        result = addExpression(BooleanExpression{false}, previous().span);
    } else if (match(TokenKind::String)) {
        const auto token = previous();
        result = addExpression(StringExpression{token.text}, token.span);
    } else if (match(TokenKind::Match)) {
        result = matchExpression(previous());
    } else if (match(TokenKind::Dot)) {
        result = finishMember(std::nullopt);
    } else if (match(TokenKind::Identifier)) {
        const auto name = previous();
        TypeSyntax type{name.text, {}, name.span};
        if (startsGenericPrimary()) {
            --current_;
            type = typeSyntax("FDN1063", "expected type");
        }
        if (match(TokenKind::LeftParen)) {
            result = finishCall(name, std::move(type.arguments));
        } else if (structLiteralsAllowed_ && check(TokenKind::LeftBrace) &&
                   peek(1).kind == TokenKind::Identifier && peek(2).kind == TokenKind::Equal) {
            advance();
            result = finishStruct(std::move(type));
        } else {
            result = addExpression(NameExpression{name.text, std::move(type.arguments)}, name.span);
        }
    } else if (match(TokenKind::LeftParen)) {
        result = expression();
        expect(TokenKind::RightParen, "FDN1022", "expected ) after expression");
    } else {
        const auto bad = current();
        diagnostics_.error("FDN1023", "expected expression", bad.span);
        if (!atEnd()) {
            advance();
        }
        result = addExpression(IntegerExpression{0}, bad.span);
    }

    while (continuesLine() && match(TokenKind::Dot)) {
        result = finishMember(result);
    }
    return result;
}

AstExpressionId Parser::finishCall(const Token &callee, std::vector<TypeSyntax> typeArguments) {
    std::vector<AstExpressionId> arguments;
    if (!check(TokenKind::RightParen)) {
        do {
            arguments.push_back(expression());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1024", "expected ) after call arguments");
    return addExpression(
        CallExpression{callee.text, std::move(typeArguments), std::move(arguments)}, callee.span);
}

AstExpressionId Parser::finishStruct(TypeSyntax type) {
    std::vector<StructFieldInitializer> fields;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1038", "expected struct literal field name", current().span);
            advance();
            continue;
        }
        const auto field = advance();
        expect(TokenKind::Equal, "FDN1039", "expected = after struct literal field name");
        fields.push_back({field.text, expression(), field.span});
    }
    expect(TokenKind::RightBrace, "FDN1040", "expected } after struct literal");
    const auto span = type.span;
    return addExpression(StructExpression{std::move(type), std::move(fields)}, span);
}

AstExpressionId Parser::finishMember(std::optional<AstExpressionId> base) {
    const auto member = expect(TokenKind::Identifier, "FDN1051", "expected member name");
    auto invoked = false;
    std::optional<AstExpressionId> payload;
    if (match(TokenKind::LeftParen)) {
        invoked = true;
        if (!check(TokenKind::RightParen)) {
            payload = expression();
        }
        expect(TokenKind::RightParen, "FDN1052", "expected ) after member invocation");
    }
    return addExpression(MemberExpression{base, member.text, invoked, payload}, member.span);
}

AstExpressionId Parser::matchExpression(const Token &start) {
    const auto value = expression();
    expect(TokenKind::LeftBrace, "FDN1053", "expected { after match value");
    std::vector<MatchArm> arms;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        const auto variant = expect(TokenKind::Identifier, "FDN1056", "expected pattern variant");
        std::optional<std::string> binding;
        if (match(TokenKind::LeftParen)) {
            binding = expect(TokenKind::Identifier, "FDN1057", "expected payload binding").text;
            expect(TokenKind::RightParen, "FDN1058", "expected ) after payload binding");
        }
        expect(TokenKind::Colon, "FDN1059", "expected : after match pattern");
        arms.push_back({variant.text, std::move(binding), expression(), variant.span});
    }
    expect(TokenKind::RightBrace, "FDN1060", "expected } after match expression");
    return addExpression(MatchExpression{value, std::move(arms)}, start.span);
}

AstExpressionId Parser::addExpression(ExpressionValue value, SourceSpan span) {
    ++expressionNodes_;
    if (expressionNodes_ > maxExpressionNodes && !expressionLimitReported_) {
        diagnostics_.error("FDN1029", "expression exceeds 1024 nodes", span);
        expressionLimitReported_ = true;
    }
    program_.expressions.push_back({std::move(value), span});
    return program_.expressions.size() - 1;
}

AstStatementId Parser::addStatement(StatementValue value, SourceSpan span) {
    program_.statements.push_back({std::move(value), span});
    return program_.statements.size() - 1;
}

AstBlockId Parser::skipNestedBlock(SourceSpan span) {
    std::size_t depth = 1;
    while (!atEnd() && depth != 0) {
        if (match(TokenKind::LeftBrace)) {
            ++depth;
        } else if (match(TokenKind::RightBrace)) {
            --depth;
        } else {
            advance();
        }
    }
    program_.blocks.push_back({{}, span});
    return program_.blocks.size() - 1;
}

void Parser::installBuiltins() {
    constexpr SourceSpan span{0, 0, 1, 1};
    const TypeSyntax optionValue{"T", {}, span};
    program_.enums.push_back({
        "Option",
        {"T"},
        {{"None", std::nullopt, true, span},
         {"Some", optionValue, true, span}},
        true,
        BuiltinEnumKind::Option,
        span,
    });

    const TypeSyntax resultValue{"T", {}, span};
    const TypeSyntax resultError{"E", {}, span};
    program_.enums.push_back({
        "Result",
        {"T", "E"},
        {{"Ok", resultValue, true, span},
         {"Err", resultError, true, span}},
        true,
        BuiltinEnumKind::Result,
        span,
    });
}

} // namespace foundation
