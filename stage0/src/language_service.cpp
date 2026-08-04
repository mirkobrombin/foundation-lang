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

std::string_view trimWhitespace(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::pair<std::size_t, std::size_t> previousLine(std::string_view source,
                                                 std::size_t lineStart) {
    if (lineStart == 0) {
        return {0, 0};
    }
    auto end = lineStart - 1;
    if (end != 0 && source[end - 1] == '\r') {
        --end;
    }
    const auto newline = end == 0 ? std::string_view::npos : source.rfind('\n', end - 1);
    const auto start = newline == std::string_view::npos ? 0 : newline + 1;
    return {start, end};
}

std::string documentationBefore(std::string_view source, std::size_t offset) {
    if (offset > source.size()) {
        return {};
    }
    const auto newline = offset == 0 ? std::string_view::npos : source.rfind('\n', offset - 1);
    auto cursor = newline == std::string_view::npos ? 0 : newline + 1;
    while (cursor != 0) {
        const auto [start, end] = previousLine(source, cursor);
        const auto line = trimWhitespace(source.substr(start, end - start));
        if (!line.starts_with('@')) {
            break;
        }
        cursor = start;
    }
    std::vector<std::string> lines;
    while (cursor != 0) {
        const auto [start, end] = previousLine(source, cursor);
        const auto line = trimWhitespace(source.substr(start, end - start));
        if (!line.starts_with("//")) {
            break;
        }
        auto text = line.substr(2);
        if (!text.empty() && text.front() == ' ') {
            text.remove_prefix(1);
        }
        lines.emplace_back(text);
        cursor = start;
    }
    if (!lines.empty()) {
        std::reverse(lines.begin(), lines.end());
        std::string result;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (index != 0) {
                result += '\n';
            }
            result += lines[index];
        }
        return result;
    }
    return {};
}

std::string displayTypeSyntax(const TypeSyntax &type);

std::string parameterDocumentation(const ProjectAnalysis &analysis,
                                   const Parameter &parameter) {
    if (parameter.span.source >= analysis.sources.size()) {
        return {};
    }
    const auto &source = analysis.sources[parameter.span.source].contents;
    const auto atLineStart = [&source](std::size_t offset) {
        if (offset > source.size()) {
            return false;
        }
        const auto newline =
            offset == 0 ? std::string_view::npos : source.rfind('\n', offset - 1);
        const auto lineStart = newline == std::string_view::npos ? 0 : newline + 1;
        return trimWhitespace(
                   std::string_view(source).substr(lineStart, offset - lineStart))
            .empty();
    };
    if (atLineStart(parameter.span.offset)) {
        const auto documentation = documentationBefore(source, parameter.span.offset);
        if (!documentation.empty()) {
            return documentation;
        }
    }
    if (parameter.attributes.empty()) {
        return {};
    }
    auto attributeOffset = parameter.attributes.front().span.offset;
    if (attributeOffset <= source.size() && attributeOffset != 0 &&
        source[attributeOffset - 1] == '@') {
        --attributeOffset;
    }
    return atLineStart(attributeOffset) ? documentationBefore(source, attributeOffset)
                                        : std::string{};
}

std::string fieldDetail(const ProjectAnalysis &analysis, const StructField &field) {
    auto result = field.name + ' ' + displayTypeSyntax(field.type);
    if (!field.defaultSpan.has_value() ||
        field.defaultSpan->source >= analysis.sources.size()) {
        return result;
    }
    const auto &source = analysis.sources[field.defaultSpan->source].contents;
    if (field.defaultSpan->offset > source.size() ||
        field.defaultSpan->length > source.size() - field.defaultSpan->offset) {
        return result;
    }
    const auto expression = trimWhitespace(std::string_view(source).substr(
        field.defaultSpan->offset, field.defaultSpan->length));
    if (!expression.empty()) {
        result += " = ";
        result += expression;
    }
    return result;
}

