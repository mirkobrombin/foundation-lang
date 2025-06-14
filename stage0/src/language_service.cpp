#include "foundation/language_service.hpp"

#include "foundation/driver.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace foundation {

namespace {

bool identifierByte(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

std::string shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return std::string(name.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

SourceSpan identifierSpan(const ProjectAnalysis &analysis, SourceSpan anchor,
                          std::string_view name, std::size_t startOffset = 0) {
    if (anchor.source >= analysis.sources.size()) {
        return anchor;
    }
    name = name.substr(name.rfind('.') == std::string_view::npos ? 0 : name.rfind('.') + 1);
    const auto &source = analysis.sources[anchor.source].contents;
    auto offset = std::max(anchor.offset, startOffset);
    const auto limit = std::min(source.size(), offset + 1024);
    while (offset < limit) {
        offset = source.find(name, offset);
        if (offset == std::string_view::npos || offset >= limit) {
            break;
        }
        const auto end = offset + name.size();
        const auto left = offset == 0 ||
                          !identifierByte(static_cast<unsigned char>(source[offset - 1]));
        const auto right = end == source.size() ||
                           !identifierByte(static_cast<unsigned char>(source[end]));
        if (left && right) {
            return {offset, name.size(), anchor.line, anchor.column, anchor.source};
        }
        ++offset;
    }
    return anchor;
}

std::string displayTypeSyntax(const TypeSyntax &type) {
    if (type.name == "[array]" && type.arguments.size() == 1) {
        return '[' + std::to_string(type.arrayLength) + ']' + displayTypeSyntax(type.arguments[0]);
    }
    if (type.name == "[slice]" && type.arguments.size() == 1) {
        return '[' + displayTypeSyntax(type.arguments[0]) + ']';
    }
    if (type.name == "[function]" && !type.arguments.empty()) {
        std::string result = "fn(";
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            if (index != 1) {
                result += ", ";
            }
            result += displayTypeSyntax(type.arguments[index]);
        }
        result += ") " + displayTypeSyntax(type.arguments[0]);
        return result;
    }
    if ((type.name == "own" || type.name == "view" || type.name == "edit") &&
        type.arguments.size() == 1) {
        return type.name + ' ' + displayTypeSyntax(type.arguments[0]);
    }
    std::string result = shortName(type.name);
    if (type.arguments.empty()) {
        return result;
    }
    result += '<';
    for (std::size_t index = 0; index < type.arguments.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += displayTypeSyntax(type.arguments[index]);
    }
    result += '>';
    return result;
}

std::string typeParameterSuffix(const std::vector<std::string> &parameters) {
    if (parameters.empty()) {
        return {};
    }
    std::string result = "<";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += parameters[index];
    }
    result += '>';
    return result;
}

std::string functionDetail(const Function &function) {
    std::string result = "fn " + shortName(function.name) +
                         typeParameterSuffix(function.typeParameters) + '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        const auto &parameter = function.parameters[index];
        if (function.receiver.has_value() && index == 0) {
            result += *function.receiver == ReceiverKind::View ? "view"
                      : *function.receiver == ReceiverKind::Edit ? "edit"
                                                                  : "own";
        } else {
            result += parameter.name + ' ' + displayTypeSyntax(parameter.type);
        }
    }
    result += ") " + displayTypeSyntax(function.returnType);
    return result;
}

std::string contractMethodDetail(const ContractMethod &method) {
    std::string result = "fn " + method.name + '(';
    result += method.receiver == ReceiverKind::View ? "view"
              : method.receiver == ReceiverKind::Edit ? "edit"
                                                       : "own";
    for (const auto &parameter : method.parameters) {
        result += ", " + parameter.name + ' ' + displayTypeSyntax(parameter.type);
    }
    result += ") " + displayTypeSyntax(method.returnType);
    return result;
}

bool sameId(const LanguageSymbolId &left, const LanguageSymbolId &right) {
    return left == right;
}

bool idLess(const LanguageSymbolId &left, const LanguageSymbolId &right) {
    if (left.kind != right.kind) {
        return left.kind < right.kind;
    }
    if (left.owner != right.owner) {
        return left.owner < right.owner;
    }
    return left.member < right.member;
}

class IndexBuilder {
  public:
    explicit IndexBuilder(const ProjectAnalysis &analysis)
        : analysis_(analysis), semantic_(analysis.semantic ? &*analysis.semantic : nullptr),
          expressionOwners_(analysis.program.expressions.size()),
          statementOwners_(analysis.program.statements.size()) {
        if (semantic_ != nullptr) {
            localSymbols_.resize(semantic_->functions.size());
            localAliases_.resize(semantic_->functions.size());
            for (std::size_t function = 0; function < semantic_->functions.size(); ++function) {
                localSymbols_[function].resize(semantic_->functions[function].locals.size());
                localAliases_[function].resize(semantic_->functions[function].locals.size());
            }
        }
    }

    LanguageIndex build() {
        assignOwners();
        addDeclarations();
        if (semantic_ != nullptr) {
            addLocalDeclarations();
            addTypeLinks();
            bindCaptures();
            addStatementReferences();
            addExpressionReferences();
        }
        addDeclarationTypeReferences();
        finish();
        return LanguageIndex(std::move(symbols_), std::move(occurrences_),
                             std::move(calls_), std::move(typeLinks_));
    }

  private:
    const DiagnosticSource *source(SourceSpan span) const {
        return span.source < analysis_.sources.size() ? &analysis_.sources[span.source]
                                                      : nullptr;
    }

    bool standardSource(SourceSpan span) const {
        const auto *value = source(span);
        return value != nullptr && (value->path == "std" || value->path.starts_with("std/"));
    }

