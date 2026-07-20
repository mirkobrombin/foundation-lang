#include "foundation/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <utility>

namespace foundation {

namespace {

constexpr std::size_t maxBlockDepth = 128;
constexpr std::size_t maxExpressionDepth = 256;
constexpr std::size_t maxExpressionNodes = 1024;
constexpr std::size_t maxTypeDepth = 128;
constexpr std::size_t maxGenericLookaheadTokens = 4096;

bool hasBlockingAttribute(const std::vector<AttributeApplication> &attributes) {
    return std::any_of(attributes.begin(), attributes.end(), [](const auto &attribute) {
        return attribute.name == "blocking";
    });
}

bool hasCallbackAttribute(const std::vector<AttributeApplication> &attributes) {
    return std::any_of(attributes.begin(), attributes.end(), [](const auto &attribute) {
        return attribute.name == "callback";
    });
}

bool sameTypeSyntax(const TypeSyntax &left, const TypeSyntax &right) {
    if (left.name != right.name || left.arrayLength != right.arrayLength ||
        left.arguments.size() != right.arguments.size()) {
        return false;
    }
    return std::equal(left.arguments.begin(), left.arguments.end(), right.arguments.begin(),
                      sameTypeSyntax);
}

class TypeLookahead {
  public:
    TypeLookahead(const std::vector<Token> &tokens, std::size_t start)
        : tokens_(tokens), start_(start), current_(start) {}

    [[nodiscard]] bool scanTypeArguments(std::size_t depth, std::size_t &closing) {
        if (depth >= maxTypeDepth || !match(TokenKind::Less) || !scanType(depth + 1)) {
            return false;
        }
        while (match(TokenKind::Comma)) {
            if (!scanType(depth + 1)) {
                return false;
            }
        }
        if (!check(TokenKind::Greater)) {
            return false;
        }
        closing = current_;
        ++current_;
        return true;
    }

  private:
    [[nodiscard]] bool withinBudget() const {
        return current_ < tokens_.size() &&
               current_ - start_ < maxGenericLookaheadTokens;
    }

    [[nodiscard]] bool check(TokenKind kind) const {
        return withinBudget() && tokens_[current_].kind == kind;
    }

    bool match(TokenKind kind) {
        if (!check(kind)) {
            return false;
        }
        ++current_;
        return true;
    }

    bool scanType(std::size_t depth) {
        if (depth >= maxTypeDepth || !withinBudget()) {
            return false;
        }
        if (check(TokenKind::Own) || check(TokenKind::View) || check(TokenKind::Edit)) {
            ++current_;
            return scanType(depth + 1);
        }
        if (match(TokenKind::Star)) {
            match(TokenKind::Const);
            return scanType(depth + 1);
        }
        if (match(TokenKind::Fn)) {
            if (!match(TokenKind::LeftParen)) {
                return false;
            }
            if (!check(TokenKind::RightParen)) {
                if (!scanType(depth + 1)) {
                    return false;
                }
                while (match(TokenKind::Comma)) {
                    if (!scanType(depth + 1)) {
                        return false;
                    }
                }
            }
            return match(TokenKind::RightParen) && scanType(depth + 1);
        }
        if (match(TokenKind::LeftBracket)) {
            if (match(TokenKind::Minus)) {
                if (!match(TokenKind::Integer) || !match(TokenKind::RightBracket)) {
                    return false;
                }
                return scanType(depth + 1);
            }
            if (match(TokenKind::Integer)) {
                return match(TokenKind::RightBracket) && scanType(depth + 1);
            }
            return scanType(depth + 1) && match(TokenKind::RightBracket);
        }
        if (!match(TokenKind::Identifier)) {
            return false;
        }
        while (match(TokenKind::Dot)) {
            if (!match(TokenKind::Identifier)) {
                return false;
            }
        }
        if (check(TokenKind::Less)) {
            std::size_t closing{};
            return scanTypeArguments(depth, closing);
        }
        return true;
    }

    const std::vector<Token> &tokens_;
    std::size_t start_{};
    std::size_t current_{};
};

bool isExported(const std::string &name) {
    return !name.empty() && name.front() >= 'A' && name.front() <= 'Z';
}

} // namespace

std::optional<std::size_t>
typeArgumentListClosingToken(const std::vector<Token> &tokens, std::size_t opening) {
    if (opening >= tokens.size() || tokens[opening].kind != TokenKind::Less) {
        return std::nullopt;
    }
    std::size_t closing{};
    TypeLookahead lookahead(tokens, opening);
    if (!lookahead.scanTypeArguments(0, closing)) {
        return std::nullopt;
    }
    return closing;
}

Parser::Parser(std::vector<Token> tokens, Diagnostics &diagnostics, bool installBuiltins,
               TargetPlatform target)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics), installBuiltins_(installBuiltins),
      target_(target) {}