std::string enumVariantDetail(const EnumVariant &variant) {
    auto result = variant.name;
    if (!variant.payloadType.has_value()) {
        return result;
    }
    result += '(';
    if (variant.payloadName.has_value()) {
        result += *variant.payloadName + ' ';
    }
    result += displayTypeSyntax(*variant.payloadType) + ')';
    return result;
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
    if ((type.name == "[raw]" || type.name == "[raw-const]") &&
        type.arguments.size() == 1) {
        return std::string(type.name == "[raw]" ? "*" : "*const ") +
               displayTypeSyntax(type.arguments[0]);
    }
    if ((type.name == "[function]" || type.name == "[transferable-function]") &&
        !type.arguments.empty()) {
        std::string result =
            type.name == "[transferable-function]" ? "transferable fn(" : "fn(";
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            if (index != 1) {
                result += ", ";
            }
            result += displayTypeSyntax(type.arguments[index]);
        }
        result += ") " + displayTypeSyntax(type.arguments[0]);
        return result;
    }
    if ((type.name == "[function-read]" || type.name == "[function-edit]" ||
         type.name == "[function-transfer]") &&
        type.arguments.size() == 1) {
        const auto prefix = type.name == "[function-edit]"     ? "&"
                            : type.name == "[function-transfer]" ? "$"
                                                                  : "";
        return std::string(prefix) + displayTypeSyntax(type.arguments[0]);
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

std::string typeParameterSuffix(const std::vector<std::string> &parameters,
                                const std::vector<bool> *transferable = nullptr) {
    if (parameters.empty()) {
        return {};
    }
    std::string result = "<";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += parameters[index];
        if (transferable != nullptr && index < transferable->size() &&
            (*transferable)[index]) {
            result += " transferable";
        }
    }
    result += '>';
    return result;
}

std::string attributeDetail(const AttributeApplication &attribute) {
    auto result = '@' + shortName(attribute.name);
    if (attribute.parenthesized) {
        result += attribute.arguments.empty() ? "()" : "(...)";
    }
    return result;
}

std::string parameterDetail(const Parameter &parameter) {
    std::string result;
    for (const auto &attribute : parameter.attributes) {
        result += attributeDetail(attribute) + ' ';
    }
    if (parameter.mode == ParameterMode::Edit) {
        result += '&';
    } else if (parameter.mode == ParameterMode::Transfer) {
        result += '$';
    }
    result += parameter.name + ' ' + displayTypeSyntax(parameter.type);
    return result;
}

std::string receiverDetail(ReceiverKind receiver) {
    return receiver == ReceiverKind::View ? "self"
           : receiver == ReceiverKind::Edit ? "&self"
                                             : "$self";
}

std::string functionDetail(const Function &function) {
    auto prefix = function.workflow.has_value()
                      ? std::string(function.workflow->kind == WorkflowKind::Pipeline
                                        ? "pipeline "
                                        : "saga ")
                  : function.action ? std::string("action ")
                                : function.task ? std::string("task ")
                                : function.cSymbol.has_value() ? std::string("extern c fn ")
                                                               : std::string("fn ");
    if (function.blocking) {
        prefix = "@blocking " + prefix;
    } else if (function.callback) {
        prefix = "@callback " + prefix;
    }
    for (auto attribute = function.attributes.rbegin();
         attribute != function.attributes.rend(); ++attribute) {
        if (attribute->name != "blocking" && attribute->name != "callback") {
            prefix = attributeDetail(*attribute) + ' ' + prefix;
        }
    }
    std::string result = prefix + shortName(function.name) +
                         typeParameterSuffix(function.typeParameters,
                                             &function.transferableTypeParameters) + '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        const auto &parameter = function.parameters[index];
        if (function.receiver.has_value() && index == 0) {
            result += receiverDetail(*function.receiver);
        } else {
            result += parameterDetail(parameter);
        }
    }
    result += ") " + displayTypeSyntax(function.returnType);
    return result;
}

