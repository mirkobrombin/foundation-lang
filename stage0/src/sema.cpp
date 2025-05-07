#include "foundation/sema.hpp"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace foundation {

namespace {

struct FunctionSignature {
    Type returnType{invalidType};
    std::vector<Type> parameters;
};

class Analyzer {
  public:
    Analyzer(const Program &program, Diagnostics &diagnostics)
        : program_(program), diagnostics_(diagnostics) {
        model_.expressionTypes.resize(program.expressions.size(), invalidType);
        model_.expressionLocals.resize(program.expressions.size());
        model_.callTargets.resize(program.expressions.size());
        model_.structTargets.resize(program.expressions.size());
        model_.expressionFields.resize(program.expressions.size());
        model_.enumTargets.resize(program.expressions.size());
        model_.matchTargets.resize(program.expressions.size());
        model_.statementLocals.resize(program.statements.size());
        model_.structs.resize(program.structs.size());
        model_.enums.resize(program.enums.size());
        model_.functions.resize(program.functions.size());
    }

    std::optional<SemanticModel> run() {
        declareStructs();
        declareEnums();
        resolveStructs();
        resolveEnums();
        rejectTypeCycles();
        declareFunctions();
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            analyzeFunction(index);
        }
        if (diagnostics_.hasErrors()) {
            return std::nullopt;
        }
        return std::move(model_);
    }