Program Parser::parse() {
    if (installBuiltins_) {
        installBuiltins();
    }
    if (match(TokenKind::Package)) {
        const auto [name, span] =
            qualifiedName("FDN1090", "expected package name after package");
        program_.packageName = name;
        program_.hasPackageDeclaration = true;
        if (name.empty()) {
            diagnostics_.error("FDN1090", "package name cannot be empty", span);
        }
    }
    while (match(TokenKind::Import)) {
        const auto [name, span] =
            qualifiedName("FDN1091", "expected package name after import");
        std::string alias;
        if (match(TokenKind::As)) {
            alias = expect(TokenKind::Identifier, "FDN1092", "expected import alias after as").text;
        }
        program_.imports.push_back({name, std::move(alias), span});
    }
    while (!atEnd()) {
        const auto expressions = program_.expressions.size();
        const auto statements = program_.statements.size();
        const auto blocks = program_.blocks.size();
        const auto functions = program_.functions.size();
        auto parsedAttributes = attributes();
        if (check(TokenKind::Struct)) {
            auto declaration = structDeclaration();
            declaration.attributes = std::move(parsedAttributes.applications);
            if (parsedAttributes.selected) {
                program_.structs.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Service)) {
            auto declaration = structDeclaration(true);
            declaration.attributes = std::move(parsedAttributes.applications);
            if (parsedAttributes.selected) {
                program_.structs.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Methods)) {
            if (!parsedAttributes.applications.empty()) {
                diagnostics_.error("FDN1160", "methods block cannot be attributed",
                                   parsedAttributes.applications.front().span);
            }
            methodsDeclaration();
            if (!parsedAttributes.selected) {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Enum)) {
            auto declaration = enumDeclaration();
            declaration.attributes = std::move(parsedAttributes.applications);
            if (parsedAttributes.selected) {
                program_.enums.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::StateMachine)) {
            const auto enums = program_.enums.size();
            stateMachineDeclaration();
            if (parsedAttributes.selected) {
                if (program_.enums.size() > enums) {
                    program_.enums[enums].attributes =
                        std::move(parsedAttributes.applications);
                }
            } else {
                restoreProgram(expressions, statements, blocks, functions);
                program_.enums.resize(enums);
            }
            continue;
        }
        if (check(TokenKind::Pipeline) || check(TokenKind::Saga)) {
            const auto structs = program_.structs.size();
            const auto enums = program_.enums.size();
            workflowDeclaration(check(TokenKind::Pipeline) ? WorkflowKind::Pipeline
                                                           : WorkflowKind::Saga);
            if (!parsedAttributes.selected) {
                restoreProgram(expressions, statements, blocks, functions);
                program_.structs.resize(structs);
                program_.enums.resize(enums);
            } else if (!parsedAttributes.applications.empty()) {
                diagnostics_.error("FDN1148", "workflow attributes are not supported yet",
                                   parsedAttributes.applications.front().span);
            }
            continue;
        }
        if (check(TokenKind::Contract)) {
            auto declaration = contractDeclaration();
            declaration.attributes = std::move(parsedAttributes.applications);
            if (parsedAttributes.selected) {
                program_.contracts.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Attribute)) {
            auto declaration = attributeDeclaration();
            if (!parsedAttributes.applications.empty()) {
                diagnostics_.error("FDN1159", "attribute declaration cannot be attributed",
                                   parsedAttributes.applications.front().span);
            }
            if (parsedAttributes.selected) {
                program_.attributeDeclarations.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Fn)) {
            auto declaration = function();
            declaration.attributes = std::move(parsedAttributes.applications);
            declaration.blocking = hasBlockingAttribute(declaration.attributes);
            declaration.callback = hasCallbackAttribute(declaration.attributes);
            if (parsedAttributes.selected) {
                program_.functions.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Task)) {
            auto declaration = function(false, true);
            declaration.attributes = std::move(parsedAttributes.applications);
            declaration.blocking = hasBlockingAttribute(declaration.attributes);
            declaration.callback = hasCallbackAttribute(declaration.attributes);
            if (parsedAttributes.selected) {
                program_.functions.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Test) ||
            (check(TokenKind::Identifier) && current().text == "test")) {
            auto declaration = testDeclaration();
            declaration.attributes = std::move(parsedAttributes.applications);
            if (parsedAttributes.selected) {
                program_.functions.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        if (check(TokenKind::Extern)) {
            auto declaration = function(true);
            declaration.attributes = std::move(parsedAttributes.applications);
            declaration.blocking = hasBlockingAttribute(declaration.attributes);
            declaration.callback = hasCallbackAttribute(declaration.attributes);
            if (parsedAttributes.selected) {
                program_.functions.push_back(std::move(declaration));
            } else {
                restoreProgram(expressions, statements, blocks, functions);
            }
            continue;
        }
        diagnostics_.error("FDN1001", "expected type or function declaration",
                           current().span);
        advance();
    }
    return std::move(program_);
}

Parser::ParsedAttributes Parser::attributes(bool allowTarget) {
    ParsedAttributes result;
    auto foundTarget = false;
    while (match(TokenKind::At)) {
        const auto [name, span] =
            qualifiedName("FDN1140", "expected attribute name after @");
        if ((name == "blocking" || name == "callback") &&
            !check(TokenKind::LeftParen)) {
            result.applications.push_back({name, {}, span, false});
            continue;
        }
        expect(TokenKind::LeftParen, "FDN1141", "expected ( after attribute name");
        if (name == "target") {
            const auto argument = expect(TokenKind::Identifier, "FDN1142",
                                         "expected target name in compiler attribute");
            expect(TokenKind::RightParen, "FDN1143", "expected ) after compiler attribute");
            if (!allowTarget) {
                diagnostics_.error("FDN1146", "@target is only valid on package declarations",
                                   span);
            } else if (foundTarget) {
                diagnostics_.error("FDN1144", "declaration has more than one @target attribute",
                                   span);
            } else {
                foundTarget = true;
                const auto requested = targetArgument(argument);
                result.selected = requested != TargetPlatform::Unknown && requested == target_;
            }
            continue;
        }
        std::vector<AttributeArgument> arguments;
        if (!check(TokenKind::RightParen)) {
            do {
                std::optional<std::string> argumentName;
                auto argumentSpan = current().span;
                if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Equal) {
                    argumentName = advance().text;
                    advance();
                }
                arguments.push_back(
                    {std::move(argumentName), expression(), argumentSpan});
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "FDN1143", "expected ) after attribute arguments");
        result.applications.push_back({name, std::move(arguments), span});
    }
    return result;
}

std::optional<AttributeTarget> Parser::attributeTarget() {
    const auto kind = current().kind;
    if (kind == TokenKind::Fn) {
        advance();
        return AttributeTarget::Function;
    }
    if (kind == TokenKind::Struct) {
        advance();
        return AttributeTarget::Struct;
    }
    if (kind == TokenKind::Service) {
        advance();
        return AttributeTarget::Service;
    }
    if (kind == TokenKind::Enum) {
        advance();
        return AttributeTarget::Enum;
    }
    if (kind == TokenKind::Contract) {
        advance();
        return AttributeTarget::Contract;
    }
    if (kind == TokenKind::Action) {
        advance();
        return AttributeTarget::Action;
    }
    if (kind != TokenKind::Identifier) {
        diagnostics_.error("FDN1154", "expected attribute target", current().span);
        if (!atEnd()) {
            advance();
        }
        return std::nullopt;
    }
    const auto target = advance();
    if (target.text == "method") {
        return AttributeTarget::Method;
    }
    if (target.text == "field") {
        return AttributeTarget::Field;
    }
    if (target.text == "variant") {
        return AttributeTarget::Variant;
    }
    if (target.text == "parameter") {
        return AttributeTarget::Parameter;
    }
    diagnostics_.error("FDN1154", "unknown attribute target " + target.text, target.span);
    return std::nullopt;
}

TargetPlatform Parser::targetArgument(const Token &argument) {
    if (argument.text == "linux") {
        return TargetPlatform::Linux;
    }
    if (argument.text == "macos") {
        return TargetPlatform::MacOS;
    }
    if (argument.text == "windows") {
        return TargetPlatform::Windows;
    }
    diagnostics_.error("FDN1142", "unknown target " + argument.text, argument.span);
    return TargetPlatform::Unknown;
}

void Parser::restoreProgram(std::size_t expressions, std::size_t statements,
                            std::size_t blocks, std::size_t functions) {
    program_.expressions.resize(expressions);
    program_.statements.resize(statements);
    program_.blocks.resize(blocks);
    program_.functions.resize(functions);
}

std::pair<std::string, SourceSpan> Parser::qualifiedName(const char *code,
                                                        const char *message) {
    const auto takeSegment = [&]() -> const Token & {
        if (check(TokenKind::Identifier) || check(TokenKind::Test)) {
            return advance();
        }
        return expect(TokenKind::Identifier, code, message);
    };
    const auto first = takeSegment();
    std::string name = first.text;
    while (match(TokenKind::Dot)) {
        const auto segment = takeSegment();
        name += '.';
        name += segment.text;
    }
    return {std::move(name), first.span};
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

bool Parser::startsTailIfExpression() const {
    if (!check(TokenKind::If)) {
        return false;
    }
    auto index = current_ + 1;
    while (index < tokens_.size() && tokens_[index].kind != TokenKind::LeftBrace &&
           tokens_[index].kind != TokenKind::Eof) {
        ++index;
    }
    const auto skipBlock = [&](std::size_t &position, bool &hasTailValue) {
        if (position >= tokens_.size() ||
            tokens_[position].kind != TokenKind::LeftBrace) {
            return false;
        }
        const auto contentStart = position + 1;
        auto depth = std::size_t{1};
        ++position;
        while (position < tokens_.size() && depth != 0) {
            if (tokens_[position].kind == TokenKind::LeftBrace) {
                ++depth;
            } else if (tokens_[position].kind == TokenKind::RightBrace) {
                --depth;
            }
            ++position;
        }
        if (depth != 0 || position <= contentStart + 1) {
            return false;
        }
        const auto closing = position - 1;
        auto tailStart = closing - 1;
        const auto tailLine = tokens_[tailStart].span.line;
        while (tailStart > contentStart &&
               tokens_[tailStart - 1].span.line == tailLine) {
            --tailStart;
        }
        const auto kind = tokens_[tailStart].kind;
        hasTailValue = kind != TokenKind::Let && kind != TokenKind::Const &&
                       kind != TokenKind::Var && kind != TokenKind::Return &&
                       kind != TokenKind::Discard && kind != TokenKind::While &&
                       kind != TokenKind::Select;
        return true;
    };
    auto thenHasValue = false;
    if (!skipBlock(index, thenHasValue) || !thenHasValue || index >= tokens_.size() ||
        tokens_[index].kind != TokenKind::Else) {
        return false;
    }
    ++index;
    auto elseHasValue = false;
    return skipBlock(index, elseHasValue) && elseHasValue &&
           index < tokens_.size() && tokens_[index].kind == TokenKind::RightBrace;
}

bool Parser::startsGenericPrimary() const {
    const auto closing = typeArgumentListClosingToken(tokens_, current_);
    if (!closing.has_value() || *closing + 1 >= tokens_.size()) {
        return false;
    }
    const auto &close = tokens_[*closing];
    const auto &next = tokens_[*closing + 1];
    const auto continues = next.span.line == close.span.line;
    if (continues && (next.kind == TokenKind::Dot || next.kind == TokenKind::LeftBrace ||
                      next.kind == TokenKind::LeftParen)) {
        return true;
    }
    if (next.kind == TokenKind::Eof || !continues) {
        return true;
    }
    return next.kind == TokenKind::Comma || next.kind == TokenKind::RightParen ||
           next.kind == TokenKind::RightBracket || next.kind == TokenKind::RightBrace;
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
    if (match(TokenKind::Star)) {
        const auto pointer = previous();
        const auto readOnly = match(TokenKind::Const);
        TypeSyntax type{readOnly ? "[raw-const]" : "[raw]", {}, pointer.span};
        if (typeDepth_ >= maxTypeDepth) {
            diagnostics_.error("FDN1065", "type nesting exceeds 128 levels", pointer.span);
            return type;
        }
        ++typeDepth_;
        type.arguments.push_back(typeSyntax(code, message));
        --typeDepth_;
        return type;
    }
    if (check(TokenKind::Own) || check(TokenKind::View) || check(TokenKind::Edit)) {
        const auto qualifier = advance();
        TypeSyntax type{qualifier.text, {}, qualifier.span};
        if (typeDepth_ >= maxTypeDepth) {
            diagnostics_.error("FDN1065", "type nesting exceeds 128 levels", qualifier.span);
            return type;
        }
        ++typeDepth_;
        type.arguments.push_back(typeSyntax(code, message));
        --typeDepth_;
        return type;
    }
    if (match(TokenKind::Fn)) {
        const auto start = previous();
        TypeSyntax type{"[function]", {}, start.span};
        expect(TokenKind::LeftParen, "FDN1120", "expected ( in function type");
        if (!check(TokenKind::RightParen)) {
            do {
                auto mode = std::string{"[function-read]"};
                auto modeSpan = current().span;
                if (match(TokenKind::Ampersand)) {
                    mode = "[function-edit]";
                    modeSpan = previous().span;
                } else if (match(TokenKind::Dollar)) {
                    mode = "[function-transfer]";
                    modeSpan = previous().span;
                } else if (check(TokenKind::Own) || check(TokenKind::View) ||
                           check(TokenKind::Edit)) {
                    type.arguments.push_back(
                        typeSyntax("FDN1121", "expected function parameter type"));
                    continue;
                }
                auto parameter = typeSyntax("FDN1121", "expected function parameter type");
                type.arguments.push_back(
                    TypeSyntax{std::move(mode), {std::move(parameter)}, modeSpan});
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "FDN1122", "expected ) in function type");
        type.arguments.insert(type.arguments.begin(),
                              typeSyntax("FDN1123", "expected function return type"));
        return type;
    }
    if (match(TokenKind::LeftBracket)) {
        const auto start = previous();
        if (match(TokenKind::Minus)) {
            const auto sign = previous();
            if (match(TokenKind::Integer)) {
                diagnostics_.error("FDN1080", "array length must be non-negative", sign.span);
                expect(TokenKind::RightBracket, "FDN1081", "expected ] after array length");
                TypeSyntax type{"[array]", {}, start.span, 0};
                type.arguments.push_back(typeSyntax(code, message));
                return type;
            }
        }
        if (match(TokenKind::Integer)) {
            const auto lengthToken = previous();
            std::size_t length{};
            const auto conversion =
                std::from_chars(lengthToken.text.data(),
                                lengthToken.text.data() + lengthToken.text.size(), length);
            if (conversion.ec != std::errc{} ||
                conversion.ptr != lengthToken.text.data() + lengthToken.text.size()) {
                diagnostics_.error("FDN1080", "array length is outside the supported range",
                                   lengthToken.span);
            }
            expect(TokenKind::RightBracket, "FDN1081", "expected ] after array length");
            TypeSyntax type{"[array]", {}, start.span, length};
            type.arguments.push_back(typeSyntax(code, message));
            return type;
        }
        TypeSyntax type{"[slice]", {}, start.span};
        type.arguments.push_back(typeSyntax("FDN1082", "expected slice element type"));
        expect(TokenKind::RightBracket, "FDN1083", "expected ] after slice element type");
        return type;
    }
    const auto [qualified, span] = qualifiedName(code, message);
    TypeSyntax type{qualified, {}, span};
    if (!match(TokenKind::Less)) {
        return type;
    }
    if (typeDepth_ >= maxTypeDepth) {
        diagnostics_.error("FDN1065", "type nesting exceeds 128 levels", span);
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

StructDeclaration Parser::structDeclaration(bool service) {
    const auto declarationKind = service ? TokenKind::Service : TokenKind::Struct;
    const auto start = expect(declarationKind, "FDN1031",
                              service ? "expected service" : "expected struct");
    const auto name = expect(TokenKind::Identifier, "FDN1032",
                             service ? "expected service name" : "expected struct name");
    auto parameters = typeParameters();
    std::vector<StructImplementation> implementations;
    if (match(TokenKind::Implements)) {
        do {
            auto contract = typeSyntax("FDN1094", "expected contract after implements");
            const auto span = contract.span;
            implementations.push_back({std::move(contract), std::nullopt, span});
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::LeftBrace, "FDN1033", "expected { after struct name");

    std::vector<StructField> fields;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (match(TokenKind::Delegate)) {
            const auto declaration = previous();
            const auto field = expect(TokenKind::Identifier, "FDN1168",
                                      "expected field after delegate");
            expect(TokenKind::As, "FDN1169", "expected as after delegated field");
            auto contract = typeSyntax("FDN1170", "expected contract after as");
            const auto implementation = std::find_if(
                implementations.begin(), implementations.end(), [&](const auto &candidate) {
                    return sameTypeSyntax(candidate.contract, contract);
                });
            if (implementation == implementations.end()) {
                diagnostics_.error("FDN1171",
                                   "delegated contract must appear after implements",
                                   contract.span);
            } else if (implementation->delegate.has_value()) {
                diagnostics_.error("FDN1172", "contract already has a delegate",
                                   declaration.span);
            } else {
                implementation->delegate = field.text;
            }
            continue;
        }
        auto parsedAttributes = attributes(false);
        if (check(TokenKind::Extern)) {
            diagnostics_.error("FDN2117", "C ABI function cannot be a method", current().span);
            advance();
            continue;
        }
        if (check(TokenKind::Fn) || check(TokenKind::Action)) {
            const auto action = check(TokenKind::Action);
            auto declaration = method(name.text, parameters, action);
            declaration.attributes = std::move(parsedAttributes.applications);
            program_.functions.push_back(std::move(declaration));
            continue;
        }
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1034", "expected struct field or method", current().span);
            advance();
            continue;
        }
        const auto field = advance();
        auto type = typeSyntax("FDN1036", "expected struct field type");
        std::optional<AstFunctionId> defaultFunction;
        std::optional<SourceSpan> defaultSpan;
        if (match(TokenKind::Equal)) {
            const auto defaultStart = current().span;
            auto previousTypeParameters = std::move(activeTypeParameters_);
            activeTypeParameters_ = parameters;
            const auto value = expression();
            activeTypeParameters_ = std::move(previousTypeParameters);
            const auto defaultEnd = previous().span;
            defaultSpan = SourceSpan{
                defaultStart.offset,
                defaultEnd.offset + defaultEnd.length - defaultStart.offset,
                defaultStart.line,
                defaultStart.column,
                defaultStart.source,
            };

            const auto returned = addStatement(ReturnStatement{value}, field.span);
            program_.blocks.push_back({{returned}, field.span});

            Function initializer;
            initializer.name = "$field_default." + name.text + '.' + field.text;
            initializer.typeParameters = parameters;
            initializer.returnType = type;
            initializer.body = program_.blocks.size() - 1;
            initializer.span = field.span;
            defaultFunction = program_.functions.size();
            program_.functions.push_back(std::move(initializer));
        }
        fields.push_back({field.text, std::move(type), isExported(field.text), field.span,
                          std::move(parsedAttributes.applications), defaultFunction,
                          defaultSpan});
    }
    expect(TokenKind::RightBrace, "FDN1037", "expected } after struct declaration");
    StructDeclaration result{name.text, std::move(parameters), std::move(implementations),
                             std::move(fields), isExported(name.text), start.span, {}, {},
                             StructKind::Struct, {}};
    result.kind = service ? StructKind::Service : StructKind::Struct;
    return result;
}

void Parser::methodsDeclaration() {
    expect(TokenKind::Methods, "FDN1161", "expected methods");
    const auto owner =
        expect(TokenKind::Identifier, "FDN1162", "expected type name after methods");
    const auto parameters = typeParameters();
    expect(TokenKind::LeftBrace, "FDN1163", "expected { after methods type");
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        auto parsedAttributes = attributes(false);
        if (check(TokenKind::Extern)) {
            diagnostics_.error("FDN2117", "C ABI function cannot be a method", current().span);
            advance();
            continue;
        }
        if (!check(TokenKind::Fn) && !check(TokenKind::Action)) {
            diagnostics_.error("FDN1164", "expected method declaration", current().span);
            advance();
            continue;
        }
        const auto action = check(TokenKind::Action);
        auto declaration = method(owner.text, parameters, action);
        declaration.attributes = std::move(parsedAttributes.applications);
        program_.functions.push_back(std::move(declaration));
    }
    expect(TokenKind::RightBrace, "FDN1165", "expected } after methods block");
}

EnumDeclaration Parser::enumDeclaration() {
    const auto start = expect(TokenKind::Enum, "FDN1042", "expected enum");
    const auto name = expect(TokenKind::Identifier, "FDN1043", "expected enum name");
    auto parameters = typeParameters();
    expect(TokenKind::LeftBrace, "FDN1044", "expected { after enum name");

    std::vector<EnumVariant> variants;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        auto parsedAttributes = attributes(false);
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1045", "expected enum variant name", current().span);
            advance();
            continue;
        }
        const auto variant = advance();
        std::optional<TypeSyntax> payloadType;
        std::optional<std::string> payloadName;
        std::optional<SourceSpan> payloadNameSpan;
        if (match(TokenKind::LeftParen)) {
            if (check(TokenKind::Identifier) &&
                (peek(1).kind == TokenKind::Identifier ||
                 peek(1).kind == TokenKind::LeftBracket || peek(1).kind == TokenKind::Fn ||
                 peek(1).kind == TokenKind::Own || peek(1).kind == TokenKind::View ||
                 peek(1).kind == TokenKind::Edit || peek(1).kind == TokenKind::Star)) {
                const auto payload = advance();
                payloadName = payload.text;
                payloadNameSpan = payload.span;
            }
            payloadType = typeSyntax("FDN1048", "expected payload type");
            expect(TokenKind::RightParen, "FDN1049", "expected ) after enum payload");
        }
        variants.push_back(
            {variant.text, std::move(payloadType), isExported(variant.text), variant.span,
             std::move(parsedAttributes.applications), std::move(payloadName),
             payloadNameSpan});
    }
    expect(TokenKind::RightBrace, "FDN1050", "expected } after enum declaration");
    return {name.text, std::move(parameters), std::move(variants), isExported(name.text),
            BuiltinEnumKind::None, start.span, {}, {}};
}

void Parser::stateMachineDeclaration() {
    const auto start = expect(TokenKind::StateMachine, "FDN1190", "expected state_machine");
    const auto name =
        expect(TokenKind::Identifier, "FDN1191", "expected state machine name");
    if (check(TokenKind::Less)) {
        diagnostics_.error("FDN1192", "generic state machines are not supported yet",
                           current().span);
        static_cast<void>(typeParameters());
    }
    expect(TokenKind::LeftBrace, "FDN1193", "expected { after state machine name");

    std::vector<EnumVariant> states;
    struct PendingTransition {
        Token event;
        std::vector<Parameter> parameters;
        std::vector<Token> sources;
        Token destination;
        std::optional<Token> destinationArgument;
    };
    std::vector<PendingTransition> transitions;
    auto sawTransition = false;
    const auto contextual = [&](std::string_view keyword, const char *code,
                                const char *message) -> Token {
        const auto token = expect(TokenKind::Identifier, code, message);
        if (token.text != keyword) {
            diagnostics_.error(code, message, token.span);
        }
        return token;
    };

    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (check(TokenKind::Identifier) && current().text == "state") {
            advance();
            const auto stateToken =
                expect(TokenKind::Identifier, "FDN1194", "expected state name");
            if (sawTransition) {
                diagnostics_.error("FDN1195", "state must be declared before transitions",
                                   stateToken.span);
            }
            std::optional<TypeSyntax> payloadType;
            std::optional<std::string> payloadName;
            std::optional<SourceSpan> payloadNameSpan;
            if (match(TokenKind::LeftParen)) {
                const auto payload = expect(TokenKind::Identifier, "FDN1196",
                                            "expected state payload name");
                payloadName = payload.text;
                payloadNameSpan = payload.span;
                payloadType = typeSyntax("FDN1197", "expected state payload type");
                expect(TokenKind::RightParen, "FDN1198",
                       "expected ) after state payload");
            }
            states.push_back({stateToken.text, std::move(payloadType),
                              isExported(stateToken.text), stateToken.span, {},
                              std::move(payloadName), payloadNameSpan});
            continue;
        }

        if (check(TokenKind::Identifier) && current().text == "on") {
            advance();
            sawTransition = true;
            PendingTransition transition;
            transition.event = expect(TokenKind::Identifier, "FDN1199",
                                      "expected transition event name");
            if (match(TokenKind::LeftParen)) {
                if (!check(TokenKind::RightParen)) {
                    do {
                        transition.parameters.push_back(parameter());
                    } while (match(TokenKind::Comma));
                }
                expect(TokenKind::RightParen, "FDN1200",
                       "expected ) after transition parameters");
            }
            static_cast<void>(contextual("from", "FDN1201",
                                         "expected from in transition"));
            do {
                transition.sources.push_back(
                    expect(TokenKind::Identifier, "FDN1202", "expected source state"));
            } while (match(TokenKind::Comma));
            static_cast<void>(contextual("to", "FDN1203", "expected to in transition"));
            transition.destination =
                expect(TokenKind::Identifier, "FDN1204", "expected destination state");
            if (match(TokenKind::LeftParen)) {
                transition.destinationArgument = expect(
                    TokenKind::Identifier, "FDN1205", "expected destination payload binding");
                expect(TokenKind::RightParen, "FDN1206",
                       "expected ) after destination payload");
            }
            transitions.push_back(std::move(transition));
            continue;
        }

        diagnostics_.error("FDN1207", "expected state or on declaration", current().span);
        advance();
    }
    expect(TokenKind::RightBrace, "FDN1208", "expected } after state machine");

    if (states.empty()) {
        diagnostics_.error("FDN1209", "state machine requires at least one state", start.span);
    }

    const auto machineExported = isExported(name.text);
    EnumDeclaration machine{name.text, {}, states, machineExported, BuiltinEnumKind::None,
                            start.span, {}, {}, true};
    program_.enums.push_back(std::move(machine));

    const auto errorName = name.text + "TransitionError";
    EnumVariant invalidState{"InvalidState", std::nullopt, true, start.span, {},
                             std::nullopt, std::nullopt};
    program_.enums.push_back({errorName, {}, {std::move(invalidState)}, machineExported,
                              BuiltinEnumKind::None, start.span, {}, {}, false});

    const auto findState = [&](const Token &token) -> std::optional<std::size_t> {
        const auto found = std::find_if(states.begin(), states.end(), [&](const auto &state) {
            return state.name == token.text;
        });
        if (found == states.end()) {
            diagnostics_.error("FDN1210", "unknown state " + token.text, token.span);
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::distance(states.begin(), found));
    };

    for (auto &transition : transitions) {
        StateTransitionFunction metadata;
        for (const auto &source : transition.sources) {
            if (const auto state = findState(source); state.has_value()) {
                if (std::find(metadata.sourceVariants.begin(), metadata.sourceVariants.end(),
                              *state) != metadata.sourceVariants.end()) {
                    diagnostics_.error("FDN1211", "duplicate transition source " + source.text,
                                       source.span);
                } else {
                    metadata.sourceVariants.push_back(*state);
                }
            }
        }
        if (const auto destination = findState(transition.destination);
            destination.has_value()) {
            metadata.destinationVariant = *destination;
            const auto expectsPayload = states[*destination].payloadType.has_value();
            if (expectsPayload != transition.destinationArgument.has_value()) {
                diagnostics_.error(
                    "FDN1212",
                    expectsPayload ? "destination state requires a payload binding"
                                   : "destination state does not accept a payload",
                    transition.destination.span);
            }
        }
        if (transition.destinationArgument.has_value()) {
            const auto found = std::find_if(
                transition.parameters.begin(), transition.parameters.end(),
                [&](const auto &parameter) {
                    return parameter.name == transition.destinationArgument->text;
                });
            if (found == transition.parameters.end()) {
                diagnostics_.error("FDN1213",
                                   "unknown transition parameter " +
                                       transition.destinationArgument->text,
                                   transition.destinationArgument->span);
            } else {
                metadata.destinationParameter =
                    1 + static_cast<std::size_t>(
                            std::distance(transition.parameters.begin(), found));
            }
        }

        TypeSyntax machineType{name.text, {}, name.span};
        Parameter receiver{"self", TypeSyntax{"edit", {machineType}, name.span}, name.span, {},
                           ParameterMode::Bootstrap};
        std::vector<Parameter> parameters;
        parameters.reserve(transition.parameters.size() + 1);
        parameters.push_back(std::move(receiver));
        parameters.insert(parameters.end(), transition.parameters.begin(),
                          transition.parameters.end());
        TypeSyntax returnType{
            "Result",
            {TypeSyntax{"void", {}, transition.event.span},
             TypeSyntax{errorName, {}, transition.event.span}},
            transition.event.span};
        program_.blocks.push_back({{}, transition.event.span});

        Function function;
        function.name = transition.event.text;
        function.parameters = std::move(parameters);
        function.returnType = std::move(returnType);
        function.body = program_.blocks.size() - 1;
        function.exported = isExported(transition.event.text);
        function.span = transition.event.span;
        function.receiver = ReceiverKind::Edit;
        function.ownerType = name.text;
        function.stateTransition = std::move(metadata);
        program_.functions.push_back(std::move(function));
    }
}

void Parser::workflowDeclaration(WorkflowKind kind) {
    const auto token = kind == WorkflowKind::Pipeline ? TokenKind::Pipeline : TokenKind::Saga;
    const auto word = kind == WorkflowKind::Pipeline ? "pipeline" : "saga";
    const auto start = expect(token, "FDN1214", "expected workflow declaration");
    if (!check(TokenKind::Identifier)) {
        static_cast<void>(
            expect(TokenKind::Identifier, "FDN1215", "expected workflow name"));
        return;
    }
    const auto name = advance();
    auto typeParameters = this->typeParameters();
    expect(TokenKind::LeftParen, "FDN1216", "expected ( after workflow name");
    std::vector<Parameter> parameters;
    if (!check(TokenKind::RightParen)) {
        parameters.push_back(parameter());
        while (match(TokenKind::Comma)) {
            parameters.push_back(parameter());
        }
    }
    expect(TokenKind::RightParen, "FDN1217", "expected ) after workflow input");
    auto declaredReturn = typeSyntax("FDN1218", "expected workflow result type");
    TypeSyntax successType{"void", {}, declaredReturn.span};
    TypeSyntax errorType{"void", {}, declaredReturn.span};
    if (declaredReturn.name != "Result" || declaredReturn.arguments.size() != 2) {
        diagnostics_.error("FDN1219", "workflow must declare Result<Output, Error>",
                           declaredReturn.span);
    } else {
        successType = declaredReturn.arguments[0];
        errorType = declaredReturn.arguments[1];
    }
    expect(TokenKind::LeftBrace, "FDN1220", "expected { after workflow result type");

    std::vector<WorkflowStep> steps;
    std::vector<std::string> stepNames;
    const auto contextual = [&](std::string_view expected, const char *code,
                                const char *message) -> Token {
        const auto value = expect(TokenKind::Identifier, code, message);
        if (value.text != expected) {
            diagnostics_.error(code, message, value.span);
        }
        return value;
    };
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        const auto stepStart = current_;
        const auto step = contextual("step", "FDN1221", "expected step in workflow");
        const auto stepName =
            expect(TokenKind::Identifier, "FDN1222", "expected workflow step name");
        static_cast<void>(contextual("using", "FDN1223", "expected using after step name"));
        auto [functionName, functionSpan] =
            qualifiedName("FDN1224", "expected workflow step function");

        WorkflowStep workflowStep{stepName.text, std::move(functionName), step.span,
                                  functionSpan, 1, std::nullopt, std::nullopt};
        if (check(TokenKind::Identifier) && current().text == "retry") {
            advance();
            static_cast<void>(contextual("exponential", "FDN1225",
                                         "expected exponential retry strategy"));
            expect(TokenKind::LeftParen, "FDN1226", "expected ( after exponential");
            static_cast<void>(contextual("max", "FDN1227", "expected max retry argument"));
            expect(TokenKind::Equal, "FDN1228", "expected = after retry max");
            const auto attempts =
                expect(TokenKind::Integer, "FDN1229", "expected retry attempt count");
            std::size_t parsed{};
            const auto conversion =
                std::from_chars(attempts.text.data(),
                                attempts.text.data() + attempts.text.size(), parsed);
            if (conversion.ec != std::errc{} || parsed == 0 || parsed > 1024) {
                diagnostics_.error("FDN1230", "retry max must be between 1 and 1024",
                                   attempts.span);
            } else {
                workflowStep.attempts = parsed;
            }
            expect(TokenKind::RightParen, "FDN1231", "expected ) after retry strategy");
        }
        if (kind == WorkflowKind::Saga && check(TokenKind::Identifier) &&
            current().text == "compensate") {
            advance();
            auto [compensation, compensationSpan] =
                qualifiedName("FDN1232", "expected compensation function");
            workflowStep.compensation = std::move(compensation);
            workflowStep.compensationSpan = compensationSpan;
        }
        if (std::find(stepNames.begin(), stepNames.end(), stepName.text) != stepNames.end()) {
            diagnostics_.error("FDN1233", "duplicate workflow step " + stepName.text,
                               stepName.span);
        } else {
            stepNames.push_back(stepName.text);
        }
        steps.push_back(std::move(workflowStep));
        if (current_ == stepStart) {
            advance();
        }
    }
    expect(TokenKind::RightBrace, "FDN1234", "expected } after workflow");
    if (parameters.size() != 1) {
        diagnostics_.error("FDN1235", std::string(word) + " requires exactly one input",
                           name.span);
    }
    if (steps.empty()) {
        diagnostics_.error("FDN1236", std::string(word) + " requires at least one step",
                           name.span);
    }
    if (kind == WorkflowKind::Saga &&
        std::none_of(steps.begin(), steps.end(), [](const auto &entry) {
            return entry.compensation.has_value();
        })) {
        diagnostics_.error("FDN1237", "saga requires at least one compensation", name.span);
    }

    WorkflowFunction workflow{kind, successType, errorType, std::move(steps),
                              std::nullopt, std::nullopt};
    auto returnType = declaredReturn;
    if (kind == WorkflowKind::Saga) {
        const auto detailsName = name.text + "CompensationFailure";
        const auto failureName = name.text + "Failure";
        workflow.failureStruct = detailsName;
        workflow.failureEnum = failureName;

        TypeSyntax errorArray{"[array]", {errorType}, name.span,
                              static_cast<std::size_t>(std::count_if(
                                  workflow.steps.begin(), workflow.steps.end(),
                                  [](const auto &entry) {
                                      return entry.compensation.has_value();
                                  }))};
        std::vector<StructField> fields;
        fields.push_back({"Original", errorType, true, name.span, {}, std::nullopt,
                          std::nullopt});
        fields.push_back({"CompensationCount", TypeSyntax{"usize", {}, name.span}, true,
                          name.span, {}, std::nullopt, std::nullopt});
        fields.push_back({"CompensationErrors", std::move(errorArray), true, name.span, {},
                          std::nullopt, std::nullopt});
        program_.structs.push_back({detailsName, typeParameters, {}, std::move(fields),
                                    isExported(name.text), start.span, {}, {},
                                    StructKind::Struct, {}});

        TypeSyntax detailsType{detailsName, {}, name.span};
        TypeSyntax failureType{failureName, {}, name.span};
        for (const auto &parameterName : typeParameters) {
            detailsType.arguments.push_back(TypeSyntax{parameterName, {}, name.span});
            failureType.arguments.push_back(TypeSyntax{parameterName, {}, name.span});
        }
        std::vector<EnumVariant> variants;
        variants.push_back({"Step", errorType, true, name.span, {}, "error", name.span});
        variants.push_back(
            {"Compensation", detailsType, true, name.span, {}, "details", name.span});
        program_.enums.push_back({failureName, typeParameters, std::move(variants),
                                  isExported(name.text), BuiltinEnumKind::None, start.span,
                                  {}, {}, false});
        returnType = TypeSyntax{"Result", {successType, std::move(failureType)},
                                declaredReturn.span};
    }

    program_.blocks.push_back({{}, start.span});
    Function function;
    function.name = name.text;
    function.typeParameters = std::move(typeParameters);
    function.parameters = std::move(parameters);
    function.returnType = std::move(returnType);
    function.body = program_.blocks.size() - 1;
    function.exported = isExported(name.text);
    function.span = start.span;
    function.workflow = std::move(workflow);
    program_.functions.push_back(std::move(function));
}