    void addSymbol(LanguageSymbol symbol, bool addDefinition = true) {
        if (symbol.definition.source >= analysis_.sources.size()) {
            return;
        }
        if (addDefinition) {
            occurrences_.push_back({symbol.id, symbol.definition, true});
        }
        symbols_.push_back(std::move(symbol));
    }

    void addOccurrence(LanguageSymbolId symbol, SourceSpan span) {
        if (span.source >= analysis_.sources.size() || span.length == 0) {
            return;
        }
        occurrences_.push_back({symbol, span, false});
    }

    const LanguageSymbol *symbol(LanguageSymbolId id) const {
        const auto found = std::find_if(symbols_.begin(), symbols_.end(),
                                        [id](const auto &value) { return value.id == id; });
        return found == symbols_.end() ? nullptr : &*found;
    }

    void addNamedOccurrence(LanguageSymbolId id, SourceSpan anchor,
                            std::size_t startOffset = 0) {
        const auto *value = symbol(id);
        if (value != nullptr) {
            addOccurrence(id, identifierSpan(analysis_, anchor, value->name, startOffset));
        }
    }

    void assignOwners() {
        for (std::size_t function = 0; function < analysis_.program.functions.size(); ++function) {
            const auto &declaration = analysis_.program.functions[function];
            if (declaration.hasBody && declaration.body < analysis_.program.blocks.size()) {
                visitBlock(declaration.body, function);
            }
        }
    }

    void visitBlock(AstBlockId id, std::size_t function) {
        if (id >= analysis_.program.blocks.size()) {
            return;
        }
        for (const auto statement : analysis_.program.blocks[id].statements) {
            visitStatement(statement, function);
        }
    }

    void visitStatement(AstStatementId id, std::size_t function) {
        if (id >= analysis_.program.statements.size()) {
            return;
        }
        statementOwners_[id] = function;
        const auto &value = analysis_.program.statements[id].value;
        if (const auto *variable = std::get_if<VariableStatement>(&value)) {
            visitExpression(variable->initializer, function);
            if (variable->elseBlock.has_value()) {
                visitBlock(*variable->elseBlock, function);
            }
        } else if (const auto *destructure = std::get_if<StructDestructureStatement>(&value)) {
            visitExpression(destructure->initializer, function);
        } else if (const auto *assignment = std::get_if<AssignmentStatement>(&value)) {
            visitExpression(assignment->target, function);
            visitExpression(assignment->value, function);
        } else if (const auto *expression = std::get_if<ExpressionStatement>(&value)) {
            visitExpression(expression->expression, function);
        } else if (const auto *returned = std::get_if<ReturnStatement>(&value)) {
            if (returned->value.has_value()) {
                visitExpression(*returned->value, function);
            }
        } else if (const auto *discarded = std::get_if<DiscardStatement>(&value)) {
            visitExpression(discarded->value, function);
        } else if (const auto *branch = std::get_if<IfStatement>(&value)) {
            visitExpression(branch->condition, function);
            visitBlock(branch->thenBlock, function);
            if (branch->elseBlock.has_value()) {
                visitBlock(*branch->elseBlock, function);
            }
        } else if (const auto *loop = std::get_if<WhileStatement>(&value)) {
            visitExpression(loop->condition, function);
            visitBlock(loop->body, function);
        }
    }