std::string contractMethodDetail(const ContractMethod &method) {
    std::string result = "fn " + method.name + '(';
    result += receiverDetail(method.receiver);
    for (const auto &parameter : method.parameters) {
        result += ", " + parameterDetail(parameter);
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
            addWorkflowReferences();
        }
        addDeclarationTypeReferences();
        finish();
        return LanguageIndex(std::move(symbols_), std::move(occurrences_),
                             std::move(calls_), std::move(typeLinks_));
    }

    void addWorkflowReferences() {
        for (std::size_t function = 0;
             function < analysis_.program.functions.size() &&
             function < semantic_->functions.size();
             ++function) {
            const auto &source = analysis_.program.functions[function];
            const auto &semantic = semantic_->functions[function];
            if (!source.workflow.has_value() || !semantic.workflow.has_value()) {
                continue;
            }
            const auto caller = functionSymbols_.find(function);
            const auto count = std::min(source.workflow->steps.size(),
                                        semantic.workflow->steps.size());
            for (std::size_t index = 0; index < count; ++index) {
                const auto &sourceStep = source.workflow->steps[index];
                const auto &targetStep = semantic.workflow->steps[index];
                const auto target = functionSymbols_.find(targetStep.function);
                if (target != functionSymbols_.end()) {
                    const auto *targetSymbol = symbol(target->second);
                    if (targetSymbol != nullptr) {
                        const auto span = identifierSpan(
                            analysis_, sourceStep.functionSpan, targetSymbol->name);
                        addOccurrence(target->second, span);
                        if (caller != functionSymbols_.end()) {
                            calls_.push_back({caller->second, target->second, span});
                        }
                    }
                }
                if (!targetStep.compensation.has_value() ||
                    !sourceStep.compensationSpan.has_value()) {
                    continue;
                }
                const auto compensation =
                    functionSymbols_.find(*targetStep.compensation);
                if (compensation == functionSymbols_.end()) {
                    continue;
                }
                const auto *compensationSymbol = symbol(compensation->second);
                if (compensationSymbol == nullptr) {
                    continue;
                }
                const auto span = identifierSpan(analysis_, *sourceStep.compensationSpan,
                                                 compensationSymbol->name);
                addOccurrence(compensation->second, span);
                if (caller != functionSymbols_.end()) {
                    calls_.push_back({caller->second, compensation->second, span});
                }
            }
        }
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

    void addSymbol(LanguageSymbol symbol, bool addDefinition = true,
                   bool inferDocumentation = true) {
        if (symbol.definition.source >= analysis_.sources.size()) {
            return;
        }
        if (inferDocumentation && symbol.documentation.empty()) {
            symbol.documentation = documentationBefore(
                analysis_.sources[symbol.definition.source].contents,
                symbol.definition.offset);
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
        } else if (const auto *resultElse = std::get_if<ResultElseStatement>(&value)) {
            visitExpression(resultElse->expression, function);
            visitBlock(resultElse->elseBlock, function);
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
        } else if (const auto *loop = std::get_if<ForStatement>(&value)) {
            visitExpression(loop->sequence, function);
            visitBlock(loop->body, function);
        } else if (const auto *selection = std::get_if<SelectStatement>(&value)) {
            for (const auto &operation : selection->operations) {
                visitExpression(operation.operation, function);
                visitBlock(operation.body, function);
            }
            if (selection->timeout.has_value()) {
                if (selection->timeout->duration.has_value()) {
                    visitExpression(*selection->timeout->duration, function);
                }
                visitBlock(selection->timeout->body, function);
            }
            visitBlock(selection->errorBlock, function);
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
                if (arm.pattern.has_value()) {
                    visitExpression(*arm.pattern, function);
                }
                if (arm.guard.has_value()) {
                    visitExpression(*arm.guard, function);
                }
                visitBlock(arm.block, function);
                if (arm.expression.has_value()) {
                    visitExpression(*arm.expression, function);
                }
            }
        } else if (const auto *conditional =
                       std::get_if<ConditionalExpression>(&value)) {
            visitExpression(conditional->condition, function);
            visitBlock(conditional->thenBlock, function);
            visitExpression(conditional->thenValue, function);
            visitBlock(conditional->elseBlock, function);
            visitExpression(conditional->elseValue, function);
        }
    }

    bool contractOwner(std::string_view name) const {
        return std::any_of(analysis_.program.contracts.begin(), analysis_.program.contracts.end(),
                           [name](const auto &contract) { return contract.name == name; });
    }

    bool generatedWorkflowType(std::string_view name) const {
        return std::any_of(
            analysis_.program.functions.begin(), analysis_.program.functions.end(),
            [name](const auto &function) {
                return function.workflow.has_value() &&
                       ((function.workflow->failureStruct.has_value() &&
                         *function.workflow->failureStruct == name) ||
                        (function.workflow->failureEnum.has_value() &&
                         *function.workflow->failureEnum == name));
            });
    }

    void addDeclarations() {
        for (std::size_t id = 0; id < analysis_.program.structs.size(); ++id) {
            const auto &declaration = analysis_.program.structs[id];
            const auto generated = generatedWorkflowType(declaration.name);
            const LanguageSymbolId symbol{LanguageSymbolKind::Struct, id, 0};
            typeSymbols_[declaration.name] = symbol;
            const auto kind = declaration.kind == StructKind::Service ? "service " : "struct ";
            addSymbol({symbol, shortName(declaration.name),
                       kind + shortName(declaration.name) +
                           typeParameterSuffix(declaration.typeParameters),
                       "type:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span) && !generated}, !generated,
                      !generated);
            for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                const auto &value = declaration.fields[field];
                const LanguageSymbolId fieldSymbol{LanguageSymbolKind::Field, id, field};
                addSymbol({fieldSymbol, value.name, fieldDetail(analysis_, value),
                           "field:" + std::to_string(id), value.span,
                           !standardSource(value.span) && !generated}, !generated,
                          !generated);
            }
        }
        for (std::size_t id = 0; id < analysis_.program.enums.size(); ++id) {
            const auto &declaration = analysis_.program.enums[id];
            if (declaration.builtin != BuiltinEnumKind::None) {
                continue;
            }
            const auto generated = generatedWorkflowType(declaration.name);
            const LanguageSymbolId symbol{LanguageSymbolKind::Enum, id, 0};
            typeSymbols_[declaration.name] = symbol;
            addSymbol({symbol, shortName(declaration.name),
                       std::string(declaration.stateMachine ? "state_machine " : "enum ") +
                           shortName(declaration.name) +
                           typeParameterSuffix(declaration.typeParameters),
                       "type:" + declaration.packageName,
                       identifierSpan(analysis_, declaration.span, declaration.name),
                       !standardSource(declaration.span) && !generated}, !generated,
                      !generated);
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                const auto &value = declaration.variants[variant];
                addSymbol({{LanguageSymbolKind::EnumVariant, id, variant}, value.name,
                           enumVariantDetail(value), "variant:" + std::to_string(id), value.span,
                           !standardSource(value.span) && !generated}, !generated,
                          !generated);
                if (value.payloadName.has_value() && value.payloadNameSpan.has_value() &&
                    value.payloadType.has_value()) {
                    addSymbol({{LanguageSymbolKind::EnumPayload, id, variant},
                               *value.payloadName,
                               *value.payloadName + ' ' +
                                   displayTypeSyntax(*value.payloadType),
                               "enum-payload:" + std::to_string(id) + ':' +
                                   std::to_string(variant),
                               *value.payloadNameSpan,
                               !standardSource(*value.payloadNameSpan) && !generated},
                              !generated, !generated);
                }
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
            if (function.closure || function.testName.has_value() ||
                function.name.find("$field_default.") != std::string::npos ||
                (function.receiver.has_value() && contractOwner(function.ownerType))) {
                continue;
            }
            const auto typeMember = !function.ownerType.empty();
            const auto kind = typeMember ? LanguageSymbolKind::Method
                                         : LanguageSymbolKind::Function;
            const LanguageSymbolId symbol{kind, id, 0};
            functionSymbols_[id] = symbol;
            auto scope = typeMember ? "method:" + function.ownerType
                                    : "function:" + function.packageName;
            const auto name = shortName(function.name);
            const auto definition = function.stateTimeout.has_value()
                                        ? function.span
                                        : identifierSpan(analysis_, function.span, name);
            addSymbol({symbol, name, functionDetail(function), std::move(scope),
                       definition,
                       !standardSource(function.span) && name != "main" && name != "drop"},
                      !function.stateTimeout.has_value());
        }
    }

    Type substituteType(Type type, const std::vector<Type> &arguments) const {
        if (type.kind == TypeKind::Parameter && type.declaration < arguments.size()) {
            return arguments[type.declaration];
        }
        for (auto &argument : type.arguments) {
            argument = substituteType(std::move(argument), arguments);
        }
        return type;
    }

    bool isCopyParameterType(const Type &type,
                             std::set<std::pair<TypeKind, std::size_t>> &active) const {
        if (isMachineScalar(type) && type != voidType && type != neverType) {
            return true;
        }
        if (type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) {
            return type.arguments.size() == 1;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return isCopyParameterType(type.arguments.front(), active);
        }
        if (type.kind != TypeKind::Struct && type.kind != TypeKind::Enum) {
            return false;
        }

        const auto key = std::pair{type.kind, type.declaration};
        if (!active.insert(key).second) {
            return true;
        }
        auto copy = true;
        if (type.kind == TypeKind::Struct && type.declaration < semantic_->structs.size()) {
            const auto &name = analysis_.program.structs[type.declaration].name;
            for (const auto &candidate : analysis_.program.functions) {
                if (candidate.ownerType == name && candidate.name.ends_with(".drop")) {
                    copy = false;
                    break;
                }
            }
            if (copy) {
                for (const auto &field : semantic_->structs[type.declaration].fieldTypes) {
                    if (!isCopyParameterType(substituteType(field, type.arguments), active)) {
                        copy = false;
                        break;
                    }
                }
            }
        } else if (type.declaration >= semantic_->enums.size()) {
            copy = false;
        } else {
            for (const auto &payload : semantic_->enums[type.declaration].payloadTypes) {
                if (payload.has_value() &&
                    !isCopyParameterType(substituteType(*payload, type.arguments), active)) {
                    copy = false;
                    break;
                }
            }
        }
        active.erase(key);
        return copy;
    }

    bool isCopyParameterType(const Type &type) const {
        std::set<std::pair<TypeKind, std::size_t>> active;
        return isCopyParameterType(type, active);
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
        if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
            type.arguments.size() == 1) {
            return std::string(type.kind == TypeKind::Raw ? "*" : "*const ") +
                   displayType(type.arguments[0], function);
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return '[' + std::to_string(type.declaration) + ']' +
                   displayType(type.arguments[0], function);
        }
        if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
            return '[' + displayType(type.arguments[0], function) + ']';
        }
        if (type.kind == TypeKind::Function && !type.arguments.empty()) {
            std::string result =
                isTransferableFunction(type) ? "transferable fn(" : "fn(";
            for (std::size_t index = 1; index < type.arguments.size(); ++index) {
                if (index != 1) {
                    result += ", ";
                }
                const auto &parameter = type.arguments[index];
                if (parameter.kind == TypeKind::View && parameter.declaration == 1 &&
                    parameter.arguments.size() == 1) {
                    result += displayType(parameter.arguments.front(), function);
                } else if (parameter.kind == TypeKind::Edit &&
                           parameter.arguments.size() == 1) {
                    result += '&' + displayType(parameter.arguments.front(), function);
                } else if (parameter.kind != TypeKind::Own &&
                           !isCopyParameterType(parameter)) {
                    result += '$' + displayType(parameter, function);
                } else {
                    result += displayType(parameter, function);
                }
            }
            result += ") " + displayType(type.arguments[0], function);
            return result;
        }
        if (type.kind == TypeKind::Task && type.arguments.size() == 1) {
            return "Task<" + displayType(type.arguments[0], function) + '>';
        }
        if ((type.kind == TypeKind::Channel || type.kind == TypeKind::Sender ||
             type.kind == TypeKind::Receiver) && type.arguments.size() == 1) {
            return std::string(typeName(type)) + '<' +
                   displayType(type.arguments[0], function) + '>';
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
            result = type.kind == TypeKind::String ? "String" : typeName(type);
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
                      bool addDefinition = true, std::string documentation = {}) {
        if (function >= localSymbols_.size() || local >= localSymbols_[function].size() ||
            function >= analysis_.program.functions.size()) {
            return;
        }
        const auto &semanticFunction = semantic_->functions[function];
        const auto &declaration = semanticFunction.locals[local];
        auto displayedType = declaration.type;
        if (declaration.readBinding &&
            (displayedType.kind == TypeKind::View ||
             displayedType.kind == TypeKind::Edit) &&
            displayedType.arguments.size() == 1) {
            displayedType = displayedType.arguments.front();
        }
        const LanguageSymbolId symbol{kind, function, local};
        localSymbols_[function][local] = symbol;
        addSymbol({symbol, declaration.name,
                   declaration.name + ' ' +
                       displayType(displayedType, analysis_.program.functions[function]),
                   "local:" + std::to_string(function), span,
                   renameable && !standardSource(span), std::move(documentation)},
                  addDefinition, false);
    }

    void addLocalDeclarations() {
        for (std::size_t function = 0; function < analysis_.program.functions.size() &&
                                       function < semantic_->functions.size();
             ++function) {
            const auto &declaration = analysis_.program.functions[function];
            const auto &semanticFunction = semantic_->functions[function];
            if (declaration.stateTimeout.has_value()) {
                continue;
            }
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
                             value.name != "self",
                             value.name == "self"
                                 ? std::string{}
                                 : parameterDocumentation(analysis_, value));
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
            } else if (const auto *resultElse =
                           std::get_if<ResultElseStatement>(&sourceStatement.value)) {
                if (resultElse->errorBinding.has_value() &&
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
                                                *resultElse->errorBinding, start));
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
            } else if (const auto *loop =
                           std::get_if<ForStatement>(&sourceStatement.value)) {
                const auto &target = semantic_->forTargets[statement];
                if (!target.has_value()) {
                    continue;
                }
                auto valueStart = sourceStatement.span.offset;
                if (loop->indexBinding.has_value()) {
                    const auto indexSpan = identifierSpan(
                        analysis_, sourceStatement.span, *loop->indexBinding, valueStart);
                    declareLocal(function, target->index, LanguageSymbolKind::Local,
                                 indexSpan);
                    valueStart = indexSpan.offset + indexSpan.length;
                }
                declareLocal(function, target->value, LanguageSymbolKind::Local,
                             identifierSpan(analysis_, sourceStatement.span,
                                            loop->valueBinding, valueStart));
            } else if (const auto *selection =
                           std::get_if<SelectStatement>(&sourceStatement.value)) {
                const auto &target = semantic_->selectTargets[statement];
                if (!target.has_value()) {
                    continue;
                }
                for (std::size_t arm = 0;
                     arm < selection->operations.size() && arm < target->bindings.size();
                     ++arm) {
                    if (!selection->operations[arm].binding.has_value() ||
                        !target->bindings[arm].has_value()) {
                        continue;
                    }
                    declareLocal(function, *target->bindings[arm], LanguageSymbolKind::Local,
                                 identifierSpan(analysis_, selection->operations[arm].span,
                                                *selection->operations[arm].binding));
                }
                auto errorStart = sourceStatement.span.offset;
                if (const auto *value = source(sourceStatement.span); value != nullptr) {
                    const auto found = value->contents.find("else", errorStart);
                    if (found != std::string::npos) {
                        errorStart = found + 4;
                    }
                }
                declareLocal(function, target->errorLocal, LanguageSymbolKind::Local,
                             identifierSpan(analysis_, sourceStatement.span,
                                            selection->errorBinding, errorStart));
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
                if (arm < target.guardBindings.size() &&
                    target.guardBindings[arm].has_value() &&
                    function < localSymbols_.size() &&
                    *target.bindings[arm] < localSymbols_[function].size() &&
                    *target.guardBindings[arm] < localSymbols_[function].size()) {
                    localSymbols_[function][*target.guardBindings[arm]] =
                        localSymbols_[function][*target.bindings[arm]];
                }
            }
        }
    }

    std::optional<LanguageSymbolId> typeSymbol(Type type) const {
        while ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                type.kind == TypeKind::Edit || type.kind == TypeKind::Array ||
                type.kind == TypeKind::Slice || type.kind == TypeKind::Raw ||
                type.kind == TypeKind::RawConst) &&
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
             type.name == "[array]" || type.name == "[slice]" ||
             type.name == "[raw]" || type.name == "[raw-const]") &&
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
                    if (analysis_.program.enums[declaration].variants[variant]
                            .payloadName.has_value()) {
                        addTypeLink({LanguageSymbolKind::EnumPayload, declaration, variant},
                                    typeSymbol(*semantic.payloadTypes[variant]));
                    }
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

    void addNamedArgumentReferences(
        AstExpressionId expression,
        const std::vector<std::optional<std::string>> &names,
        const std::vector<std::optional<SourceSpan>> &spans) {
        if (expression >= semantic_->callTargets.size() ||
            !semantic_->callTargets[expression].has_value()) {
            return;
        }
        const auto &target = *semantic_->callTargets[expression];
        if ((target.kind != CallTargetKind::Function &&
             target.kind != CallTargetKind::Method) ||
            target.function >= semantic_->functions.size()) {
            return;
        }
        const auto &function = semantic_->functions[target.function];
        for (std::size_t source = 0;
             source < names.size() && source < spans.size() &&
             source < target.argumentParameters.size();
             ++source) {
            if (!names[source].has_value() || !spans[source].has_value()) {
                continue;
            }
            auto parameter = target.argumentParameters[source];
            if (target.kind == CallTargetKind::Method) {
                ++parameter;
            }
            if (parameter >= function.parameters.size()) {
                continue;
            }
            if (const auto symbol = resolveLocal(target.function,
                                                 function.parameters[parameter]);
                symbol.has_value()) {
                addOccurrence(*symbol, *spans[source]);
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
                    addNamedArgumentReferences(id, call->argumentNames,
                                               call->argumentNameSpans);
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
                    const auto &target = *semantic_->callTargets[id];
                    addCallReference(id, target, expression.span);
                    addNamedArgumentReferences(id, member->argumentNames,
                                               member->argumentNameSpans);
                    if (target.kind == CallTargetKind::Function &&
                        target.function < analysis_.program.functions.size() &&
                        member->base.has_value()) {
                        const auto &function = analysis_.program.functions[target.function];
                        const auto owner = typeSymbols_.find(function.ownerType);
                        if (!function.ownerType.empty() && owner != typeSymbols_.end()) {
                            addNamedOccurrence(
                                owner->second,
                                analysis_.program.expressions[*member->base].span);
                        }
                    }
                } else if (semantic_->enumTargets[id].has_value()) {
                    const auto &target = *semantic_->enumTargets[id];
                    addNamedOccurrence({LanguageSymbolKind::EnumVariant,
                                        target.type.declaration, target.variant},
                                       expression.span);
                    const auto &variant = analysis_.program.enums[target.type.declaration]
                                              .variants[target.variant];
                    for (std::size_t argument = 0;
                         variant.payloadName.has_value() &&
                         argument < member->argumentNames.size() &&
                         argument < member->argumentNameSpans.size();
                         ++argument) {
                        if (member->argumentNames[argument] == variant.payloadName &&
                            member->argumentNameSpans[argument].has_value()) {
                            addNamedOccurrence(
                                {LanguageSymbolKind::EnumPayload,
                                 target.type.declaration, target.variant},
                                *member->argumentNameSpans[argument]);
                        }
                    }
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
                        if (match->arms[arm].wildcard) {
                            continue;
                        }
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
        "package", "import", "as",      "extern", "struct", "service", "enum",  "contract",
        "attribute", "implements", "extends", "delegate", "fn", "action", "task", "test", "spawn",
        "const", "var", "return", "discard", "if", "else", "while", "for", "in", "break",
        "continue", "select", "timeout", "unsafe", "match", "capture", "replace", "with",
        "new", "own", "view", "edit", "true", "false", "print", "panic", "len", "i8", "i16",
        "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize", "f32", "f64",
        "bool", "String", "void", "never", "Option", "Result", "ChannelError", "NumberError",
        "Task",
        "Channel", "Sender", "Receiver", "channel", "null", "isNull"};
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

std::string languageDocumentation(const ProjectAnalysis &analysis,
                                  SourceSpan definition) {
    return definition.source < analysis.sources.size()
               ? documentationBefore(analysis.sources[definition.source].contents,
                                     definition.offset)
               : std::string{};
}

std::string languageParameterDocumentation(const ProjectAnalysis &analysis,
                                            const Parameter &parameter) {
    return parameterDocumentation(analysis, parameter);
}

std::string languageTypeSyntax(const TypeSyntax &type) {
    return displayTypeSyntax(type);
}

} // namespace foundation