ContractDeclaration Parser::contractDeclaration() {
    const auto start = expect(TokenKind::Contract, "FDN1095", "expected contract");
    const auto name = expect(TokenKind::Identifier, "FDN1096", "expected contract name");
    auto parameters = typeParameters();
    std::vector<TypeSyntax> parents;
    if (match(TokenKind::Extends)) {
        do {
            parents.push_back(typeSyntax("FDN1140", "expected contract after extends"));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::LeftBrace, "FDN1097", "expected { after contract name");

    std::vector<ContractMethod> methods;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        auto parsedAttributes = attributes(false);
        if (check(TokenKind::Extern)) {
            diagnostics_.error("FDN2117", "C ABI function cannot be a method", current().span);
            advance();
            continue;
        }
        if (!check(TokenKind::Fn)) {
            diagnostics_.error("FDN1098", "expected contract method", current().span);
            advance();
            continue;
        }
        auto declaration = contractMethod(name.text, parameters);
        declaration.attributes = std::move(parsedAttributes.applications);
        methods.push_back(std::move(declaration));
    }
    expect(TokenKind::RightBrace, "FDN1099", "expected } after contract declaration");
    return {name.text, std::move(parameters), std::move(parents), std::move(methods),
            isExported(name.text), start.span, {}, {}};
}