    void visitExpression(AstExpressionId id, std::size_t function) {
        if (id >= analysis_.program.expressions.size()) {
            return;
        }
        expressionOwners_[id] = function;
        const auto &value = analysis_.program.expressions[id].value;
        if (const auto *array = std::get_if<ArrayExpression>(&value)) {
            for (const auto element : array->elements) {
                visitExpression(element, function);
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&value)) {
            visitExpression(unary->operand, function);
        } else if (const auto *ownership = std::get_if<OwnershipExpression>(&value)) {
            visitExpression(ownership->operand, function);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&value)) {
            visitExpression(binary->left, function);
            visitExpression(binary->right, function);
        } else if (const auto *call = std::get_if<CallExpression>(&value)) {
            for (const auto argument : call->arguments) {
                visitExpression(argument, function);
            }
        } else if (const auto *literal = std::get_if<StructExpression>(&value)) {
            for (const auto &field : literal->fields) {
                visitExpression(field.value, function);
            }
        } else if (const auto *member = std::get_if<MemberExpression>(&value)) {
            if (member->base.has_value()) {
                visitExpression(*member->base, function);
            }
            for (const auto argument : member->arguments) {
                visitExpression(argument, function);
            }
        } else if (const auto *index = std::get_if<IndexExpression>(&value)) {
            visitExpression(index->base, function);
            visitExpression(index->index, function);
        } else if (const auto *replace = std::get_if<ReplaceExpression>(&value)) {
            visitExpression(replace->target, function);
            visitExpression(replace->value, function);
        } else if (const auto *match = std::get_if<MatchExpression>(&value)) {
            visitExpression(match->value, function);
            for (const auto &arm : match->arms) {
                visitExpression(arm.expression, function);
            }
        }
    }

    bool contractOwner(std::string_view name) const {
        return std::any_of(analysis_.program.contracts.begin(), analysis_.program.contracts.end(),
                           [name](const auto &contract) { return contract.name == name; });
    }

    void addDeclarations() {
        for (std::size_t id = 0; id < analysis_.program.structs.size(); ++id) {
            const auto &declaration = analysis_.program.structs[id];
            const LanguageSymbolId symbol{LanguageSymbolKind::Struct, id, 0};
            typeSymbols_[declaration.name] = symbol;
            addSymbol({symbol, shortName(declaration.name),
                       "struct " + shortName(declaration.name) +
                           typeParameterSuffix(declaration.typeParameters),
                       "type:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span)});
            for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                const auto &value = declaration.fields[field];
                const LanguageSymbolId fieldSymbol{LanguageSymbolKind::Field, id, field};
                addSymbol({fieldSymbol, value.name,
                           value.name + ' ' + displayTypeSyntax(value.type),
                           "field:" + std::to_string(id), value.span,
                           !standardSource(value.span)});
            }
        }
        for (std::size_t id = 0; id < analysis_.program.enums.size(); ++id) {
            const auto &declaration = analysis_.program.enums[id];
            if (declaration.builtin != BuiltinEnumKind::None) {
                continue;
            }
            const LanguageSymbolId symbol{LanguageSymbolKind::Enum, id, 0};
            typeSymbols_[declaration.name] = symbol;
            addSymbol({symbol, shortName(declaration.name),
                       "enum " + shortName(declaration.name) +
                           typeParameterSuffix(declaration.typeParameters),
                       "type:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span)});
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                const auto &value = declaration.variants[variant];
                auto detail = value.name;
                if (value.payloadType.has_value()) {
                    detail += '(' + displayTypeSyntax(*value.payloadType) + ')';
                }
                addSymbol({{LanguageSymbolKind::EnumVariant, id, variant}, value.name,
                           std::move(detail), "variant:" + std::to_string(id), value.span,
                           !standardSource(value.span)});
            }
        }
        for (std::size_t id = 0; id < analysis_.program.contracts.size(); ++id) {
            const auto &declaration = analysis_.program.contracts[id];
            const LanguageSymbolId symbol{LanguageSymbolKind::Contract, id, 0};
            typeSymbols_[declaration.name] = symbol;
            addSymbol({symbol, shortName(declaration.name),
                       "contract " + shortName(declaration.name) +
                           typeParameterSuffix(declaration.typeParameters),
                       "type:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span)});
            for (std::size_t method = 0; method < declaration.methods.size(); ++method) {
                const auto &value = declaration.methods[method];
                const LanguageSymbolId methodSymbol{
                    LanguageSymbolKind::ContractMethod, id, method};
                addSymbol({methodSymbol, value.name, contractMethodDetail(value),
                           "contract-method:" + std::to_string(id),
                           identifierSpan(analysis_, value.span, value.name),
                           !standardSource(value.span)});
                if (value.defaultFunction.has_value()) {
                    functionSymbols_[*value.defaultFunction] = methodSymbol;
                }
            }
        }
        for (std::size_t id = 0; id < analysis_.program.attributeDeclarations.size(); ++id) {
            const auto &declaration = analysis_.program.attributeDeclarations[id];
            std::string detail = "attribute " + shortName(declaration.name) + '(';
            for (std::size_t parameter = 0; parameter < declaration.parameters.size();
                 ++parameter) {
                if (parameter != 0) {
                    detail += ", ";
                }
                const auto &value = declaration.parameters[parameter];
                detail += value.name + ' ' + displayTypeSyntax(value.type);
            }
            detail += ')';
            const LanguageSymbolId symbol{LanguageSymbolKind::Attribute, id, 0};
            attributeSymbols_[declaration.name] = symbol;
            addSymbol({symbol, shortName(declaration.name), std::move(detail),
                       "attribute:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span)});
        }
        for (std::size_t id = 0; id < analysis_.program.functions.size(); ++id) {
            const auto &function = analysis_.program.functions[id];
            if (function.closure || (function.receiver.has_value() && contractOwner(function.ownerType))) {
                continue;
            }
            const auto kind = function.receiver.has_value() ? LanguageSymbolKind::Method
                                                            : LanguageSymbolKind::Function;
            const LanguageSymbolId symbol{kind, id, 0};
            functionSymbols_[id] = symbol;
            auto scope = function.receiver.has_value()
                             ? "method:" + function.ownerType
                             : "function:" + function.packageName;
            const auto name = shortName(function.name);
            addSymbol({symbol, name, functionDetail(function), std::move(scope),
                       identifierSpan(analysis_, function.span, name),
                       !standardSource(function.span) && name != "main" && name != "drop"});
        }
    }

    std::string displayType(const Type &type, const Function &function) const {
        if (type.kind == TypeKind::Parameter &&
            type.declaration < function.typeParameters.size()) {
            return function.typeParameters[type.declaration];
        }
        if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
             type.kind == TypeKind::Edit) && type.arguments.size() == 1) {
            const auto qualifier = type.kind == TypeKind::Own ? "own"
                                   : type.kind == TypeKind::View ? "view"
                                                                 : "edit";
            return std::string(qualifier) + ' ' + displayType(type.arguments[0], function);
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return '[' + std::to_string(type.declaration) + ']' +
                   displayType(type.arguments[0], function);
        }
        if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
            return '[' + displayType(type.arguments[0], function) + ']';
        }
        if (type.kind == TypeKind::Function && !type.arguments.empty()) {
            std::string result = "fn(";
            for (std::size_t index = 1; index < type.arguments.size(); ++index) {
                if (index != 1) {
                    result += ", ";
                }
                result += displayType(type.arguments[index], function);
            }
            result += ") " + displayType(type.arguments[0], function);
            return result;
        }
        std::string result;
        if (type.kind == TypeKind::Struct && type.declaration < analysis_.program.structs.size()) {
            result = shortName(analysis_.program.structs[type.declaration].name);
        } else if (type.kind == TypeKind::Enum &&
                   type.declaration < analysis_.program.enums.size()) {
            result = shortName(analysis_.program.enums[type.declaration].name);
        } else if (type.kind == TypeKind::Contract &&
                   type.declaration < analysis_.program.contracts.size()) {
            result = shortName(analysis_.program.contracts[type.declaration].name);
        } else {
            result = type.kind == TypeKind::Void      ? "void"
                     : type.kind == TypeKind::I32     ? "i32"
                     : type.kind == TypeKind::U64     ? "u64"
                     : type.kind == TypeKind::Bool    ? "bool"
                     : type.kind == TypeKind::String  ? "String"
                                                       : "invalid";
        }
        if (type.arguments.empty()) {
            return result;
        }
        result += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                result += ", ";
            }
            result += displayType(type.arguments[index], function);
        }
        result += '>';
        return result;
    }

    void declareLocal(std::size_t function, FirLocalId local, LanguageSymbolKind kind,
                      SourceSpan span, bool renameable = true,
                      bool addDefinition = true) {
        if (function >= localSymbols_.size() || local >= localSymbols_[function].size() ||
            function >= analysis_.program.functions.size()) {
            return;
        }
        const auto &semanticFunction = semantic_->functions[function];
        const auto &declaration = semanticFunction.locals[local];
        const LanguageSymbolId symbol{kind, function, local};
        localSymbols_[function][local] = symbol;
        addSymbol({symbol, declaration.name,
                   declaration.name + ' ' +
                       displayType(declaration.type, analysis_.program.functions[function]),
                   "local:" + std::to_string(function), span,
                   renameable && !standardSource(span)},
                  addDefinition);
    }

    void addLocalDeclarations() {
        for (std::size_t function = 0; function < analysis_.program.functions.size() &&
                                       function < semantic_->functions.size();
             ++function) {
            const auto &declaration = analysis_.program.functions[function];
            const auto &semanticFunction = semantic_->functions[function];
            for (std::size_t parameter = 0;
                 parameter < declaration.parameters.size() &&
                 parameter < semanticFunction.parameters.size();
                 ++parameter) {
                const auto &value = declaration.parameters[parameter];
                auto span = value.span;
                auto renameable = true;
                if (value.name == "self") {
                    span = identifierSpan(analysis_, declaration.span, shortName(declaration.name));
                    renameable = false;
                }
                declareLocal(function, semanticFunction.parameters[parameter],
                             LanguageSymbolKind::Parameter, span, renameable,
                             value.name != "self");
            }
        }
        for (std::size_t statement = 0; statement < analysis_.program.statements.size();
             ++statement) {
            if (!statementOwners_[statement].has_value()) {
                continue;
            }
            const auto function = *statementOwners_[statement];
            const auto &sourceStatement = analysis_.program.statements[statement];
            if (const auto *variable = std::get_if<VariableStatement>(&sourceStatement.value)) {
                if (semantic_->statementLocals[statement].has_value()) {
                    declareLocal(function, *semantic_->statementLocals[statement],
                                 LanguageSymbolKind::Local,
                                 identifierSpan(analysis_, sourceStatement.span, variable->name));
                }
                if (variable->elseBinding.has_value() &&
                    semantic_->statementElseLocals[statement].has_value()) {
                    auto start = sourceStatement.span.offset;
                    if (const auto *value = source(sourceStatement.span); value != nullptr) {
                        const auto found = value->contents.find("else", start);
                        if (found != std::string::npos) {
                            start = found + 4;
                        }
                    }
                    declareLocal(function, *semantic_->statementElseLocals[statement],
                                 LanguageSymbolKind::Local,
                                 identifierSpan(analysis_, sourceStatement.span,
                                                *variable->elseBinding, start));
                }
            } else if (const auto *destructure =
                           std::get_if<StructDestructureStatement>(&sourceStatement.value)) {
                const auto &target = semantic_->statementStructTargets[statement];
                if (!target.has_value()) {
                    continue;
                }
                for (std::size_t field = 0;
                     field < destructure->fields.size() && field < target->bindings.size();
                     ++field) {
                    const auto &pattern = destructure->fields[field];
                    const auto start = pattern.span.offset + pattern.field.size();
                    declareLocal(function, target->bindings[field], LanguageSymbolKind::Local,
                                 identifierSpan(analysis_, pattern.span, pattern.binding, start),
                                 pattern.binding != pattern.field);
                }
            }
        }
        for (std::size_t expression = 0; expression < analysis_.program.expressions.size();
             ++expression) {
            const auto *match =
                std::get_if<MatchExpression>(&analysis_.program.expressions[expression].value);
            if (match == nullptr || !expressionOwners_[expression].has_value() ||
                !semantic_->matchTargets[expression].has_value()) {
                continue;
            }
            const auto function = *expressionOwners_[expression];
            const auto &target = *semantic_->matchTargets[expression];
            for (std::size_t arm = 0;
                 arm < match->arms.size() && arm < target.bindings.size(); ++arm) {
                if (!match->arms[arm].binding.has_value() || !target.bindings[arm].has_value()) {
                    continue;
                }
                const auto start = match->arms[arm].span.offset + match->arms[arm].variant.size();
                declareLocal(function, *target.bindings[arm], LanguageSymbolKind::Local,
                             identifierSpan(analysis_, match->arms[arm].span,
                                            *match->arms[arm].binding, start));
            }
        }
    }

    std::optional<LanguageSymbolId> typeSymbol(Type type) const {
        while ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                type.kind == TypeKind::Edit || type.kind == TypeKind::Array ||
                type.kind == TypeKind::Slice) &&
               type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        if (type.kind == TypeKind::Struct) {
            return LanguageSymbolId{LanguageSymbolKind::Struct, type.declaration, 0};
        }
        if (type.kind == TypeKind::Enum) {
            return LanguageSymbolId{LanguageSymbolKind::Enum, type.declaration, 0};
        }
        if (type.kind == TypeKind::Contract) {
            return LanguageSymbolId{LanguageSymbolKind::Contract, type.declaration, 0};
        }
        return std::nullopt;
    }

    std::optional<LanguageSymbolId> typeSymbol(const TypeSyntax &type) const {
        if ((type.name == "own" || type.name == "view" || type.name == "edit" ||
             type.name == "[array]" || type.name == "[slice]") &&
            type.arguments.size() == 1) {
            return typeSymbol(type.arguments.front());
        }
        const auto found = typeSymbols_.find(type.name);
        return found == typeSymbols_.end() ? std::nullopt
                                          : std::optional<LanguageSymbolId>{found->second};
    }

    void addTypeLink(LanguageSymbolId symbol,
                     const std::optional<LanguageSymbolId> &type) {
        if (type.has_value()) {
            typeLinks_.push_back({symbol, *type});
        }
    }

    void addTypeLinks() {
        for (std::size_t declaration = 0;
             declaration < analysis_.program.structs.size(); ++declaration) {
            const LanguageSymbolId owner{LanguageSymbolKind::Struct, declaration, 0};
            addTypeLink(owner, owner);
            if (declaration >= semantic_->structs.size()) {
                continue;
            }
            const auto &semantic = semantic_->structs[declaration];
            for (std::size_t field = 0; field < semantic.fieldTypes.size(); ++field) {
                addTypeLink({LanguageSymbolKind::Field, declaration, field},
                            typeSymbol(semantic.fieldTypes[field]));
            }
        }
        for (std::size_t declaration = 0;
             declaration < analysis_.program.enums.size(); ++declaration) {
            const LanguageSymbolId owner{LanguageSymbolKind::Enum, declaration, 0};
            addTypeLink(owner, owner);
            if (declaration >= semantic_->enums.size()) {
                continue;
            }
            const auto &semantic = semantic_->enums[declaration];
            for (std::size_t variant = 0; variant < semantic.payloadTypes.size(); ++variant) {
                if (semantic.payloadTypes[variant].has_value()) {
                    addTypeLink({LanguageSymbolKind::EnumVariant, declaration, variant},
                                typeSymbol(*semantic.payloadTypes[variant]));
                }
            }
        }
        for (std::size_t declaration = 0;
             declaration < analysis_.program.contracts.size(); ++declaration) {
            const LanguageSymbolId owner{LanguageSymbolKind::Contract, declaration, 0};
            addTypeLink(owner, owner);
            const auto &contract = analysis_.program.contracts[declaration];
            for (std::size_t method = 0; method < contract.methods.size(); ++method) {
                addTypeLink({LanguageSymbolKind::ContractMethod, declaration, method},
                            typeSymbol(contract.methods[method].returnType));
            }
        }
        for (std::size_t function = 0;
             function < semantic_->functions.size(); ++function) {
            const auto callable = functionSymbols_.find(function);
            if (callable != functionSymbols_.end()) {
                addTypeLink(callable->second,
                            typeSymbol(semantic_->functions[function].returnType));
            }
            for (std::size_t local = 0;
                 function < localSymbols_.size() &&
                 local < localSymbols_[function].size() &&
                 local < semantic_->functions[function].locals.size(); ++local) {
                if (localSymbols_[function][local].has_value()) {
                    addTypeLink(*localSymbols_[function][local],
                                typeSymbol(semantic_->functions[function].locals[local].type));
                }
            }
        }
    }

    std::optional<LanguageSymbolId> resolveLocal(std::size_t function, FirLocalId local,
                                                  std::set<std::pair<std::size_t, FirLocalId>> &seen) const {
        if (function >= localSymbols_.size() || local >= localSymbols_[function].size()) {
            return std::nullopt;
        }
        if (localSymbols_[function][local].has_value()) {
            return localSymbols_[function][local];
        }
        if (!seen.emplace(function, local).second ||
            !localAliases_[function][local].has_value()) {
            return std::nullopt;
        }
        const auto [outerFunction, outerLocal] = *localAliases_[function][local];
        return resolveLocal(outerFunction, outerLocal, seen);
    }

    std::optional<LanguageSymbolId> resolveLocal(std::size_t function, FirLocalId local) const {
        std::set<std::pair<std::size_t, FirLocalId>> seen;
        return resolveLocal(function, local, seen);
    }

    void bindCaptures() {
        for (std::size_t expression = 0; expression < analysis_.program.expressions.size();
             ++expression) {
            const auto *closure =
                std::get_if<FunctionExpression>(&analysis_.program.expressions[expression].value);
            if (closure == nullptr || !expressionOwners_[expression].has_value() ||
                !semantic_->closureTargets[expression].has_value() ||
                closure->function >= localAliases_.size()) {
                continue;
            }
            const auto outerFunction = *expressionOwners_[expression];
            const auto &target = *semantic_->closureTargets[expression];
            const auto count = std::min(target.captures.size(),
                                        localAliases_[closure->function].size());
            for (std::size_t capture = 0; capture < count; ++capture) {
                localAliases_[closure->function][capture] =
                    std::pair{outerFunction, target.captures[capture]};
            }
        }
        for (std::size_t expression = 0; expression < analysis_.program.expressions.size();
             ++expression) {
            const auto *closure =
                std::get_if<FunctionExpression>(&analysis_.program.expressions[expression].value);
            if (closure == nullptr || !expressionOwners_[expression].has_value() ||
                !semantic_->closureTargets[expression].has_value() ||
                closure->function >= localAliases_.size()) {
                continue;
            }
            const auto outerFunction = *expressionOwners_[expression];
            const auto &target = *semantic_->closureTargets[expression];
            const auto count = std::min(target.captures.size(),
                                        localAliases_[closure->function].size());
            for (std::size_t capture = 0; capture < count; ++capture) {
                const auto symbol = resolveLocal(outerFunction, target.captures[capture]);
                if (symbol.has_value() &&
                    capture < analysis_.program.functions[closure->function].captures.size()) {
                    addOccurrence(*symbol,
                                  analysis_.program.functions[closure->function].captures[capture]
                                      .span);
                }
            }
        }
    }

    void addTypeReference(const TypeSyntax &type) {
        const auto found = typeSymbols_.find(type.name);
        if (found != typeSymbols_.end()) {
            addOccurrence(found->second, identifierSpan(analysis_, type.span, type.name));
        }
        for (const auto &argument : type.arguments) {
            addTypeReference(argument);
        }
    }

    void addAttributes(const std::vector<AttributeApplication> &attributes) {
        for (const auto &application : attributes) {
            const auto found = attributeSymbols_.find(application.name);
            if (found != attributeSymbols_.end()) {
                addOccurrence(found->second,
                              identifierSpan(analysis_, application.span, application.name));
            }
        }
    }

    void addDeclarationTypeReferences() {
        for (const auto &declaration : analysis_.program.structs) {
            addAttributes(declaration.attributes);
            for (const auto &field : declaration.fields) {
                addTypeReference(field.type);
                addAttributes(field.attributes);
            }
            for (const auto &implementation : declaration.implementations) {
                addTypeReference(implementation.contract);
            }
        }
        for (const auto &declaration : analysis_.program.enums) {
            addAttributes(declaration.attributes);
            for (const auto &variant : declaration.variants) {
                if (variant.payloadType.has_value()) {
                    addTypeReference(*variant.payloadType);
                }
                addAttributes(variant.attributes);
            }
        }
        for (const auto &declaration : analysis_.program.contracts) {
            addAttributes(declaration.attributes);
            for (const auto &parent : declaration.parents) {
                addTypeReference(parent);
            }
            for (const auto &method : declaration.methods) {
                for (const auto &parameter : method.parameters) {
                    addTypeReference(parameter.type);
                    addAttributes(parameter.attributes);
                }
                addTypeReference(method.returnType);
                addAttributes(method.attributes);
            }
        }
        for (const auto &declaration : analysis_.program.attributeDeclarations) {
            for (const auto &parameter : declaration.parameters) {
                addTypeReference(parameter.type);
            }
        }
        for (const auto &function : analysis_.program.functions) {
            addAttributes(function.attributes);
            for (const auto &parameter : function.parameters) {
                addTypeReference(parameter.type);
                addAttributes(parameter.attributes);
            }
            addTypeReference(function.returnType);
        }
    }

    std::optional<LanguageSymbolId> contractMethodSymbol(const CallTarget &target) const {
        if (target.contract >= semantic_->contracts.size() ||
            target.method >= semantic_->contracts[target.contract].methods.size()) {
            return std::nullopt;
        }
        const auto &method = semantic_->contracts[target.contract].methods[target.method];
        if (method.originContract >= analysis_.program.contracts.size()) {
            return std::nullopt;
        }
        const auto &origin = analysis_.program.contracts[method.originContract];
        for (std::size_t index = 0; index < origin.methods.size(); ++index) {
            if (origin.methods[index].name == method.name &&
                origin.methods[index].span.source == method.span.source &&
                origin.methods[index].span.offset == method.span.offset) {
                return LanguageSymbolId{LanguageSymbolKind::ContractMethod,
                                        method.originContract, index};
            }
        }
        return std::nullopt;
    }

    void addCallReference(AstExpressionId expression, const CallTarget &target,
                          SourceSpan span) {
        std::optional<LanguageSymbolId> callee;
        if (target.kind == CallTargetKind::Function || target.kind == CallTargetKind::Method) {
            const auto found = functionSymbols_.find(target.function);
            if (found != functionSymbols_.end()) {
                callee = found->second;
            }
        } else if (target.kind == CallTargetKind::ContractMethod) {
            callee = contractMethodSymbol(target);
        } else if (target.kind == CallTargetKind::FunctionValue &&
                   expressionOwners_[expression].has_value()) {
            if (const auto symbol = resolveLocal(*expressionOwners_[expression], target.local);
                symbol.has_value()) {
                addNamedOccurrence(*symbol, span);
            }
        }
        if (!callee.has_value()) {
            return;
        }
        const auto *calleeSymbol = symbol(*callee);
        if (calleeSymbol == nullptr) {
            return;
        }
        addNamedOccurrence(*callee, span);
        if (expression < expressionOwners_.size() &&
            expressionOwners_[expression].has_value()) {
            const auto caller = functionSymbols_.find(*expressionOwners_[expression]);
            if (caller != functionSymbols_.end()) {
                calls_.push_back({caller->second, *callee,
                                  identifierSpan(analysis_, span,
                                                 calleeSymbol->name)});
            }
        }
    }

    static Type valueType(Type type) {
        while ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                type.kind == TypeKind::Edit) && type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        return type;
    }

    void addExpressionReferences() {
        for (std::size_t id = 0; id < analysis_.program.expressions.size(); ++id) {
            const auto &expression = analysis_.program.expressions[id];
            if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
                for (const auto &argument : name->typeArguments) {
                    addTypeReference(argument);
                }
                if (expressionOwners_[id].has_value() &&
                    semantic_->expressionLocals[id].has_value()) {
                    if (const auto symbol = resolveLocal(*expressionOwners_[id],
                                                         *semantic_->expressionLocals[id]);
                        symbol.has_value()) {
                        addOccurrence(*symbol, expression.span);
                    }
                } else if (semantic_->functionValueTargets[id].has_value()) {
                    const auto found =
                        functionSymbols_.find(semantic_->functionValueTargets[id]->function);
                    if (found != functionSymbols_.end()) {
                        addNamedOccurrence(found->second, expression.span);
                    }
                }
            } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
                for (const auto &argument : call->typeArguments) {
                    addTypeReference(argument);
                }
                if (semantic_->callTargets[id].has_value()) {
                    addCallReference(id, *semantic_->callTargets[id], expression.span);
                }
            } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
                addTypeReference(literal->type);
                if (semantic_->structTargets[id].has_value()) {
                    const auto &target = *semantic_->structTargets[id];
                    const auto type = valueType(target.type);
                    if (type.kind == TypeKind::Struct) {
                        for (std::size_t field = 0;
                             field < literal->fields.size() && field < target.fields.size();
                             ++field) {
                            addNamedOccurrence({LanguageSymbolKind::Field, type.declaration,
                                                target.fields[field]},
                                               literal->fields[field].span);
                        }
                    }
                }
            } else if (const auto *member = std::get_if<MemberExpression>(&expression.value)) {
                for (const auto &argument : member->typeArguments) {
                    addTypeReference(argument);
                }
                if (semantic_->callTargets[id].has_value()) {
                    addCallReference(id, *semantic_->callTargets[id], expression.span);
                } else if (semantic_->enumTargets[id].has_value()) {
                    const auto &target = *semantic_->enumTargets[id];
                    addNamedOccurrence({LanguageSymbolKind::EnumVariant,
                                        target.type.declaration, target.variant},
                                       expression.span);
                    if (member->base.has_value()) {
                        const auto baseSpan = analysis_.program.expressions[*member->base].span;
                        addNamedOccurrence(
                            {LanguageSymbolKind::Enum, target.type.declaration, 0}, baseSpan);
                    }
                } else if (semantic_->expressionFields[id].has_value() &&
                           member->base.has_value()) {
                    const auto base = valueType(semantic_->expressionTypes[*member->base]);
                    if (base.kind == TypeKind::Struct) {
                        addNamedOccurrence({LanguageSymbolKind::Field, base.declaration,
                                            *semantic_->expressionFields[id]},
                                           expression.span);
                    }
                }
            } else if (const auto *match = std::get_if<MatchExpression>(&expression.value)) {
                if (semantic_->matchTargets[id].has_value()) {
                    const auto &target = *semantic_->matchTargets[id];
                    for (std::size_t arm = 0;
                         arm < match->arms.size() && arm < target.variants.size(); ++arm) {
                        addNamedOccurrence({LanguageSymbolKind::EnumVariant,
                                            target.type.declaration, target.variants[arm]},
                                           match->arms[arm].span);
                    }
                }
            }
        }
    }

    void addStatementReferences() {
        for (std::size_t id = 0; id < analysis_.program.statements.size(); ++id) {
            const auto &statement = analysis_.program.statements[id];
            if (const auto *variable = std::get_if<VariableStatement>(&statement.value)) {
                if (variable->type.has_value()) {
                    addTypeReference(*variable->type);
                }
            } else if (const auto *destructure =
                           std::get_if<StructDestructureStatement>(&statement.value)) {
                addTypeReference(destructure->type);
                const auto &target = semantic_->statementStructTargets[id];
                if (!target.has_value()) {
                    continue;
                }
                const auto type = valueType(target->type);
                if (type.kind != TypeKind::Struct) {
                    continue;
                }
                for (std::size_t field = 0;
                     field < destructure->fields.size() && field < target->fields.size();
                     ++field) {
                    addNamedOccurrence({LanguageSymbolKind::Field, type.declaration,
                                        target->fields[field]},
                                       destructure->fields[field].span);
                }
            }
        }
    }

    void finish() {
        std::sort(symbols_.begin(), symbols_.end(),
                  [](const auto &left, const auto &right) { return idLess(left.id, right.id); });
        std::sort(occurrences_.begin(), occurrences_.end(),
                  [](const auto &left, const auto &right) {
                      if (left.span.source != right.span.source) {
                          return left.span.source < right.span.source;
                      }
                      if (left.span.offset != right.span.offset) {
                          return left.span.offset < right.span.offset;
                      }
                      if (left.definition != right.definition) {
                          return left.definition;
                      }
                      return idLess(left.symbol, right.symbol);
                  });
        occurrences_.erase(
            std::unique(occurrences_.begin(), occurrences_.end(),
                        [](const auto &left, const auto &right) {
                            return sameId(left.symbol, right.symbol) &&
                                   left.span.source == right.span.source &&
                                   left.span.offset == right.span.offset &&
                                   left.span.length == right.span.length &&
                                   left.definition == right.definition;
                        }),
            occurrences_.end());
        std::sort(calls_.begin(), calls_.end(), [](const auto &left, const auto &right) {
            if (!sameId(left.caller, right.caller)) {
                return idLess(left.caller, right.caller);
            }
            if (!sameId(left.callee, right.callee)) {
                return idLess(left.callee, right.callee);
            }
            if (left.span.source != right.span.source) {
                return left.span.source < right.span.source;
            }
            return left.span.offset < right.span.offset;
        });
        calls_.erase(std::unique(calls_.begin(), calls_.end(), [](const auto &left,
                                                                  const auto &right) {
                         return sameId(left.caller, right.caller) &&
                                sameId(left.callee, right.callee) &&
                                left.span.source == right.span.source &&
                                left.span.offset == right.span.offset &&
                                left.span.length == right.span.length;
                     }),
                     calls_.end());
        std::sort(typeLinks_.begin(), typeLinks_.end(), [](const auto &left,
                                                           const auto &right) {
            if (!sameId(left.symbol, right.symbol)) {
                return idLess(left.symbol, right.symbol);
            }
            return idLess(left.type, right.type);
        });
        typeLinks_.erase(
            std::unique(typeLinks_.begin(), typeLinks_.end(), [](const auto &left,
                                                                 const auto &right) {
                return sameId(left.symbol, right.symbol) && sameId(left.type, right.type);
            }),
            typeLinks_.end());
    }

    const ProjectAnalysis &analysis_;
    const SemanticModel *semantic_{};
    std::vector<LanguageSymbol> symbols_;
    std::vector<LanguageOccurrence> occurrences_;
    std::vector<LanguageCall> calls_;
    std::vector<LanguageTypeLink> typeLinks_;
    std::vector<std::optional<std::size_t>> expressionOwners_;
    std::vector<std::optional<std::size_t>> statementOwners_;
    std::vector<std::vector<std::optional<LanguageSymbolId>>> localSymbols_;
    std::vector<std::vector<std::optional<std::pair<std::size_t, FirLocalId>>>> localAliases_;
    std::map<std::string, LanguageSymbolId> typeSymbols_;
    std::map<std::string, LanguageSymbolId> attributeSymbols_;
    std::map<std::size_t, LanguageSymbolId> functionSymbols_;
};

