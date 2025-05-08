#include "foundation/project.hpp"

#include "foundation/lexer.hpp"
#include "foundation/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef FOUNDATION_STANDARD_LIBRARY
#define FOUNDATION_STANDARD_LIBRARY ""
#endif

namespace foundation {

namespace {

struct ParsedFile {
    Program program;
    std::string sourcePath;
};

struct DeclarationInfo {
    std::string internalName;
    bool exported{};
};

struct PackageSymbols {
    std::unordered_map<std::string, DeclarationInfo> types;
    std::unordered_map<std::string, DeclarationInfo> functions;
};

using SymbolTable = std::unordered_map<std::string, PackageSymbols>;
using ImportAliases = std::unordered_map<std::string, std::string>;

std::optional<std::string> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return std::nullopt;
    }
    return contents.str();
}

std::filesystem::path sourceIdentity(const std::filesystem::path &path) {
    std::error_code error;
    auto result = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    const auto canonical = std::filesystem::weakly_canonical(result, error);
    return error ? result.lexically_normal() : canonical;
}

std::string defaultAlias(std::string_view packageName) {
    const auto separator = packageName.rfind('.');
    return std::string(packageName.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

std::string internalName(std::string_view packageName, std::string_view name) {
    if (packageName.empty()) {
        return std::string(name);
    }
    return std::string(packageName) + '.' + std::string(name);
}

bool intrinsicType(std::string_view name) {
    return name == "void" || name == "i32" || name == "u64" || name == "bool" ||
           name == "String" ||
           name == "Option" || name == "Result" || name == "[array]" || name == "[slice]" ||
           name == "[function]" || name == "own" || name == "view" || name == "edit";
}

void remapExpression(Expression &expression, std::size_t expressionOffset,
                     std::size_t functionOffset) {
    if (auto *array = std::get_if<ArrayExpression>(&expression.value)) {
        for (auto &element : array->elements) {
            element += expressionOffset;
        }
    } else if (auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
        unary->operand += expressionOffset;
    } else if (auto *ownership = std::get_if<OwnershipExpression>(&expression.value)) {
        ownership->operand += expressionOffset;
    } else if (auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
        binary->left += expressionOffset;
        binary->right += expressionOffset;
    } else if (auto *call = std::get_if<CallExpression>(&expression.value)) {
        for (auto &argument : call->arguments) {
            argument += expressionOffset;
        }
    } else if (auto *literal = std::get_if<StructExpression>(&expression.value)) {
        for (auto &field : literal->fields) {
            field.value += expressionOffset;
        }
    } else if (auto *member = std::get_if<MemberExpression>(&expression.value)) {
        if (member->base.has_value()) {
            *member->base += expressionOffset;
        }
        for (auto &argument : member->arguments) {
            argument += expressionOffset;
        }
    } else if (auto *index = std::get_if<IndexExpression>(&expression.value)) {
        index->base += expressionOffset;
        index->index += expressionOffset;
    } else if (auto *replace = std::get_if<ReplaceExpression>(&expression.value)) {
        replace->target += expressionOffset;
        replace->value += expressionOffset;
    } else if (auto *match = std::get_if<MatchExpression>(&expression.value)) {
        match->value += expressionOffset;
        for (auto &arm : match->arms) {
            arm.expression += expressionOffset;
        }
    } else if (auto *function = std::get_if<FunctionExpression>(&expression.value)) {
        function->function += functionOffset;
    }
}

void remapStatement(Statement &statement, std::size_t expressionOffset,
                    std::size_t blockOffset) {
    if (auto *variable = std::get_if<VariableStatement>(&statement.value)) {
        variable->initializer += expressionOffset;
        if (variable->elseBlock.has_value()) {
            *variable->elseBlock += blockOffset;
        }
    } else if (auto *destructure =
                   std::get_if<StructDestructureStatement>(&statement.value)) {
        destructure->initializer += expressionOffset;
    } else if (auto *assignment = std::get_if<AssignmentStatement>(&statement.value)) {
        assignment->target += expressionOffset;
        assignment->value += expressionOffset;
    } else if (auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
        expression->expression += expressionOffset;
    } else if (auto *returned = std::get_if<ReturnStatement>(&statement.value)) {
        if (returned->value.has_value()) {
            *returned->value += expressionOffset;
        }
    } else if (auto *discarded = std::get_if<DiscardStatement>(&statement.value)) {
        discarded->value += expressionOffset;
    } else if (auto *branch = std::get_if<IfStatement>(&statement.value)) {
        branch->condition += expressionOffset;
        branch->thenBlock += blockOffset;
        if (branch->elseBlock.has_value()) {
            *branch->elseBlock += blockOffset;
        }
    } else if (auto *loop = std::get_if<WhileStatement>(&statement.value)) {
        loop->condition += expressionOffset;
        loop->body += blockOffset;
    }
}

void appendProgram(Program &target, Program source) {
    const auto expressionOffset = target.expressions.size();
    const auto statementOffset = target.statements.size();
    const auto blockOffset = target.blocks.size();
    const auto functionOffset = target.functions.size();
    for (auto &expression : source.expressions) {
        remapExpression(expression, expressionOffset, functionOffset);
        target.expressions.push_back(std::move(expression));
    }
    for (auto &statement : source.statements) {
        remapStatement(statement, expressionOffset, blockOffset);
        target.statements.push_back(std::move(statement));
    }
    for (auto &block : source.blocks) {
        for (auto &statement : block.statements) {
            statement += statementOffset;
        }
        target.blocks.push_back(std::move(block));
    }
    for (auto &declaration : source.structs) {
        target.structs.push_back(std::move(declaration));
    }
    for (auto &declaration : source.enums) {
        target.enums.push_back(std::move(declaration));
    }
    for (auto &declaration : source.contracts) {
        target.contracts.push_back(std::move(declaration));
    }
    for (auto &function : source.functions) {
        function.body += blockOffset;
        target.functions.push_back(std::move(function));
    }
}

const DeclarationInfo *findDeclaration(const SymbolTable &symbols, std::string_view packageName,
                                       std::string_view name, bool function) {
    const auto package = symbols.find(std::string(packageName));
    if (package == symbols.end()) {
        return nullptr;
    }
    const auto &declarations = function ? package->second.functions : package->second.types;
    const auto declaration = declarations.find(std::string(name));
    return declaration == declarations.end() ? nullptr : &declaration->second;
}

void reportPrivate(Diagnostics &diagnostics, std::string_view packageName, std::string_view name,
                   SourceSpan span) {
    diagnostics.error("FDN3008",
                      std::string(packageName) + '.' + std::string(name) + " is not exported",
                      span);
}

void linkType(TypeSyntax &type, const std::string &currentPackage, const ImportAliases &imports,
              const SymbolTable &symbols, const std::unordered_set<std::string> &typeParameters,
              Diagnostics &diagnostics) {
    for (auto &argument : type.arguments) {
        linkType(argument, currentPackage, imports, symbols, typeParameters, diagnostics);
    }
    if (intrinsicType(type.name) || typeParameters.contains(type.name)) {
        return;
    }
    const auto separator = type.name.find('.');
    if (separator == std::string::npos) {
        if (const auto *declaration = findDeclaration(symbols, currentPackage, type.name, false)) {
            type.name = declaration->internalName;
        }
        return;
    }
    const auto alias = type.name.substr(0, separator);
    const auto name = type.name.substr(separator + 1);
    const auto imported = imports.find(alias);
    if (imported == imports.end()) {
        diagnostics.error("FDN3009", "unknown import alias " + alias, type.span);
        return;
    }
    const auto *declaration = findDeclaration(symbols, imported->second, name, false);
    if (declaration == nullptr) {
        diagnostics.error("FDN3009", "unknown type " + imported->second + '.' + name, type.span);
        return;
    }
    if (!declaration->exported) {
        reportPrivate(diagnostics, imported->second, name, type.span);
    }
    type.name = declaration->internalName;
}

void linkExpression(Program &program, AstExpressionId id, const std::string &currentPackage,
                    const ImportAliases &imports, const SymbolTable &symbols,
                    const std::unordered_set<std::string> &typeParameters,
                    Diagnostics &diagnostics) {
    auto &expression = program.expressions[id];
    if (auto *array = std::get_if<ArrayExpression>(&expression.value)) {
        for (const auto element : array->elements) {
            linkExpression(program, element, currentPackage, imports, symbols, typeParameters,
                           diagnostics);
        }
    } else if (auto *name = std::get_if<NameExpression>(&expression.value)) {
        for (auto &argument : name->typeArguments) {
            linkType(argument, currentPackage, imports, symbols, typeParameters, diagnostics);
        }
    } else if (auto *unary = std::get_if<UnaryExpression>(&expression.value)) {
        linkExpression(program, unary->operand, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
    } else if (auto *ownership = std::get_if<OwnershipExpression>(&expression.value)) {
        linkExpression(program, ownership->operand, currentPackage, imports, symbols,
                       typeParameters, diagnostics);
    } else if (auto *binary = std::get_if<BinaryExpression>(&expression.value)) {
        linkExpression(program, binary->left, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
        linkExpression(program, binary->right, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
    } else if (auto *call = std::get_if<CallExpression>(&expression.value)) {
        for (auto &argument : call->typeArguments) {
            linkType(argument, currentPackage, imports, symbols, typeParameters, diagnostics);
        }
        for (const auto argument : call->arguments) {
            linkExpression(program, argument, currentPackage, imports, symbols, typeParameters,
                           diagnostics);
        }
    } else if (auto *literal = std::get_if<StructExpression>(&expression.value)) {
        linkType(literal->type, currentPackage, imports, symbols, typeParameters, diagnostics);
        for (auto &field : literal->fields) {
            linkExpression(program, field.value, currentPackage, imports, symbols, typeParameters,
                           diagnostics);
        }
    } else if (auto *member = std::get_if<MemberExpression>(&expression.value)) {
        for (auto &argument : member->typeArguments) {
            linkType(argument, currentPackage, imports, symbols, typeParameters, diagnostics);
        }
        for (const auto argument : member->arguments) {
            linkExpression(program, argument, currentPackage, imports, symbols, typeParameters,
                           diagnostics);
        }
        if (member->base.has_value()) {
            linkExpression(program, *member->base, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
            auto &base = program.expressions[*member->base];
            if (auto *baseName = std::get_if<NameExpression>(&base.value)) {
                if (const auto imported = imports.find(baseName->name); imported != imports.end()) {
                    if (const auto *function =
                            findDeclaration(symbols, imported->second, member->member, true);
                        function != nullptr) {
                        if (!function->exported) {
                            reportPrivate(diagnostics, imported->second, member->member,
                                          expression.span);
                        }
                        if (member->invoked) {
                            expression.value = CallExpression{function->internalName,
                                                              std::move(member->typeArguments),
                                                              std::move(member->arguments)};
                        } else {
                            expression.value = NameExpression{function->internalName,
                                                              std::move(member->typeArguments)};
                        }
                        return;
                    }
                    if (const auto *type =
                            findDeclaration(symbols, imported->second, member->member, false);
                        type != nullptr && !member->invoked) {
                        if (!type->exported) {
                            reportPrivate(diagnostics, imported->second, member->member,
                                          expression.span);
                        }
                        expression.value = NameExpression{type->internalName,
                                                          std::move(member->typeArguments)};
                        return;
                    }
                    diagnostics.error("FDN3009",
                                      "unknown declaration " + imported->second + '.' +
                                          member->member,
                                      expression.span);
                    return;
                }
            }
        }
    } else if (auto *index = std::get_if<IndexExpression>(&expression.value)) {
        linkExpression(program, index->base, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
        linkExpression(program, index->index, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
    } else if (auto *replace = std::get_if<ReplaceExpression>(&expression.value)) {
        linkExpression(program, replace->target, currentPackage, imports, symbols,
                       typeParameters, diagnostics);
        linkExpression(program, replace->value, currentPackage, imports, symbols,
                       typeParameters, diagnostics);
    } else if (auto *match = std::get_if<MatchExpression>(&expression.value)) {
        linkExpression(program, match->value, currentPackage, imports, symbols, typeParameters,
                       diagnostics);
        for (auto &arm : match->arms) {
            linkExpression(program, arm.expression, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
        }
    }
}

void linkBlock(Program &program, AstBlockId id, const std::string &currentPackage,
               const ImportAliases &imports, const SymbolTable &symbols,
               const std::unordered_set<std::string> &typeParameters,
               Diagnostics &diagnostics) {
    for (const auto statementId : program.blocks[id].statements) {
        auto &statement = program.statements[statementId];
        if (auto *variable = std::get_if<VariableStatement>(&statement.value)) {
            if (variable->type.has_value()) {
                linkType(*variable->type, currentPackage, imports, symbols, typeParameters,
                         diagnostics);
            }
            linkExpression(program, variable->initializer, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
            if (variable->elseBlock.has_value()) {
                linkBlock(program, *variable->elseBlock, currentPackage, imports, symbols,
                          typeParameters, diagnostics);
            }
        } else if (auto *destructure =
                       std::get_if<StructDestructureStatement>(&statement.value)) {
            linkType(destructure->type, currentPackage, imports, symbols, typeParameters,
                     diagnostics);
            linkExpression(program, destructure->initializer, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
        } else if (auto *assignment = std::get_if<AssignmentStatement>(&statement.value)) {
            linkExpression(program, assignment->target, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
            linkExpression(program, assignment->value, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
        } else if (auto *expression = std::get_if<ExpressionStatement>(&statement.value)) {
            linkExpression(program, expression->expression, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
        } else if (auto *returned = std::get_if<ReturnStatement>(&statement.value)) {
            if (returned->value.has_value()) {
                linkExpression(program, *returned->value, currentPackage, imports, symbols,
                               typeParameters, diagnostics);
            }
        } else if (auto *discarded = std::get_if<DiscardStatement>(&statement.value)) {
            linkExpression(program, discarded->value, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
        } else if (auto *branch = std::get_if<IfStatement>(&statement.value)) {
            linkExpression(program, branch->condition, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
            linkBlock(program, branch->thenBlock, currentPackage, imports, symbols, typeParameters,
                      diagnostics);
            if (branch->elseBlock.has_value()) {
                linkBlock(program, *branch->elseBlock, currentPackage, imports, symbols,
                          typeParameters, diagnostics);
            }
        } else if (auto *loop = std::get_if<WhileStatement>(&statement.value)) {
            linkExpression(program, loop->condition, currentPackage, imports, symbols,
                           typeParameters, diagnostics);
            linkBlock(program, loop->body, currentPackage, imports, symbols, typeParameters,
                      diagnostics);
        }
    }
}

ImportAliases validateImports(const Program &program, const SymbolTable &symbols,
                              Diagnostics &diagnostics) {
    ImportAliases aliases;
    std::unordered_set<std::string> packages;
    for (const auto &imported : program.imports) {
        const auto alias = imported.alias.empty() ? defaultAlias(imported.packageName)
                                                  : imported.alias;
        if (!symbols.contains(imported.packageName)) {
            diagnostics.error("FDN3005", "package not found: " + imported.packageName,
                              imported.span);
        }
        if (!packages.emplace(imported.packageName).second) {
            diagnostics.error("FDN3004", "package imported more than once: " +
                                             imported.packageName,
                              imported.span);
        }
        if (!aliases.emplace(alias, imported.packageName).second) {
            diagnostics.error("FDN3004", "duplicate import alias " + alias, imported.span);
        }
    }
    return aliases;
}

void rejectAliasShadows(const Program &program, const ImportAliases &aliases,
                        Diagnostics &diagnostics) {
    for (const auto &function : program.functions) {
        for (const auto &parameter : function.parameters) {
            if (aliases.contains(parameter.name)) {
                diagnostics.error("FDN3004", "parameter shadows import alias " + parameter.name,
                                  parameter.span);
            }
        }
    }
    for (const auto &statement : program.statements) {
        const auto *variable = std::get_if<VariableStatement>(&statement.value);
        if (variable != nullptr) {
            if (aliases.contains(variable->name)) {
                diagnostics.error("FDN3004", "binding shadows import alias " + variable->name,
                                  statement.span);
            }
            if (variable->elseBinding.has_value() && aliases.contains(*variable->elseBinding)) {
                diagnostics.error("FDN3004",
                                  "binding shadows import alias " + *variable->elseBinding,
                                  statement.span);
            }
            continue;
        }
        const auto *destructure =
            std::get_if<StructDestructureStatement>(&statement.value);
        if (destructure == nullptr) {
            continue;
        }
        for (const auto &field : destructure->fields) {
            if (aliases.contains(field.binding)) {
                diagnostics.error("FDN3004",
                                  "binding shadows import alias " + field.binding, field.span);
            }
        }
    }
    for (const auto &expression : program.expressions) {
        const auto *match = std::get_if<MatchExpression>(&expression.value);
        if (match == nullptr) {
            continue;
        }
        for (const auto &arm : match->arms) {
            if (arm.binding.has_value() && aliases.contains(*arm.binding)) {
                diagnostics.error("FDN3004", "binding shadows import alias " + *arm.binding,
                                  arm.span);
            }
        }
    }
}

void collectSymbols(const std::vector<ParsedFile> &files, SymbolTable &symbols) {
    for (const auto &file : files) {
        auto &package = symbols[file.program.packageName];
        for (const auto &declaration : file.program.structs) {
            package.types.emplace(
                declaration.name,
                DeclarationInfo{internalName(file.program.packageName, declaration.name),
                                declaration.exported});
        }
        for (const auto &declaration : file.program.enums) {
            if (declaration.builtin != BuiltinEnumKind::None) {
                continue;
            }
            package.types.emplace(
                declaration.name,
                DeclarationInfo{internalName(file.program.packageName, declaration.name),
                                declaration.exported});
        }
        for (const auto &declaration : file.program.contracts) {
            package.types.emplace(
                declaration.name,
                DeclarationInfo{internalName(file.program.packageName, declaration.name),
                                declaration.exported});
        }
        for (const auto &declaration : file.program.functions) {
            if (declaration.receiver.has_value() || declaration.closure) {
                continue;
            }
            package.functions.emplace(
                declaration.name,
                DeclarationInfo{declaration.name == "main"
                                    ? std::string("main")
                                    : internalName(file.program.packageName, declaration.name),
                                declaration.exported});
        }
    }
}

void rejectCycles(const std::vector<ParsedFile> &files, const SymbolTable &symbols,
                  Diagnostics &diagnostics) {
    std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>> graph;
    for (const auto &[name, unused] : symbols) {
        static_cast<void>(unused);
        graph[name];
    }
    for (const auto &file : files) {
        for (const auto &imported : file.program.imports) {
            if (symbols.contains(imported.packageName)) {
                graph[file.program.packageName].push_back({imported.packageName, imported.span});
            }
        }
    }
    for (auto &[name, edges] : graph) {
        static_cast<void>(name);
        std::sort(edges.begin(), edges.end(), [](const auto &left, const auto &right) {
            return left.first < right.first;
        });
    }

    struct VisitFrame {
        std::string packageName;
        std::size_t edge{};
    };

    std::unordered_map<std::string, int> state;
    std::vector<std::string> stack;
    for (const auto &[packageName, edges] : graph) {
        static_cast<void>(edges);
        if (state[packageName] != 0) {
            continue;
        }
        std::vector<VisitFrame> pending{{packageName, 0}};
        state[packageName] = 1;
        stack.push_back(packageName);
        while (!pending.empty()) {
            auto &frame = pending.back();
            const auto &outgoing = graph[frame.packageName];
            if (frame.edge == outgoing.size()) {
                state[frame.packageName] = 2;
                pending.pop_back();
                stack.pop_back();
                continue;
            }
            const auto &[target, span] = outgoing[frame.edge++];
            if (state[target] == 0) {
                state[target] = 1;
                stack.push_back(target);
                pending.push_back({target, 0});
                continue;
            }
            if (state[target] != 1) {
                continue;
            }
            const auto start = std::find(stack.begin(), stack.end(), target);
            std::string cycle;
            for (auto current = start; current != stack.end(); ++current) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += *current;
            }
            cycle += " -> " + target;
            diagnostics.error("FDN3006", "package import cycle: " + cycle, span);
        }
    }
}

void linkFile(ParsedFile &file, const SymbolTable &symbols, Diagnostics &diagnostics,
              bool &foundMain) {
    auto aliases = validateImports(file.program, symbols, diagnostics);
    rejectAliasShadows(file.program, aliases, diagnostics);
    const auto packageName = file.program.packageName;
    const auto &sourcePath = file.sourcePath;

    for (auto &declaration : file.program.structs) {
        declaration.packageName = packageName;
        const std::unordered_set<std::string> parameters(declaration.typeParameters.begin(),
                                                         declaration.typeParameters.end());
        for (auto &field : declaration.fields) {
            linkType(field.type, packageName, aliases, symbols, parameters, diagnostics);
        }
        for (auto &implementation : declaration.implementations) {
            linkType(implementation, packageName, aliases, symbols, parameters, diagnostics);
        }
        declaration.name = internalName(packageName, declaration.name);
    }
    for (auto &declaration : file.program.enums) {
        if (declaration.builtin != BuiltinEnumKind::None) {
            continue;
        }
        declaration.packageName = packageName;
        const std::unordered_set<std::string> parameters(declaration.typeParameters.begin(),
                                                         declaration.typeParameters.end());
        for (auto &variant : declaration.variants) {
            if (variant.payloadType.has_value()) {
                linkType(*variant.payloadType, packageName, aliases, symbols, parameters,
                         diagnostics);
            }
        }
        declaration.name = internalName(packageName, declaration.name);
    }
    for (auto &declaration : file.program.contracts) {
        declaration.packageName = packageName;
        const std::unordered_set<std::string> parameters(declaration.typeParameters.begin(),
                                                         declaration.typeParameters.end());
        for (auto &method : declaration.methods) {
            for (auto &parameter : method.parameters) {
                linkType(parameter.type, packageName, aliases, symbols, parameters, diagnostics);
            }
            linkType(method.returnType, packageName, aliases, symbols, parameters, diagnostics);
        }
        declaration.name = internalName(packageName, declaration.name);
    }
    for (auto &function : file.program.functions) {
        function.packageName = packageName;
        function.sourcePath = sourcePath;
        const std::unordered_set<std::string> parameters(function.typeParameters.begin(),
                                                         function.typeParameters.end());
        for (auto &parameter : function.parameters) {
            linkType(parameter.type, packageName, aliases, symbols, parameters, diagnostics);
        }
        linkType(function.returnType, packageName, aliases, symbols, parameters, diagnostics);
        linkBlock(file.program, function.body, packageName, aliases, symbols, parameters,
                  diagnostics);
        if (function.closure) {
            function.name = internalName(packageName, function.name);
        } else if (function.receiver.has_value()) {
            function.ownerType = internalName(packageName, function.ownerType);
            function.name = function.ownerType + '.' + function.name;
        } else if (function.name == "main") {
            if (foundMain) {
                diagnostics.error("FDN3010", "project declares more than one main function",
                                  function.span);
            }
            foundMain = true;
        } else if (function.name == "print" || function.name == "panic") {
            diagnostics.error("FDN2018", function.name + " is a reserved builtin", function.span);
            function.name = internalName(packageName, function.name);
        } else {
            function.name = internalName(packageName, function.name);
        }
    }
}

} // namespace

std::optional<LoadedProject> loadProject(const std::filesystem::path &input,
                                         Diagnostics &diagnostics) {
    std::error_code error;
    const auto directoryInput = std::filesystem::is_directory(input, error);
    if (error) {
        diagnostics.error("FDN3001", "cannot inspect source input", {0, 0, 1, 1});
        return std::nullopt;
    }

    std::vector<std::filesystem::path> paths;
    if (directoryInput) {
        std::filesystem::recursive_directory_iterator iterator(input, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && !error &&
                iterator->path().extension() == ".fdn") {
                paths.push_back(iterator->path());
            }
            iterator.increment(error);
        }
        if (error) {
            diagnostics.error("FDN3001", "cannot discover project sources", {0, 0, 1, 1});
            return std::nullopt;
        }
        std::sort(paths.begin(), paths.end(), [&input](const auto &left, const auto &right) {
            return left.lexically_relative(input).generic_string() <
                   right.lexically_relative(input).generic_string();
        });
        if (paths.empty()) {
            diagnostics.error("FDN3002", "project contains no .fdn source files", {0, 0, 1, 1});
            return std::nullopt;
        }
    } else {
        paths.push_back(input);
    }

    const std::filesystem::path standardRoot{FOUNDATION_STANDARD_LIBRARY};
    const auto standardIdentity = sourceIdentity(standardRoot);
    if (!standardRoot.empty() && std::filesystem::is_directory(standardRoot, error) && !error) {
        std::vector<std::filesystem::path> standardPaths;
        std::filesystem::recursive_directory_iterator iterator(standardRoot, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && !error &&
                iterator->path().extension() == ".fdn") {
                standardPaths.push_back(iterator->path());
            }
            iterator.increment(error);
        }
        if (error) {
            diagnostics.error("FDN3001", "cannot discover standard library sources",
                              {0, 0, 1, 1});
            return std::nullopt;
        }
        std::sort(standardPaths.begin(), standardPaths.end(),
                  [&standardRoot](const auto &left, const auto &right) {
                      return left.lexically_relative(standardRoot).generic_string() <
                             right.lexically_relative(standardRoot).generic_string();
                  });
        std::unordered_set<std::string> seen;
        for (const auto &path : paths) {
            seen.insert(sourceIdentity(path).generic_string());
        }
        for (const auto &path : standardPaths) {
            if (seen.insert(sourceIdentity(path).generic_string()).second) {
                paths.push_back(path);
            }
        }
    } else {
        error.clear();
    }

    LoadedProject loaded;
    std::vector<ParsedFile> files;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto contents = readFile(paths[index]);
        const auto standardRelative =
            standardRoot.empty() ? std::filesystem::path{}
                                 : sourceIdentity(paths[index]).lexically_relative(
                                       standardIdentity);
        const auto standardSource =
            !standardRelative.empty() && *standardRelative.begin() != "..";
        const auto displayPath = standardSource
                                     ? (std::filesystem::path{"std"} / standardRelative)
                                           .generic_string()
                                 : directoryInput
                                     ? paths[index].lexically_relative(input).generic_string()
                                     : paths[index].generic_string();
        loaded.sources.push_back({displayPath, contents.value_or(std::string{})});
        if (!contents.has_value()) {
            diagnostics.error("FDN3001", "cannot read source file", {0, 0, 1, 1, index});
            continue;
        }
        Lexer lexer(*contents, diagnostics, index);
        Parser parser(lexer.scan(), diagnostics, index == 0);
        auto program = parser.parse();
        if (directoryInput && !program.hasPackageDeclaration) {
            diagnostics.error("FDN3003", "project source must declare a package",
                              {0, 0, 1, 1, index});
        }
        if (!program.hasPackageDeclaration) {
            program.packageName = directoryInput ? "main" : "";
        }
        files.push_back({std::move(program), displayPath});
    }
    if (diagnostics.hasErrors()) {
        return loaded;
    }

    SymbolTable symbols;
    collectSymbols(files, symbols);
    rejectCycles(files, symbols, diagnostics);
    bool foundMain{};
    for (auto &file : files) {
        linkFile(file, symbols, diagnostics, foundMain);
    }

    for (auto &file : files) {
        appendProgram(loaded.program, std::move(file.program));
    }
    return loaded;
}

} // namespace foundation