AttributeDeclaration Parser::attributeDeclaration() {
    const auto start = expect(TokenKind::Attribute, "FDN1150", "expected attribute");
    const auto name = expect(TokenKind::Identifier, "FDN1151", "expected attribute name");
    expect(TokenKind::LeftParen, "FDN1152", "expected ( after attribute name");
    std::vector<Parameter> parameters;
    if (!check(TokenKind::RightParen)) {
        do {
            auto parsed = parameter();
            if (!parsed.attributes.empty()) {
                diagnostics_.error("FDN1159", "attribute parameter cannot be attributed",
                                   parsed.attributes.front().span);
                parsed.attributes.clear();
            }
            parameters.push_back(std::move(parsed));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1153", "expected ) after attribute parameters");
    const auto targets = expect(TokenKind::Identifier, "FDN1154", "expected targets");
    if (targets.text != "targets") {
        diagnostics_.error("FDN1154", "expected targets after attribute parameters",
                           targets.span);
    }
    expect(TokenKind::LeftParen, "FDN1155", "expected ( after targets");
    std::vector<AttributeTarget> targetList;
    if (!check(TokenKind::RightParen)) {
        do {
            if (const auto target = attributeTarget(); target.has_value()) {
                targetList.push_back(*target);
            }
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1156", "expected ) after attribute targets");
    auto repeatable = false;
    if (check(TokenKind::Identifier) && current().text == "repeatable") {
        advance();
        repeatable = true;
    }
    return {name.text, std::move(parameters), std::move(targetList), repeatable,
            isExported(name.text), start.span, {}};
}

Function Parser::function(bool external, bool task) {
    auto start = current();
    if (external) {
        start = expect(TokenKind::Extern, "FDN1112", "expected extern");
        const auto abi = expect(TokenKind::Identifier, "FDN1113", "expected c after extern");
        if (abi.text != "c") {
            diagnostics_.error("FDN1113", "only the c external ABI is supported", abi.span);
        }
    }
    expect(task ? TokenKind::Task : TokenKind::Fn, "FDN1002",
           task ? "expected task" : "expected fn");
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
    std::optional<std::string> cSymbol;
    bool hasBody = true;
    AstBlockId body{};
    if (external) {
        expect(TokenKind::As, "FDN1114", "expected as before C symbol");
        cSymbol = expect(TokenKind::Identifier, "FDN1115", "expected C symbol after as").text;
        hasBody = check(TokenKind::LeftBrace);
    }
    if (hasBody) {
        auto previousTypeParameters = std::move(activeTypeParameters_);
        activeTypeParameters_ = typeParameters;
        body = block(tailResult);
        activeTypeParameters_ = std::move(previousTypeParameters);
    } else {
        program_.blocks.push_back({{}, start.span});
        body = program_.blocks.size() - 1;
    }
    Function result{name.text, std::move(typeParameters), std::move(parameters),
                    std::move(returnType), body, isExported(name.text), start.span, {}, {},
                    std::nullopt, {}, std::nullopt, true, false, {}, {}, false, false, false,
                    std::nullopt, std::nullopt, false, std::nullopt, std::nullopt};
    result.cSymbol = std::move(cSymbol);
    result.hasBody = hasBody;
    result.task = task;
    return result;
}

Function Parser::testDeclaration() {
    const auto start = advance();
    if (start.kind != TokenKind::Test &&
        (start.kind != TokenKind::Identifier || start.text != "test")) {
        diagnostics_.error("FDN1180", "expected test", start.span);
    }
    const auto name = expect(TokenKind::String, "FDN1181", "expected test name");
    const auto body = block();
    Function result{"$test." + std::to_string(start.span.offset), {}, {},
                    TypeSyntax{"void", {}, start.span}, body, false, start.span, {}, {},
                    std::nullopt, {}, std::nullopt, true, false, {}, {}, false, false, false,
                    name.text, std::nullopt, false, std::nullopt, std::nullopt};
    result.testNameSpan = name.span;
    return result;
}

Function Parser::method(const std::string &owner,
                        const std::vector<std::string> &typeParameters, bool action) {
    const auto start = expect(action ? TokenKind::Action : TokenKind::Fn, "FDN1100",
                              action ? "expected action" : "expected fn");
    const auto name = expect(TokenKind::Identifier, "FDN1101", "expected method name");
    expect(TokenKind::LeftParen, "FDN1102", "expected ( after method name");
    std::vector<Parameter> parameters;
    std::optional<ReceiverKind> access;
    auto parseParameters = true;
    const auto targetReceiver =
        (check(TokenKind::Identifier) && current().text == "self") ||
        ((check(TokenKind::Ampersand) || check(TokenKind::Dollar)) &&
         peek(1).kind == TokenKind::Identifier && peek(1).text == "self");
    if (check(TokenKind::View) || check(TokenKind::Edit) || check(TokenKind::Own) ||
        targetReceiver) {
        const auto receiverStart = current();
        access = receiver("FDN1103", "expected self, &self, or $self receiver");
        std::vector<TypeSyntax> ownerArguments;
        ownerArguments.reserve(typeParameters.size());
        for (const auto &parameterName : typeParameters) {
            ownerArguments.push_back({parameterName, {}, receiverStart.span});
        }
        TypeSyntax ownerType{owner, std::move(ownerArguments), receiverStart.span};
        const auto qualifier = access == ReceiverKind::View ? "view"
                               : access == ReceiverKind::Edit ? "edit"
                                                              : "own";
        parameters.push_back(
            {"self", TypeSyntax{qualifier, {std::move(ownerType)}, receiverStart.span},
             receiverStart.span, {}, ParameterMode::Bootstrap});
        parseParameters = match(TokenKind::Comma);
    }
    if (parseParameters && !check(TokenKind::RightParen)) {
        do {
            parameters.push_back(parameter());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1104", "expected ) after method parameters");
    auto returnType = typeSyntax("FDN1105", "expected method return type");
    const auto tailResult = returnType.name != "void" || !returnType.arguments.empty();
    auto previousTypeParameters = std::move(activeTypeParameters_);
    activeTypeParameters_ = typeParameters;
    const auto body = block(tailResult);
    activeTypeParameters_ = std::move(previousTypeParameters);
    Function result{name.text, typeParameters, std::move(parameters), std::move(returnType), body,
                    isExported(name.text), start.span, {}, {}, access, owner, std::nullopt, true,
                    false, {}, {}, false, false, false, std::nullopt, std::nullopt, action,
                    std::nullopt, std::nullopt};
    result.action = action;
    return result;
}

ContractMethod Parser::contractMethod(const std::string &owner,
                                      const std::vector<std::string> &typeParameters) {
    const auto start = expect(TokenKind::Fn, "FDN1106", "expected fn");
    const auto name = expect(TokenKind::Identifier, "FDN1107", "expected contract method name");
    expect(TokenKind::LeftParen, "FDN1108", "expected ( after contract method name");
    const auto access = receiver("FDN1109", "expected self, &self, or $self receiver");
    std::vector<Parameter> parameters;
    if (match(TokenKind::Comma)) {
        do {
            parameters.push_back(parameter());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1110", "expected ) after contract method parameters");
    auto returnType = typeSyntax("FDN1111", "expected contract method return type");
    std::optional<AstFunctionId> defaultFunction;
    if (check(TokenKind::LeftBrace)) {
        std::vector<TypeSyntax> ownerArguments;
        ownerArguments.reserve(typeParameters.size());
        for (const auto &parameterName : typeParameters) {
            ownerArguments.push_back({parameterName, {}, start.span});
        }
        TypeSyntax ownerType{owner, std::move(ownerArguments), start.span};
        const auto qualifier = access == ReceiverKind::View ? "view"
                               : access == ReceiverKind::Edit ? "edit"
                                                             : "own";
        std::vector<Parameter> functionParameters;
        functionParameters.reserve(parameters.size() + 1);
        functionParameters.push_back(
            {"self", TypeSyntax{qualifier, {std::move(ownerType)}, start.span}, start.span, {},
             ParameterMode::Bootstrap});
        functionParameters.insert(functionParameters.end(), parameters.begin(), parameters.end());
        const auto tailResult = returnType.name != "void" || !returnType.arguments.empty();
        auto previousTypeParameters = std::move(activeTypeParameters_);
        activeTypeParameters_ = typeParameters;
        const auto body = block(tailResult);
        activeTypeParameters_ = std::move(previousTypeParameters);
        defaultFunction = program_.functions.size();
        program_.functions.push_back(
            {name.text, typeParameters, std::move(functionParameters), returnType, body,
             isExported(name.text), start.span, {}, {}, access, owner, std::nullopt, true, false,
             {}, {}, false, false, false, std::nullopt, std::nullopt, false, std::nullopt,
             std::nullopt});
    }
    return {name.text, access, std::move(parameters), std::move(returnType),
            isExported(name.text), start.span, defaultFunction, {}};
}

ReceiverKind Parser::receiver(const char *code, const char *message) {
    if (match(TokenKind::Ampersand)) {
        const auto self = expect(TokenKind::Identifier, code, message);
        if (self.text != "self") {
            diagnostics_.error(code, message, self.span);
        }
        return ReceiverKind::Edit;
    }
    if (match(TokenKind::Dollar)) {
        const auto self = expect(TokenKind::Identifier, code, message);
        if (self.text != "self") {
            diagnostics_.error(code, message, self.span);
        }
        return ReceiverKind::Own;
    }
    if (check(TokenKind::Identifier) && current().text == "self") {
        advance();
        return ReceiverKind::View;
    }
    if (match(TokenKind::View)) {
        diagnostics_.error("FDN1185", "view receiver was removed; use self", previous().span);
        return ReceiverKind::View;
    }
    if (match(TokenKind::Edit)) {
        diagnostics_.error("FDN1185", "edit receiver was removed; use &self", previous().span);
        return ReceiverKind::Edit;
    }
    if (match(TokenKind::Own)) {
        diagnostics_.error("FDN1185", "own receiver was removed; use $self", previous().span);
        return ReceiverKind::Own;
    }
    diagnostics_.error(code, message, current().span);
    if (!atEnd()) {
        advance();
    }
    return ReceiverKind::View;
}

Parameter Parser::parameter(bool allowInferredType) {
    auto parsedAttributes = attributes(false);
    const auto start = current().span;
    auto mode = ParameterMode::Read;
    if (match(TokenKind::Ampersand)) {
        mode = ParameterMode::Edit;
    } else if (match(TokenKind::Dollar)) {
        mode = ParameterMode::Transfer;
    }
    const auto name = expect(TokenKind::Identifier, "FDN1026", "expected parameter name");
    const auto inferredType = allowInferredType &&
                              (check(TokenKind::Comma) || check(TokenKind::RightParen));
    auto type = inferredType ? TypeSyntax{"", {}, name.span}
                             : typeSyntax("FDN1028", "expected parameter type");
    if (mode == ParameterMode::Read &&
        (type.name == "own" || type.name == "view" || type.name == "edit")) {
        const auto replacement = type.name == "edit" ? "&" + name.text
                                 : type.name == "own" ? "$" + name.text
                                                       : name.text;
        diagnostics_.error("FDN1186",
                           "parameter mode after the name was removed; use " + replacement,
                           type.span);
        mode = ParameterMode::Bootstrap;
    }
    auto span = name.span;
    if (start.offset < name.span.offset) {
        span.offset = start.offset;
        span.length = name.span.offset + name.span.length - start.offset;
        span.line = start.line;
        span.column = start.column;
    }
    return {name.text, std::move(type), span, std::move(parsedAttributes.applications), mode,
            inferredType};
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
        if (tailResult && startsTailIfExpression()) {
            const auto conditionalStart = advance();
            const auto value = ifExpression(conditionalStart);
            result.statements.push_back(
                addStatement(ExpressionStatement{value}, conditionalStart.span));
        } else {
            result.statements.push_back(statement());
        }
    }
    expect(TokenKind::RightBrace, "FDN1009", "expected } after block");
    if (tailResult && !result.statements.empty()) {
        auto &last = program_.statements[result.statements.back()];
        if (const auto *expression = std::get_if<ExpressionStatement>(&last.value)) {
            last.value = ReturnStatement{expression->expression, true};
        }
    }
    --blockDepth_;
    program_.blocks.push_back(std::move(result));
    return program_.blocks.size() - 1;
}

AstStatementId Parser::statement() {
    if (check(TokenKind::Let) || check(TokenKind::Const)) {
        const auto declaration = advance();
        if (declaration.kind == TokenKind::Let) {
            diagnostics_.error("FDN1184", "let was removed; use const", declaration.span);
        }
        auto distance = std::size_t{};
        auto pattern = peek(distance).kind == TokenKind::Identifier;
        if (pattern) {
            ++distance;
            while (peek(distance).kind == TokenKind::Dot &&
                   peek(distance + 1).kind == TokenKind::Identifier) {
                distance += 2;
            }
            pattern = peek(distance).kind == TokenKind::LeftBrace;
        }
        if (pattern) {
            return structDestructureStatement(declaration);
        }
        return variableStatement(declaration, false);
    }
    if (match(TokenKind::Var)) {
        auto distance = std::size_t{};
        auto pattern = peek(distance).kind == TokenKind::Identifier;
        if (pattern) {
            ++distance;
            while (peek(distance).kind == TokenKind::Dot &&
                   peek(distance + 1).kind == TokenKind::Identifier) {
                distance += 2;
            }
            pattern = peek(distance).kind == TokenKind::LeftBrace;
        }
        if (pattern) {
            diagnostics_.error("FDN1130", "struct destructuring requires an immutable binding",
                               previous().span);
            return structDestructureStatement(previous());
        }
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
    if (match(TokenKind::For)) {
        return forStatement(previous());
    }
    if (match(TokenKind::Break)) {
        return loopJumpStatement(previous(), false);
    }
    if (match(TokenKind::Continue)) {
        return loopJumpStatement(previous(), true);
    }
    if (match(TokenKind::Select)) {
        return selectStatement(previous());
    }
    if (match(TokenKind::Unsafe)) {
        return unsafeStatement(previous());
    }
    return expressionStatement();
}

AstStatementId Parser::unsafeStatement(const Token &start) {
    return addStatement(UnsafeStatement{block(), start.leadingSafetyProof}, start.span);
}

AstStatementId Parser::structDestructureStatement(const Token &start) {
    auto [name, span] = qualifiedName("FDN1131", "expected struct pattern type");
    TypeSyntax type{std::move(name), {}, span};
    expect(TokenKind::LeftBrace, "FDN1132", "expected { after struct pattern type");
    std::vector<StructPatternField> fields;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (!check(TokenKind::Identifier)) {
            diagnostics_.error("FDN1133", "expected field in struct pattern", peek(0).span);
            advance();
            continue;
        }
        const auto field = expect(TokenKind::Identifier, "FDN1133",
                                  "expected field in struct pattern");
        auto binding = field.text;
        if (match(TokenKind::As)) {
            binding = expect(TokenKind::Identifier, "FDN1134",
                             "expected binding after as").text;
        }
        fields.push_back({field.text, std::move(binding), field.span});
        match(TokenKind::Comma);
    }
    expect(TokenKind::RightBrace, "FDN1135", "expected } after struct pattern");
    expect(TokenKind::Equal, "FDN1136", "expected = before destructuring initializer");
    return addStatement(
        StructDestructureStatement{std::move(type), std::move(fields), expression()},
        start.span);
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
        if (check(TokenKind::Identifier)) {
            elseBinding = advance().text;
        } else if (!check(TokenKind::LeftBrace)) {
            diagnostics_.error("FDN1066", "expected error binding or { after else",
                               current().span);
        }
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
    if (!check(TokenKind::LeftBrace)) {
        const auto sameLine = current().span.line == start.span.line;
        const auto jump = check(TokenKind::Return) || check(TokenKind::Break) ||
                          check(TokenKind::Continue);
        if (!sameLine || !jump) {
            diagnostics_.error(
                "FDN1173",
                "short if requires return, break, or continue on the same line",
                current().span);
        }
        AstStatementId guarded;
        SourceSpan guardedSpan;
        if (match(TokenKind::Break)) {
            guardedSpan = previous().span;
            guarded = loopJumpStatement(previous(), false);
        } else if (match(TokenKind::Continue)) {
            guardedSpan = previous().span;
            guarded = loopJumpStatement(previous(), true);
        } else {
            const auto returned = expect(
                TokenKind::Return, "FDN1173",
                "short if requires return, break, or continue on the same line");
            guardedSpan = returned.span;
            guarded = returnStatement(returned);
        }
        Block body{{guarded}, guardedSpan};
        program_.blocks.push_back(std::move(body));
        return addStatement(IfStatement{condition, program_.blocks.size() - 1, std::nullopt},
                            start.span);
    }
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

AstStatementId Parser::forStatement(const Token &start) {
    auto editable = match(TokenKind::Ampersand);
    const auto first = expect(TokenKind::Identifier, "FDN1180",
                              "expected loop binding after for");
    std::optional<std::string> indexBinding;
    auto valueBinding = first.text;
    if (match(TokenKind::Comma)) {
        if (editable) {
            diagnostics_.error("FDN1181", "loop index cannot be editable", first.span);
        }
        indexBinding = first.text;
        editable = match(TokenKind::Ampersand);
        valueBinding = expect(TokenKind::Identifier, "FDN1182",
                              "expected value binding after comma")
                           .text;
    }
    expect(TokenKind::In, "FDN1183", "expected in after loop binding");
    const auto allowed = structLiteralsAllowed_;
    structLiteralsAllowed_ = false;
    const auto sequence = expression();
    structLiteralsAllowed_ = allowed;
    const auto body = block();
    return addStatement(ForStatement{std::move(indexBinding), std::move(valueBinding), editable,
                                     sequence, body},
                        start.span);
}

AstStatementId Parser::loopJumpStatement(const Token &start, bool continues) {
    if (!check(TokenKind::RightBrace) && !atEnd() && current().span.line == start.span.line) {
        diagnostics_.error("FDN1179", std::string(continues ? "continue" : "break") +
                                          " does not accept a value",
                           current().span);
    }
    return continues ? addStatement(ContinueStatement{}, start.span)
                     : addStatement(BreakStatement{}, start.span);
}

AstBlockId Parser::selectArmBlock() {
    if (check(TokenKind::LeftBrace)) {
        return block();
    }
    const auto span = current().span;
    Block result{{statement()}, span};
    program_.blocks.push_back(std::move(result));
    return program_.blocks.size() - 1;
}

AstStatementId Parser::selectStatement(const Token &start) {
    expect(TokenKind::LeftBrace, "FDN1140", "expected { after select");
    std::vector<SelectOperationArm> operations;
    std::optional<SelectTimeoutArm> timeout;
    std::optional<std::string> errorBinding;
    std::optional<AstBlockId> errorBlock;

    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (match(TokenKind::Timeout)) {
            const auto timeoutToken = previous();
            const auto amount = expect(TokenKind::Integer, "FDN1141",
                                       "expected timeout duration");
            expect(TokenKind::Dot, "FDN1142", "expected timeout duration unit");
            const auto unit = expect(TokenKind::Identifier, "FDN1143",
                                     "expected timeout duration unit");
            std::uint64_t magnitude{};
            const auto conversion = std::from_chars(
                amount.text.data(), amount.text.data() + amount.text.size(), magnitude);
            if (conversion.ec != std::errc{} ||
                conversion.ptr != amount.text.data() + amount.text.size()) {
                diagnostics_.error("FDN1016", "integer is outside the supported range",
                                   amount.span);
            }
            std::uint64_t multiplier{};
            if (unit.text == "seconds") {
                multiplier = UINT64_C(1000000000);
            } else if (unit.text == "milliseconds") {
                multiplier = UINT64_C(1000000);
            } else if (unit.text == "microseconds") {
                multiplier = UINT64_C(1000);
            } else if (unit.text == "nanoseconds") {
                multiplier = 1;
            } else {
                diagnostics_.error("FDN1144", "unknown timeout duration unit " + unit.text,
                                   unit.span);
            }
            std::uint64_t nanoseconds{};
            if (multiplier != 0 && magnitude > UINT64_MAX / multiplier) {
                diagnostics_.error("FDN1145", "timeout duration is outside the supported range",
                                   amount.span);
                nanoseconds = UINT64_MAX;
            } else {
                nanoseconds = magnitude * multiplier;
            }
            expect(TokenKind::Colon, "FDN1146", "expected : after timeout duration");
            const auto body = selectArmBlock();
            if (timeout.has_value()) {
                diagnostics_.error("FDN1147", "select accepts one timeout branch",
                                   timeoutToken.span);
            } else {
                timeout = SelectTimeoutArm{nanoseconds, body, timeoutToken.span};
            }
            continue;
        }
        if (match(TokenKind::Else)) {
            const auto elseToken = previous();
            const auto binding = expect(TokenKind::Identifier, "FDN1148",
                                        "expected error binding after else");
            expect(TokenKind::Colon, "FDN1149", "expected : after select error binding");
            const auto body = selectArmBlock();
            if (errorBlock.has_value()) {
                diagnostics_.error("FDN1150", "select accepts one error branch",
                                   elseToken.span);
            } else {
                errorBinding = binding.text;
                errorBlock = body;
            }
            continue;
        }

        std::optional<std::string> binding;
        auto armSpan = current().span;
        if (match(TokenKind::Const)) {
            armSpan = previous().span;
            binding = expect(TokenKind::Identifier, "FDN1151",
                             "expected select binding name").text;
            expect(TokenKind::Equal, "FDN1152", "expected = before select operation");
        }
        const auto operation = expression();
        expect(TokenKind::Colon, "FDN1153", "expected : after select operation");
        operations.push_back(
            {std::move(binding), operation, selectArmBlock(), armSpan});
    }
    expect(TokenKind::RightBrace, "FDN1154", "expected } after select");
    if (operations.empty()) {
        diagnostics_.error("FDN1155", "select requires at least one channel operation",
                           start.span);
    }
    if (!errorBlock.has_value()) {
        diagnostics_.error("FDN1156", "select requires an else error branch", start.span);
        Block empty{{}, start.span};
        program_.blocks.push_back(std::move(empty));
        errorBlock = program_.blocks.size() - 1;
        errorBinding = "$selectError";
    }
    return addStatement(SelectStatement{std::move(operations), std::move(timeout),
                                        std::move(*errorBinding), *errorBlock},
                        start.span);
}

AstStatementId Parser::expressionStatement() {
    const auto start = current().span;
    const auto value = expression();
    if (match(TokenKind::Equal)) {
        return addStatement(AssignmentStatement{value, expression()}, start);
    }
    if (match(TokenKind::Else)) {
        std::optional<std::string> binding;
        if (check(TokenKind::Identifier)) {
            binding = advance().text;
        } else if (!check(TokenKind::LeftBrace)) {
            diagnostics_.error("FDN1214", "expected error binding or { after else",
                               current().span);
        }
        return addStatement(ResultElseStatement{value, std::move(binding), block()}, start);
    }
    return addStatement(ExpressionStatement{value}, start);
}

AstExpressionId Parser::expression() {
    const auto root = expressionCalls_ == 0;
    if (root) {
        expressionNodes_ = 0;
        expressionLimitReported_ = false;
    }
    ++expressionCalls_;
    const auto result = conditional();
    --expressionCalls_;
    return result;
}

AstExpressionId Parser::conditional() {
    const auto value = logicalOr();
    if (!continuesLine() || !match(TokenKind::If)) {
        return value;
    }

    const auto start = previous();
    const auto condition = logicalOr();
    expect(TokenKind::Else, "FDN1174", "expected else in postfix conditional expression");
    const auto fallback = conditional();
    program_.blocks.push_back({{}, start.span});
    const auto thenBlock = program_.blocks.size() - 1;
    program_.blocks.push_back({{}, start.span});
    const auto elseBlock = program_.blocks.size() - 1;
    return addExpression(
        ConditionalExpression{condition, thenBlock, value, elseBlock, fallback}, start.span);
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
        return addExpression(IntegerExpression{0, false}, bad.span);
    }

    ++expressionDepth_;
    AstExpressionId result;
    if (match(TokenKind::Minus)) {
        const auto start = previous().span;
        const auto operand = unary();
        if (const auto *integer =
                std::get_if<IntegerExpression>(&program_.expressions[operand].value)) {
            result = addExpression(
                IntegerExpression{integer->magnitude, !integer->negative}, start);
        } else if (const auto *floating =
                       std::get_if<FloatingExpression>(&program_.expressions[operand].value)) {
            auto text = floating->text;
            if (!text.empty() && text.front() == '-') {
                text.erase(text.begin());
            } else {
                text.insert(text.begin(), '-');
            }
            result = addExpression(FloatingExpression{std::move(text)}, start);
        } else {
            result = addExpression(UnaryExpression{UnaryOperator::Negate, operand}, start);
        }
    } else if (match(TokenKind::Bang)) {
        const auto start = previous().span;
        result = addExpression(UnaryExpression{UnaryOperator::Not, unary()}, start);
    } else if (match(TokenKind::Star)) {
        const auto start = previous().span;
        result = addExpression(UnaryExpression{UnaryOperator::Dereference, unary()}, start);
    } else if (match(TokenKind::Dollar)) {
        const auto start = previous().span;
        result = addExpression(OwnershipExpression{OwnershipOperator::Transfer, unary()}, start);
    } else if (match(TokenKind::Ampersand)) {
        const auto start = previous().span;
        result = addExpression(OwnershipExpression{OwnershipOperator::Edit, unary()}, start);
    } else if (match(TokenKind::New)) {
        const auto start = previous().span;
        result = addExpression(OwnershipExpression{OwnershipOperator::New, unary()}, start);
    } else if (match(TokenKind::Spawn)) {
        const auto start = previous().span;
        result = addExpression(SpawnExpression{unary()}, start);
    } else if (check(TokenKind::Own) || check(TokenKind::View) || check(TokenKind::Edit)) {
        const auto token = advance();
        auto operation = OwnershipOperator::Own;
        if (token.kind == TokenKind::View) {
            diagnostics_.error("FDN1187", "view expression was removed; pass the value directly",
                               token.span);
            operation = OwnershipOperator::View;
        } else if (token.kind == TokenKind::Edit) {
            diagnostics_.error("FDN1187", "edit expression was removed; use &value",
                               token.span);
            operation = OwnershipOperator::Edit;
        }
        result = addExpression(OwnershipExpression{operation, unary()}, token.span);
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
        std::uint64_t magnitude{};
        const auto conversion = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(), magnitude);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != token.text.data() + token.text.size()) {
            diagnostics_.error("FDN1016", "integer is outside the supported range", token.span);
        }
        result = addExpression(IntegerExpression{magnitude, false}, token.span);
    } else if (match(TokenKind::Floating)) {
        const auto token = previous();
        double value{};
        const auto conversion = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(), value);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != token.text.data() + token.text.size()) {
            diagnostics_.error("FDN1016", "floating-point literal is outside the supported range",
                               token.span);
        }
        result = addExpression(FloatingExpression{token.text}, token.span);
    } else if (match(TokenKind::True)) {
        result = addExpression(BooleanExpression{true}, previous().span);
    } else if (match(TokenKind::False)) {
        result = addExpression(BooleanExpression{false}, previous().span);
    } else if (match(TokenKind::String)) {
        const auto token = previous();
        result = addExpression(StringExpression{token.text}, token.span);
    } else if (match(TokenKind::LeftBracket)) {
        result = finishArray(previous());
    } else if (match(TokenKind::Match)) {
        result = matchExpression(previous());
    } else if (match(TokenKind::If)) {
        result = ifExpression(previous());
    } else if (match(TokenKind::Fn)) {
        result = functionExpression(previous());
    } else if (match(TokenKind::Replace)) {
        result = replaceExpression(previous());
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
                   (peek(1).kind == TokenKind::RightBrace ||
                    (peek(1).kind == TokenKind::Identifier &&
                     peek(2).kind == TokenKind::Equal))) {
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
        result = addExpression(IntegerExpression{0, false}, bad.span);
    }

    while (continuesLine()) {
        if (match(TokenKind::Dot)) {
            result = finishMember(result);
            continue;
        }
        if (match(TokenKind::LeftBracket)) {
            const auto start = previous().span;
            const auto index = expression();
            expect(TokenKind::RightBracket, "FDN1084", "expected ] after index");
            result = addExpression(IndexExpression{result, index}, start);
            continue;
        }
        break;
    }
    if (structLiteralsAllowed_ && check(TokenKind::LeftBrace) &&
        (peek(1).kind == TokenKind::RightBrace ||
         (peek(1).kind == TokenKind::Identifier && peek(2).kind == TokenKind::Equal))) {
        const auto *member = std::get_if<MemberExpression>(&program_.expressions[result].value);
        if (member != nullptr && member->base.has_value() && !member->invoked) {
            const auto *base =
                std::get_if<NameExpression>(&program_.expressions[*member->base].value);
            if (base != nullptr && base->typeArguments.empty()) {
                TypeSyntax type{base->name + '.' + member->member, member->typeArguments,
                                program_.expressions[*member->base].span};
                advance();
                result = finishStruct(std::move(type));
            }
        }
    }
    return result;
}