bool validIdentifier(std::string_view name) {
    if (name.empty() || (!(name.front() >= 'a' && name.front() <= 'z') &&
                         !(name.front() >= 'A' && name.front() <= 'Z') &&
                         name.front() != '_')) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char value) {
        return identifierByte(value);
    });
}

bool reservedIdentifier(std::string_view name) {
    static const std::set<std::string_view> reserved{
        "package", "import", "as",      "extern", "struct", "enum",  "contract",
        "attribute", "implements", "extends", "by", "fn", "let", "var", "return",
        "discard", "if", "else", "while", "match", "capture", "replace", "with",
        "own", "view", "edit", "true", "false", "print", "panic", "len", "i32",
        "u64", "bool", "String", "void", "Option", "Result"};
    return reserved.contains(name);
}

} // namespace

LanguageIndex::LanguageIndex(std::vector<LanguageSymbol> symbols,
                             std::vector<LanguageOccurrence> occurrences,
                             std::vector<LanguageCall> calls,
                             std::vector<LanguageTypeLink> typeLinks)
    : symbols_(std::move(symbols)), occurrences_(std::move(occurrences)),
      calls_(std::move(calls)), typeLinks_(std::move(typeLinks)) {}

const std::vector<LanguageSymbol> &LanguageIndex::symbols() const { return symbols_; }