  private:
    void declareStructs() {
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &declaration = program_.structs[index];
            if (isBuiltinType(declaration.name) ||
                !structs_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2020", "duplicate type " + declaration.name,
                                   declaration.span);
            }
            if (declaration.fields.empty()) {
                diagnostics_.error("FDN2029", "struct must declare at least one field",
                                   declaration.span);
            }
        }
    }

    void declareEnums() {
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            const auto &declaration = program_.enums[index];
            if (isBuiltinType(declaration.name) || structs_.contains(declaration.name) ||
                !enums_.emplace(declaration.name, index).second) {
                diagnostics_.error("FDN2020", "duplicate type " + declaration.name,
                                   declaration.span);
            }
            if (declaration.variants.empty()) {
                diagnostics_.error("FDN2031", "enum must declare at least one variant",
                                   declaration.span);
            }
        }
    }

    void resolveStructs() {
        for (std::size_t index = 0; index < program_.structs.size(); ++index) {
            const auto &declaration = program_.structs[index];
            auto &semantic = model_.structs[index];
            std::unordered_map<std::string, std::size_t> fields;
            semantic.fieldTypes.reserve(declaration.fields.size());
            for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                const auto &source = declaration.fields[field];
                if (!fields.emplace(source.name, field).second) {
                    diagnostics_.error("FDN2021", "duplicate field " + source.name,
                                       source.span);
                }
                const auto type = resolveType(source.typeName, source.span);
                if (type == voidType) {
                    diagnostics_.error("FDN2022", "struct field cannot have type void",
                                       source.span);
                }
                semantic.fieldTypes.push_back(type);
            }
        }
    }

    void resolveEnums() {
        for (std::size_t index = 0; index < program_.enums.size(); ++index) {
            const auto &declaration = program_.enums[index];
            auto &semantic = model_.enums[index];
            std::unordered_map<std::string, std::size_t> variants;
            semantic.payloadTypes.reserve(declaration.variants.size());
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                const auto &source = declaration.variants[variant];
                if (!variants.emplace(source.name, variant).second) {
                    diagnostics_.error("FDN2032", "duplicate variant " + source.name,
                                       source.span);
                }
                if (!source.payloadType.has_value()) {
                    semantic.payloadTypes.push_back(std::nullopt);
                    continue;
                }
                const auto type = resolveType(*source.payloadType, source.span);
                if (type == voidType) {
                    diagnostics_.error("FDN2033", "enum payload cannot have type void",
                                       source.span);
                }
                semantic.payloadTypes.push_back(type);
            }
        }
    }

    void rejectTypeCycles() {
        const auto structCount = program_.structs.size();
        const auto typeCount = structCount + program_.enums.size();
        std::vector<std::vector<std::pair<std::size_t, SourceSpan>>> edges(typeCount);
        for (std::size_t type = 0; type < model_.structs.size(); ++type) {
            for (std::size_t field = 0; field < model_.structs[type].fieldTypes.size(); ++field) {
                const auto target = typeNode(model_.structs[type].fieldTypes[field]);
                if (target.has_value()) {
                    edges[type].push_back({*target, program_.structs[type].fields[field].span});
                }
            }
        }
        for (std::size_t type = 0; type < model_.enums.size(); ++type) {
            for (std::size_t variant = 0; variant < model_.enums[type].payloadTypes.size();
                 ++variant) {
                if (!model_.enums[type].payloadTypes[variant].has_value()) {
                    continue;
                }
                const auto target = typeNode(*model_.enums[type].payloadTypes[variant]);
                if (target.has_value()) {
                    edges[structCount + type].push_back(
                        {*target, program_.enums[type].variants[variant].span});
                }
            }
        }

        std::vector<std::size_t> dependencies(typeCount);
        std::vector<std::vector<std::size_t>> dependents(typeCount);
        for (std::size_t type = 0; type < edges.size(); ++type) {
            dependencies[type] = edges[type].size();
            for (const auto &[target, span] : edges[type]) {
                static_cast<void>(span);
                dependents[target].push_back(type);
            }
        }

        std::vector<FirStructId> ready;
        for (std::size_t type = 0; type < dependencies.size(); ++type) {
            if (dependencies[type] == 0) {
                ready.push_back(type);
            }
        }
        for (std::size_t current = 0; current < ready.size(); ++current) {
            for (const auto dependent : dependents[ready[current]]) {
                if (--dependencies[dependent] == 0) {
                    ready.push_back(dependent);
                }
            }
        }
        if (ready.size() == typeCount) {
            return;
        }

        for (std::size_t type = 0; type < dependencies.size(); ++type) {
            if (dependencies[type] == 0) {
                continue;
            }
            for (const auto &[target, span] : edges[type]) {
                if (dependencies[target] != 0) {
                    diagnostics_.error("FDN2023", "recursive value type is not allowed", span);
                    return;
                }
            }
        }
    }

    void declareFunctions() {
        bool foundMain = false;
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto &function = program_.functions[index];
            auto &semantic = model_.functions[index];
            semantic.returnType = resolveType(function.returnType, function.span);
            for (const auto &parameter : function.parameters) {
                const auto type = resolveType(parameter.typeName, parameter.span);
                if (type == voidType) {
                    diagnostics_.error("FDN2016", "parameter cannot have type void",
                                       parameter.span);
                }
                semantic.parameterTypes.push_back(type);
            }

            if (!functions_.emplace(function.name, index).second) {
                diagnostics_.error("FDN2001", "duplicate function " + function.name,
                                   function.span);
            }
            if (function.name == "print") {
                diagnostics_.error("FDN2018", "print is a reserved builtin", function.span);
            }
            signatures_.push_back({semantic.returnType, semantic.parameterTypes});

            if (function.name != "main") {
                continue;
            }
            if (!foundMain) {
                model_.main = index;
            }
            foundMain = true;
            if (!function.parameters.empty() || semantic.returnType != i32Type) {
                diagnostics_.error("FDN2007", "main must have signature fn main() -> i32",
                                   function.span);
            }
        }
        if (!foundMain) {
            diagnostics_.error("FDN2006", "program must declare main", {0, 0, 1, 1});
        }
    }

    void analyzeFunction(std::size_t index) {
        currentFunction_ = index;
        scopes_.clear();
        scopes_.emplace_back();

        const auto &function = program_.functions[index];
        auto &semantic = model_.functions[index];
        for (std::size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
            const auto &parameter = function.parameters[parameterIndex];
            const auto local = addLocal(parameter.name, semantic.parameterTypes[parameterIndex],
                                        false, parameter.span);
            semantic.parameters.push_back(local);
        }

        const auto returns = analyzeBlock(function.body, false);
        if (semantic.returnType != voidType && !returns) {
            diagnostics_.error("FDN2008", "function does not return on every path", function.span);
        }
    }

    bool analyzeBlock(AstBlockId id, bool nested) {
        if (nested) {
            scopes_.emplace_back();
        }

        bool returns = false;
        for (const auto statement : program_.blocks[id].statements) {
            if (analyzeStatement(statement)) {
                returns = true;
            }
        }

        if (nested) {
            scopes_.pop_back();
        }
        return returns;
    }

    bool analyzeStatement(AstStatementId id) {
        const auto &statement = program_.statements[id];
        if (const auto *variable = std::get_if<VariableStatement>(&statement.value)) {
            const auto initializer = analyzeExpression(variable->initializer);
            auto declared = initializer;
            if (variable->typeName.has_value()) {
                declared = resolveType(*variable->typeName, statement.span);
                requireSame(declared, initializer, statement.span, "binding initializer");
            }
            if (declared == voidType) {
                diagnostics_.error("FDN2016", "binding initializer cannot be void", statement.span);
                declared = invalidType;
            }
            model_.statementLocals[id] =
                addLocal(variable->name, declared, variable->mutableBinding, statement.span);
            return false;
        }
        if (const auto *assignment = std::get_if<AssignmentStatement>(&statement.value)) {
            const auto local = findLocal(assignment->name, statement.span);
            const auto value = analyzeExpression(assignment->value);
            if (local.has_value()) {
                model_.statementLocals[id] = *local;
                const auto &declaration = model_.functions[currentFunction_].locals[*local];
                if (!declaration.mutableBinding) {
                    diagnostics_.error("FDN2013", "cannot assign to immutable binding " +
                                                        assignment->name,
                                       statement.span);
                }
                requireSame(declaration.type, value, statement.span, "assignment");
            }
            return false;
        }
        if (const auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            static_cast<void>(analyzeExpression(expression->expression));
            return false;
        }
        if (const auto *returned = std::get_if<ReturnStatement>(&statement.value)) {
            const auto expected = model_.functions[currentFunction_].returnType;
            if (!returned->value.has_value()) {
                if (expected != voidType) {
                    diagnostics_.error("FDN2014", "non-void function must return a value",
                                       statement.span);
                }
                return true;
            }
            const auto value = analyzeExpression(*returned->value);
            if (expected == voidType) {
                diagnostics_.error("FDN2015", "void function must use a bare return",
                                   statement.span);
                return true;
            }
            requireSame(expected, value, statement.span, "return");
            return true;
        }
        if (const auto *branch = std::get_if<IfStatement>(&statement.value)) {
            requireSame(boolType, analyzeExpression(branch->condition), statement.span,
                        "if condition");
            const auto thenReturns = analyzeBlock(branch->thenBlock, true);
            const auto elseReturns =
                branch->elseBlock.has_value() && analyzeBlock(*branch->elseBlock, true);
            return thenReturns && elseReturns;
        }

        const auto &loop = std::get<WhileStatement>(statement.value);
        requireSame(boolType, analyzeExpression(loop.condition), statement.span,
                    "while condition");
        static_cast<void>(analyzeBlock(loop.body, true));
        return false;
    }

    Type analyzeExpression(AstExpressionId id) {
        const auto &expression = program_.expressions[id];
        auto type = invalidType;
        if (const auto *integer = std::get_if<IntegerExpression>(&expression.value)) {
            if (integer->value < INT32_MIN || integer->value > INT32_MAX) {
                diagnostics_.error("FDN2005", "integer literal does not fit i32", expression.span);
            }
            type = i32Type;
        } else if (std::holds_alternative<BooleanExpression>(expression.value)) {
            type = boolType;
        } else if (std::holds_alternative<StringExpression>(expression.value)) {
            type = stringType;
        } else if (const auto *name = std::get_if<NameExpression>(&expression.value)) {
            const auto local = findLocal(name->name, expression.span);
            if (local.has_value()) {
                model_.expressionLocals[id] = *local;
                type = model_.functions[currentFunction_].locals[*local].type;
            }
        } else if (const auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
            type = analyzeUnary(*unary, expression.span);
        } else if (const auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
            type = analyzeBinary(*binary, expression.span);
        } else if (const auto *call = std::get_if<CallExpression>(&expression.value)) {
            type = analyzeCall(id, *call, expression.span);
        } else if (const auto *literal = std::get_if<StructExpression>(&expression.value)) {
            type = analyzeStruct(id, *literal, expression.span);
        } else if (const auto *field = std::get_if<FieldExpression>(&expression.value)) {
            type = analyzeField(id, *field, expression.span);
        } else if (const auto *constructor = std::get_if<EnumExpression>(&expression.value)) {
            type = analyzeEnum(id, *constructor, expression.span);
        } else {
            type = analyzeMatch(id, std::get<MatchExpression>(expression.value), expression.span);
        }
        model_.expressionTypes[id] = type;
        return type;
    }

    Type analyzeUnary(const UnaryExpression &unary, SourceSpan span) {
        const auto operand = analyzeExpression(unary.operand);
        if (unary.operation == UnaryOperator::Negate) {
            requireSame(i32Type, operand, span, "unary -");
            return i32Type;
        }
        requireSame(boolType, operand, span, "unary !");
        return boolType;
    }

    Type analyzeBinary(const BinaryExpression &binary, SourceSpan span) {
        const auto left = analyzeExpression(binary.left);
        const auto right = analyzeExpression(binary.right);
        switch (binary.operation) {
        case BinaryOperator::Add:
        case BinaryOperator::Subtract:
        case BinaryOperator::Multiply:
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:
            requireSame(i32Type, left, span, "arithmetic operand");
            requireSame(i32Type, right, span, "arithmetic operand");
            return i32Type;
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual:
            requireSame(i32Type, left, span, "comparison operand");
            requireSame(i32Type, right, span, "comparison operand");
            return boolType;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            if (left.kind == TypeKind::String || right.kind == TypeKind::String ||
                left.kind == TypeKind::Struct || right.kind == TypeKind::Struct ||
                left.kind == TypeKind::Enum || right.kind == TypeKind::Enum) {
                diagnostics_.error("FDN2012", "equality is not available for this type", span);
            } else {
                requireSame(left, right, span, "equality operand");
                if (left == voidType) {
                    diagnostics_.error("FDN2012", "void values cannot be compared", span);
                }
            }
            return boolType;
        case BinaryOperator::And:
        case BinaryOperator::Or:
            requireSame(boolType, left, span, "logical operand");
            requireSame(boolType, right, span, "logical operand");
            return boolType;
        }
        return invalidType;
    }

    Type analyzeCall(AstExpressionId id, const CallExpression &call, SourceSpan span) {
        std::vector<Type> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(analyzeExpression(argument));
        }

        if (call.callee == "print") {
            if (arguments.size() != 1) {
                diagnostics_.error("FDN2010", "print expects one argument", span);
            } else {
                requireSame(stringType, arguments.front(), span, "print argument");
            }
            model_.callTargets[id] = CallTarget{CallTargetKind::Print, 0};
            return voidType;
        }

        const auto found = functions_.find(call.callee);
        if (found == functions_.end()) {
            diagnostics_.error("FDN2009", "unknown function " + call.callee, span);
            return invalidType;
        }
        const auto function = found->second;
        if (program_.functions[function].name == "main") {
            diagnostics_.error("FDN2019", "main cannot be called", span);
        }
        const auto &signature = signatures_[function];
        if (arguments.size() != signature.parameters.size()) {
            diagnostics_.error("FDN2010", "wrong argument count for " + call.callee, span);
        }
        const auto count = std::min(arguments.size(), signature.parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            requireSame(signature.parameters[index], arguments[index], span,
                        "function argument");
        }
        model_.callTargets[id] = CallTarget{CallTargetKind::Function, function};
        return signature.returnType;
    }

    Type analyzeStruct(AstExpressionId id, const StructExpression &literal, SourceSpan span) {
        const auto found = structs_.find(literal.typeName);
        if (found == structs_.end()) {
            for (const auto &field : literal.fields) {
                static_cast<void>(analyzeExpression(field.value));
            }
            diagnostics_.error("FDN2024", "unknown struct " + literal.typeName, span);
            return invalidType;
        }

        const auto type = found->second;
        const auto &declaration = program_.structs[type];
        const auto &semantic = model_.structs[type];
        std::vector<bool> initialized(declaration.fields.size());
        std::vector<FirFieldId> fields;
        fields.reserve(literal.fields.size());
        for (const auto &initializer : literal.fields) {
            const auto value = analyzeExpression(initializer.value);
            const auto field = findField(type, initializer.name);
            if (!field.has_value()) {
                diagnostics_.error("FDN2025", "unknown field " + initializer.name,
                                   initializer.span);
                continue;
            }
            fields.push_back(*field);
            if (initialized[*field]) {
                diagnostics_.error("FDN2026", "duplicate field initializer " + initializer.name,
                                   initializer.span);
                continue;
            }
            initialized[*field] = true;
            requireSame(semantic.fieldTypes[*field], value, initializer.span,
                        "field initializer");
        }
        for (std::size_t field = 0; field < initialized.size(); ++field) {
            if (!initialized[field]) {
                diagnostics_.error("FDN2027", "missing field " + declaration.fields[field].name,
                                   span);
            }
        }
        model_.structTargets[id] = StructLiteralTarget{type, std::move(fields)};
        return Type{TypeKind::Struct, type};
    }

    Type analyzeField(AstExpressionId id, const FieldExpression &field, SourceSpan span) {
        const auto base = analyzeExpression(field.base);
        if (base.kind == TypeKind::Invalid) {
            return invalidType;
        }
        if (base.kind != TypeKind::Struct || base.declaration >= program_.structs.size()) {
            diagnostics_.error("FDN2028", "field access requires a struct value", span);
            return invalidType;
        }
        const auto resolved = findField(base.declaration, field.field);
        if (!resolved.has_value()) {
            diagnostics_.error("FDN2025", "unknown field " + field.field, span);
            return invalidType;
        }
        model_.expressionFields[id] = *resolved;
        return model_.structs[base.declaration].fieldTypes[*resolved];
    }

    Type analyzeEnum(AstExpressionId id, const EnumExpression &constructor, SourceSpan span) {
        const auto found = enums_.find(constructor.typeName);
        if (found == enums_.end()) {
            if (constructor.payload.has_value()) {
                static_cast<void>(analyzeExpression(*constructor.payload));
            }
            diagnostics_.error("FDN2034", "unknown enum " + constructor.typeName, span);
            return invalidType;
        }
        const auto variant = findVariant(found->second, constructor.variant);
        if (!variant.has_value()) {
            if (constructor.payload.has_value()) {
                static_cast<void>(analyzeExpression(*constructor.payload));
            }
            diagnostics_.error("FDN2035", "unknown variant " + constructor.variant, span);
            return invalidType;
        }

        const auto expected = model_.enums[found->second].payloadTypes[*variant];
        if (expected.has_value() && !constructor.payload.has_value()) {
            diagnostics_.error("FDN2036", "variant requires a payload", span);
        } else if (!expected.has_value() && constructor.payload.has_value()) {
            static_cast<void>(analyzeExpression(*constructor.payload));
            diagnostics_.error("FDN2036", "unit variant does not accept a payload", span);
        } else if (expected.has_value()) {
            requireSame(*expected, analyzeExpression(*constructor.payload), span,
                        "variant payload");
        }
        model_.enumTargets[id] = EnumTarget{found->second, *variant};
        return Type{TypeKind::Enum, found->second};
    }

    Type analyzeMatch(AstExpressionId id, const MatchExpression &match, SourceSpan span) {
        const auto value = analyzeExpression(match.value);
        if (value.kind != TypeKind::Enum || value.declaration >= program_.enums.size()) {
            for (const auto &arm : match.arms) {
                scopes_.emplace_back();
                static_cast<void>(analyzeExpression(arm.expression));
                scopes_.pop_back();
            }
            diagnostics_.error("FDN2037", "match requires an enum value", span);
            return invalidType;
        }

        const auto enumType = value.declaration;
        const auto &declaration = program_.enums[enumType];
        std::vector<bool> covered(declaration.variants.size());
        std::vector<FirVariantId> variants;
        std::vector<std::optional<FirLocalId>> bindings;
        auto result = invalidType;
        for (const auto &arm : match.arms) {
            const auto patternType = enums_.find(arm.typeName);
            std::optional<FirVariantId> variant;
            if (patternType == enums_.end() || patternType->second != enumType) {
                diagnostics_.error("FDN2038", "pattern type does not match " + declaration.name,
                                   arm.span);
            } else {
                variant = findVariant(enumType, arm.variant);
                if (!variant.has_value()) {
                    diagnostics_.error("FDN2035", "unknown variant " + arm.variant, arm.span);
                } else if (covered[*variant]) {
                    diagnostics_.error("FDN2039", "duplicate match pattern " + arm.variant,
                                       arm.span);
                } else {
                    covered[*variant] = true;
                }
            }

            scopes_.emplace_back();
            std::optional<FirLocalId> binding;
            if (variant.has_value()) {
                const auto payload = model_.enums[enumType].payloadTypes[*variant];
                if (payload.has_value() && !arm.binding.has_value()) {
                    diagnostics_.error("FDN2041", "payload pattern requires a binding", arm.span);
                } else if (!payload.has_value() && arm.binding.has_value()) {
                    diagnostics_.error("FDN2041", "unit pattern does not accept a binding",
                                       arm.span);
                } else if (payload.has_value()) {
                    binding = addLocal(*arm.binding, *payload, false, arm.span);
                }
            }
            const auto armType = analyzeExpression(arm.expression);
            scopes_.pop_back();
            if (result.kind == TypeKind::Invalid) {
                result = armType;
            } else {
                requireSame(result, armType, arm.span, "match arm");
            }
            variants.push_back(variant.value_or(0));
            bindings.push_back(binding);
        }
        for (std::size_t variant = 0; variant < covered.size(); ++variant) {
            if (!covered[variant]) {
                diagnostics_.error("FDN2040",
                                   "match does not cover " + declaration.variants[variant].name,
                                   span);
            }
        }
        model_.matchTargets[id] = MatchTarget{enumType, std::move(variants), std::move(bindings)};
        return result;
    }

    Type resolveType(std::string_view name, SourceSpan span) {
        if (name == "void") {
            return voidType;
        }
        if (name == "i32") {
            return i32Type;
        }
        if (name == "bool") {
            return boolType;
        }
        if (name == "String") {
            return stringType;
        }
        const auto found = structs_.find(std::string(name));
        if (found != structs_.end()) {
            return Type{TypeKind::Struct, found->second};
        }
        const auto enumFound = enums_.find(std::string(name));
        if (enumFound != enums_.end()) {
            return Type{TypeKind::Enum, enumFound->second};
        }
        diagnostics_.error("FDN2002", "unknown type " + std::string(name), span);
        return invalidType;
    }

    bool isBuiltinType(std::string_view name) const {
        return name == "void" || name == "i32" || name == "bool" || name == "String";
    }

    std::optional<FirFieldId> findField(FirStructId type, std::string_view name) const {
        const auto &fields = program_.structs[type].fields;
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (fields[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<FirVariantId> findVariant(FirEnumId type, std::string_view name) const {
        const auto &variants = program_.enums[type].variants;
        for (std::size_t index = 0; index < variants.size(); ++index) {
            if (variants[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> typeNode(Type type) const {
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            return type.declaration;
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            return program_.structs.size() + type.declaration;
        }
        return std::nullopt;
    }

    FirLocalId addLocal(const std::string &name, Type type, bool mutableBinding, SourceSpan span) {
        auto &scope = scopes_.back();
        if (scope.contains(name)) {
            diagnostics_.error("FDN2003", "duplicate binding " + name, span);
            return scope[name];
        }

        auto &locals = model_.functions[currentFunction_].locals;
        const auto id = locals.size();
        locals.push_back({name, type, mutableBinding});
        scope.emplace(name, id);
        return id;
    }

    std::optional<FirLocalId> findLocal(const std::string &name, SourceSpan span) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) {
                return found->second;
            }
        }
        diagnostics_.error("FDN2004", "unknown binding " + name, span);
        return std::nullopt;
    }

    std::string displayType(Type type) const {
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            return program_.structs[type.declaration].name;
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            return program_.enums[type.declaration].name;
        }
        return typeName(type);
    }

    void requireSame(Type expected, Type actual, SourceSpan span, std::string_view context) {
        if (expected.kind == TypeKind::Invalid || actual.kind == TypeKind::Invalid ||
            expected == actual) {
            return;
        }
        diagnostics_.error("FDN2011",
                           std::string(context) + " expects " + displayType(expected) + ", got " +
                               displayType(actual),
                           span);
    }

    const Program &program_;
    Diagnostics &diagnostics_;
    SemanticModel model_;
    std::unordered_map<std::string, FirStructId> structs_;
    std::unordered_map<std::string, FirEnumId> enums_;
    std::unordered_map<std::string, FirFunctionId> functions_;
    std::vector<FunctionSignature> signatures_;
    std::vector<std::unordered_map<std::string, FirLocalId>> scopes_;
    FirFunctionId currentFunction_{};
};

} // namespace

std::optional<SemanticModel> analyze(const Program &program, Diagnostics &diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

} // namespace foundation