AstExpressionId Parser::replaceExpression(const Token &start) {
    const auto target = primary();
    expect(TokenKind::With, "FDN1137", "expected with after replacement place");
    return addExpression(ReplaceExpression{target, expression()}, start.span);
}

AstExpressionId Parser::finishCall(const Token &callee, std::vector<TypeSyntax> typeArguments) {
    std::vector<AstExpressionId> arguments;
    std::vector<std::optional<std::string>> argumentNames;
    std::vector<std::optional<SourceSpan>> argumentNameSpans;
    if (!check(TokenKind::RightParen)) {
        do {
            std::optional<std::string> name;
            std::optional<SourceSpan> nameSpan;
            if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Equal) {
                const auto token = advance();
                name = token.text;
                nameSpan = token.span;
                advance();
            }
            argumentNames.push_back(std::move(name));
            argumentNameSpans.push_back(nameSpan);
            arguments.push_back(expression());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1024", "expected ) after call arguments");
    return addExpression(
        CallExpression{callee.text, std::move(typeArguments), std::move(arguments),
                       std::move(argumentNames), std::move(argumentNameSpans)},
        callee.span);
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
    std::vector<TypeSyntax> typeArguments;
    if (startsGenericPrimary()) {
        expect(TokenKind::Less, "FDN1063", "expected < before type arguments");
        do {
            typeArguments.push_back(typeSyntax("FDN1063", "expected type argument"));
        } while (match(TokenKind::Comma));
        expect(TokenKind::Greater, "FDN1064", "expected > after type arguments");
    }
    auto invoked = false;
    std::vector<AstExpressionId> arguments;
    std::vector<std::optional<std::string>> argumentNames;
    std::vector<std::optional<SourceSpan>> argumentNameSpans;
    if (match(TokenKind::LeftParen)) {
        invoked = true;
        if (!check(TokenKind::RightParen)) {
            do {
                std::optional<std::string> name;
                std::optional<SourceSpan> nameSpan;
                if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Equal) {
                    const auto token = advance();
                    name = token.text;
                    nameSpan = token.span;
                    advance();
                }
                argumentNames.push_back(std::move(name));
                argumentNameSpans.push_back(nameSpan);
                arguments.push_back(expression());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "FDN1052", "expected ) after member invocation");
    }
    return addExpression(MemberExpression{base, member.text, std::move(typeArguments), invoked,
                                          std::move(arguments), std::move(argumentNames),
                                          std::move(argumentNameSpans)},
                         member.span);
}