const std::vector<LanguageOccurrence> &LanguageIndex::occurrences() const {
    return occurrences_;
}

const LanguageSymbol *LanguageIndex::symbol(LanguageSymbolId id) const {
    const auto found = std::lower_bound(symbols_.begin(), symbols_.end(), id,
                                        [](const auto &symbol, const auto &value) {
                                            return idLess(symbol.id, value);
                                        });
    return found != symbols_.end() && found->id == id ? &*found : nullptr;
}

const LanguageOccurrence *LanguageIndex::occurrenceAt(std::size_t source,
                                                       std::size_t offset) const {
    const LanguageOccurrence *result{};
    for (const auto &occurrence : occurrences_) {
        if (occurrence.span.source != source || offset < occurrence.span.offset ||
            offset >= occurrence.span.offset + occurrence.span.length) {
            continue;
        }
        if (result == nullptr || occurrence.span.length < result->span.length ||
            (occurrence.span.length == result->span.length && occurrence.definition)) {
            result = &occurrence;
        }
    }
    return result;
}

std::vector<LanguageOccurrence> LanguageIndex::references(LanguageSymbolId id,
                                                          bool includeDefinition) const {
    std::vector<LanguageOccurrence> result;
    for (const auto &occurrence : occurrences_) {
        if (occurrence.symbol == id && (includeDefinition || !occurrence.definition)) {
            result.push_back(occurrence);
        }
    }
    return result;
}