AstExpressionId Parser::finishArray(const Token &start) {
    std::vector<AstExpressionId> elements;
    if (!check(TokenKind::RightBracket)) {
        do {
            elements.push_back(expression());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightBracket, "FDN1085", "expected ] after array literal");
    return addExpression(ArrayExpression{std::move(elements)}, start.span);
}

AstExpressionId Parser::matchExpression(const Token &start) {
    const auto value = expression();
    expect(TokenKind::LeftBrace, "FDN1053", "expected { after match value");
    std::vector<MatchArm> arms;
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        const auto variant = expect(TokenKind::Identifier, "FDN1056", "expected pattern variant");
        const auto wildcard = variant.text == "_";
        std::optional<std::string> binding;
        std::optional<AstExpressionId> pattern;
        if (match(TokenKind::LeftParen)) {
            if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::RightParen) {
                binding = advance().text;
            } else {
                pattern = expression();
            }
            expect(TokenKind::RightParen, "FDN1058", "expected ) after payload pattern");
        }
        std::optional<AstExpressionId> guard;
        if (match(TokenKind::If)) {
            guard = expression();
        }
        expect(TokenKind::Colon, "FDN1059", "expected : after match pattern");
        arms.push_back({wildcard, variant.text, std::move(binding), pattern, guard, expression(),
                        variant.span});
    }
    expect(TokenKind::RightBrace, "FDN1060", "expected } after match expression");
    return addExpression(MatchExpression{value, std::move(arms)}, start.span);
}