std::vector<LanguageCall> LanguageIndex::incomingCalls(LanguageSymbolId id) const {
    std::vector<LanguageCall> result;
    std::copy_if(calls_.begin(), calls_.end(), std::back_inserter(result),
                 [id](const auto &call) { return call.callee == id; });
    return result;
}

std::vector<LanguageCall> LanguageIndex::outgoingCalls(LanguageSymbolId id) const {
    std::vector<LanguageCall> result;
    std::copy_if(calls_.begin(), calls_.end(), std::back_inserter(result),
                 [id](const auto &call) { return call.caller == id; });
    return result;
}

const LanguageSymbol *LanguageIndex::typeDefinition(LanguageSymbolId id) const {
    const auto found = std::lower_bound(typeLinks_.begin(), typeLinks_.end(), id,
                                        [](const auto &link, const auto &value) {
                                            return idLess(link.symbol, value);
                                        });
    return found != typeLinks_.end() && found->symbol == id ? symbol(found->type) : nullptr;
}

bool LanguageIndex::canRename(LanguageSymbolId id, std::string_view name) const {
    const auto *current = symbol(id);
    if (current == nullptr || !current->renameable || !validIdentifier(name) ||
        reservedIdentifier(name)) {
        return false;
    }
    const auto wasExported = !current->name.empty() &&
                             std::isupper(static_cast<unsigned char>(current->name.front())) != 0;
    const auto willExport = std::isupper(static_cast<unsigned char>(name.front())) != 0;
    if (wasExported != willExport) {
        return false;
    }
    return std::none_of(symbols_.begin(), symbols_.end(), [&](const auto &candidate) {
        return candidate.id != id && candidate.scope == current->scope && candidate.name == name;
    });
}

LanguageIndex buildLanguageIndex(const ProjectAnalysis &analysis) {
    return IndexBuilder(analysis).build();
}

} // namespace foundation