AstExpressionId Parser::ifExpression(const Token &start) {
    const auto allowed = structLiteralsAllowed_;
    structLiteralsAllowed_ = false;
    const auto condition = expression();
    structLiteralsAllowed_ = allowed;
    auto [thenBlock, thenValue] = expressionBlock();
    expect(TokenKind::Else, "FDN1175", "expected else after conditional expression branch");
    auto [elseBlock, elseValue] = expressionBlock();
    return addExpression(
        ConditionalExpression{condition, thenBlock, thenValue, elseBlock, elseValue},
        start.span);
}

std::pair<AstBlockId, AstExpressionId> Parser::expressionBlock() {
    const auto start = expect(TokenKind::LeftBrace, "FDN1176",
                              "expected { before conditional expression branch");
    if (blockDepth_ >= maxBlockDepth) {
        const auto skipped = skipNestedBlock(start.span);
        return {skipped, addExpression(IntegerExpression{0, false}, start.span)};
    }

    ++blockDepth_;
    Block result{{}, start.span};
    while (!check(TokenKind::RightBrace) && !atEnd()) {
        if (startsTailIfExpression()) {
            const auto conditionalStart = advance();
            const auto value = ifExpression(conditionalStart);
            result.statements.push_back(
                addStatement(ExpressionStatement{value}, conditionalStart.span));
        } else {
            result.statements.push_back(statement());
        }
    }
    expect(TokenKind::RightBrace, "FDN1177",
           "expected } after conditional expression branch");

    std::optional<AstExpressionId> value;
    if (!result.statements.empty()) {
        const auto statement = result.statements.back();
        if (const auto *expression =
                std::get_if<ExpressionStatement>(&program_.statements[statement].value)) {
            value = expression->expression;
            result.statements.pop_back();
        }
    }
    if (!value.has_value()) {
        diagnostics_.error("FDN1178", "conditional expression branch requires a final value",
                           start.span);
        value = addExpression(IntegerExpression{0, false}, start.span);
    }

    --blockDepth_;
    program_.blocks.push_back(std::move(result));
    return {program_.blocks.size() - 1, *value};
}

AstExpressionId Parser::functionExpression(const Token &start) {
    expect(TokenKind::LeftParen, "FDN1124", "expected ( after fn");
    std::vector<Parameter> parameters;
    if (!check(TokenKind::RightParen)) {
        do {
            parameters.push_back(parameter(true));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "FDN1125", "expected ) after closure parameters");
    const auto inferredReturn = check(TokenKind::Capture) || check(TokenKind::LeftBrace);
    auto returnType = inferredReturn ? TypeSyntax{"", {}, current().span}
                                     : typeSyntax("FDN1126", "expected closure return type");

    std::vector<Capture> captures;
    if (match(TokenKind::Capture)) {
        expect(TokenKind::LeftParen, "FDN1189", "expected ( after capture");
        do {
            auto mode = CaptureMode::Copy;
            SourceSpan span = current().span;
            if (match(TokenKind::Dollar)) {
                mode = CaptureMode::Own;
                span = previous().span;
            } else if (match(TokenKind::Ampersand)) {
                mode = CaptureMode::Edit;
                span = previous().span;
            } else if (match(TokenKind::Own)) {
                diagnostics_.error("FDN1188", "own capture was removed; use $name",
                                   previous().span);
                mode = CaptureMode::Own;
                span = previous().span;
            } else if (match(TokenKind::View)) {
                diagnostics_.error("FDN1188", "view capture was removed; use the name directly",
                                   previous().span);
                mode = CaptureMode::View;
                span = previous().span;
            } else if (match(TokenKind::Edit)) {
                diagnostics_.error("FDN1188", "edit capture was removed; use &name",
                                   previous().span);
                mode = CaptureMode::Edit;
                span = previous().span;
            }
            const auto name = expect(TokenKind::Identifier, "FDN1127",
                                     "expected captured binding");
            captures.push_back({mode, name.text, span});
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "FDN1129", "expected ) after capture list");
    }

    const auto tailResult = inferredReturn || returnType.name != "void" ||
                            !returnType.arguments.empty();
    const auto body = block(tailResult);
    Function function;
    function.name = "$closure_" + std::to_string(program_.functions.size());
    function.typeParameters = activeTypeParameters_;
    function.parameters = std::move(parameters);
    function.returnType = std::move(returnType);
    function.body = body;
    function.span = start.span;
    function.closure = true;
    function.captures = std::move(captures);
    function.inferredReturn = inferredReturn;
    program_.functions.push_back(std::move(function));
    return addExpression(FunctionExpression{program_.functions.size() - 1}, start.span);
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
        {{"None", std::nullopt, true, span, {}, {}, {}},
         {"Some", optionValue, true, span, {}, "value", span}},
        true,
        BuiltinEnumKind::Option,
        span,
        {},
        {},
    });

    const TypeSyntax resultValue{"T", {}, span};
    const TypeSyntax resultError{"E", {}, span};
    program_.enums.push_back({
        "Result",
        {"T", "E"},
        {{"Ok", resultValue, true, span, {}, "value", span},
         {"Err", resultError, true, span, {}, "error", span}},
        true,
        BuiltinEnumKind::Result,
        span,
        {},
        {},
    });

    program_.enums.push_back({
        "ChannelError",
        {},
        {{"Closed", std::nullopt, true, span, {}, {}, {}},
         {"Cancelled", std::nullopt, true, span, {}, {}, {}},
         {"Timeout", std::nullopt, true, span, {}, {}, {}}},
        true,
        BuiltinEnumKind::ChannelError,
        span,
        {},
        {},
    });

    program_.enums.push_back({
        "NumberError",
        {},
        {{"OutOfRange", std::nullopt, true, span, {}, {}, {}},
         {"NonFinite", std::nullopt, true, span, {}, {}, {}},
         {"PrecisionLoss", std::nullopt, true, span, {}, {}, {}}},
        true,
        BuiltinEnumKind::NumberError,
        span,
        {},
        {},
    });
}

} // namespace foundation
