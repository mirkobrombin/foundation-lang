#include "foundation/documentation.hpp"

#include "foundation/driver.hpp"
#include "foundation/language_service.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

namespace {

struct PackageDocumentation {
    std::vector<std::size_t> structs;
    std::vector<std::size_t> enums;
    std::vector<std::size_t> contracts;
    std::vector<std::size_t> attributes;
    std::vector<std::size_t> functions;
    std::map<std::string, std::vector<std::size_t>> methods;
};

std::string_view shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return name.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

bool generatedSource(std::string_view source) {
    const auto start = source.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return false;
    }
    source.remove_prefix(start);
    return source.starts_with("// foundation:generated package/v1") ||
           source.starts_with("// foundation:generated application/v1");
}

bool projectSource(const ProjectAnalysis &analysis, SourceSpan span,
                   const std::set<std::size_t> &projectSources) {
    if (span.source >= analysis.sources.size()) {
        return false;
    }
    const auto &source = analysis.sources[span.source];
    if (source.path.ends_with(".foundation.generated.fn") || generatedSource(source.contents)) {
        return false;
    }
    if (!projectSources.empty()) {
        return projectSources.contains(span.source);
    }
    return !source.path.starts_with("std/") &&
           !source.path.starts_with("foundation/") &&
           !source.path.starts_with("packages/");
}

std::string packageName(std::string_view name) {
    return name.empty() ? "main" : std::string(name);
}

const LanguageSymbol *symbol(const LanguageIndex &index, LanguageSymbolKind kind,
                             std::size_t owner, std::size_t member = 0) {
    return index.symbol({kind, owner, member});
}

void writeDocumentation(std::ostringstream &output, std::string_view documentation) {
    if (!documentation.empty()) {
        output << documentation << "\n\n";
    }
}

void writeSignature(std::ostringstream &output, std::string_view signature) {
    output << "```foundation\n" << signature << "\n```\n\n";
}

void writeDocumentedListItem(std::ostringstream &output, std::string_view detail,
                             std::string_view documentation) {
    output << "- `" << detail << '`';
    if (documentation.empty()) {
        output << '\n';
        return;
    }
    output << "\n\n";
    std::size_t offset = 0;
    while (offset <= documentation.size()) {
        const auto end = documentation.find('\n', offset);
        const auto line = documentation.substr(
            offset, end == std::string_view::npos ? documentation.size() - offset : end - offset);
        if (!line.empty()) {
            output << "  " << line;
        }
        output << '\n';
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }
}

void writeParameterDocumentation(std::ostringstream &output,
                                 const ProjectAnalysis &analysis,
                                 const std::vector<Parameter> &parameters,
                                 bool receiver) {
    std::vector<std::pair<std::string_view, std::string>> documented;
    const auto start = receiver && !parameters.empty() ? std::size_t{1} : std::size_t{};
    for (auto index = start; index < parameters.size(); ++index) {
        auto documentation = languageParameterDocumentation(analysis, parameters[index]);
        if (!documentation.empty()) {
            documented.emplace_back(parameters[index].name, std::move(documentation));
        }
    }
    if (documented.empty()) {
        return;
    }
    output << "Parameters:\n\n";
    for (const auto &[name, documentation] : documented) {
        writeDocumentedListItem(output, name, documentation);
    }
    output << '\n';
}

std::string_view targetName(AttributeTarget target) {
    switch (target) {
    case AttributeTarget::Function:
        return "function";
    case AttributeTarget::Struct:
        return "struct";
    case AttributeTarget::Service:
        return "service";
    case AttributeTarget::Enum:
        return "enum";
    case AttributeTarget::Contract:
        return "contract";
    case AttributeTarget::Method:
        return "method";
    case AttributeTarget::Action:
        return "action";
    case AttributeTarget::Field:
        return "field";
    case AttributeTarget::Variant:
        return "variant";
    case AttributeTarget::Parameter:
        return "parameter";
    }
    return "declaration";
}

template <typename Name>
void sortByName(std::vector<std::size_t> &values, Name name) {
    std::sort(values.begin(), values.end(), [&](std::size_t left, std::size_t right) {
        const auto leftName = name(left);
        const auto rightName = name(right);
        return leftName == rightName ? left < right : leftName < rightName;
    });
}

void writeMethods(std::ostringstream &output, const ProjectAnalysis &analysis,
                  const LanguageIndex &index, const std::vector<std::size_t> &methods) {
    if (methods.empty()) {
        return;
    }
    output << "##### Methods\n\n";
    for (const auto id : methods) {
        const auto *entry = symbol(index, LanguageSymbolKind::Method, id);
        if (entry == nullptr) {
            continue;
        }
        output << "###### `" << entry->name << "`\n\n";
        writeDocumentation(output, entry->documentation);
        writeSignature(output, entry->detail);
        const auto &function = analysis.program.functions[id];
        writeParameterDocumentation(output, analysis, function.parameters,
                                    function.receiver.has_value());
    }
}

} // namespace

std::string emitDocumentation(const ProjectAnalysis &analysis,
                              const std::vector<std::size_t> &projectSourceList) {
    const auto index = buildLanguageIndex(analysis);
    const std::set<std::size_t> projectSources(projectSourceList.begin(),
                                               projectSourceList.end());
    std::map<std::string, PackageDocumentation> packages;
    std::set<std::string> exportedTypes;
    std::set<std::string> contracts;

    for (std::size_t source = 0; source < analysis.sources.size(); ++source) {
        if (projectSource(analysis, {0, 0, 1, 1, source}, projectSources)) {
            packages[packageName(analysis.sources[source].packageName)];
        }
    }
    for (std::size_t id = 0; id < analysis.program.structs.size(); ++id) {
        const auto &declaration = analysis.program.structs[id];
        if (declaration.exported &&
            projectSource(analysis, declaration.span, projectSources)) {
            packages[packageName(declaration.packageName)].structs.push_back(id);
            exportedTypes.insert(declaration.name);
        }
    }
    for (std::size_t id = 0; id < analysis.program.enums.size(); ++id) {
        const auto &declaration = analysis.program.enums[id];
        if (declaration.exported && declaration.builtin == BuiltinEnumKind::None &&
            projectSource(analysis, declaration.span, projectSources)) {
            packages[packageName(declaration.packageName)].enums.push_back(id);
            exportedTypes.insert(declaration.name);
        }
    }
    for (std::size_t id = 0; id < analysis.program.contracts.size(); ++id) {
        const auto &declaration = analysis.program.contracts[id];
        contracts.insert(declaration.name);
        if (declaration.exported &&
            projectSource(analysis, declaration.span, projectSources)) {
            packages[packageName(declaration.packageName)].contracts.push_back(id);
            exportedTypes.insert(declaration.name);
        }
    }
    for (std::size_t id = 0; id < analysis.program.attributeDeclarations.size(); ++id) {
        const auto &declaration = analysis.program.attributeDeclarations[id];
        if (declaration.exported &&
            projectSource(analysis, declaration.span, projectSources)) {
            packages[packageName(declaration.packageName)].attributes.push_back(id);
        }
    }
    for (std::size_t id = 0; id < analysis.program.functions.size(); ++id) {
        const auto &function = analysis.program.functions[id];
        if (!function.exported || function.closure || function.testName.has_value() ||
            function.name.find("$field_default.") != std::string::npos ||
            !projectSource(analysis, function.span, projectSources)) {
            continue;
        }
        auto &package = packages[packageName(function.packageName)];
        if (function.ownerType.empty()) {
            package.functions.push_back(id);
        } else if (exportedTypes.contains(function.ownerType) &&
                   !contracts.contains(function.ownerType)) {
            package.methods[function.ownerType].push_back(id);
        }
    }

    for (auto &[name, package] : packages) {
        static_cast<void>(name);
        sortByName(package.structs, [&](std::size_t id) {
            return shortName(analysis.program.structs[id].name);
        });
        sortByName(package.enums, [&](std::size_t id) {
            return shortName(analysis.program.enums[id].name);
        });
        sortByName(package.contracts, [&](std::size_t id) {
            return shortName(analysis.program.contracts[id].name);
        });
        sortByName(package.attributes, [&](std::size_t id) {
            return shortName(analysis.program.attributeDeclarations[id].name);
        });
        sortByName(package.functions, [&](std::size_t id) {
            return shortName(analysis.program.functions[id].name);
        });
        for (auto &[owner, methods] : package.methods) {
            static_cast<void>(owner);
            sortByName(methods, [&](std::size_t id) {
                return shortName(analysis.program.functions[id].name);
            });
        }
    }

    std::ostringstream output;
    output << "# Foundation API Reference\n\n";
    for (const auto &[name, package] : packages) {
        output << "## Package `" << name << "`\n\n";
        if (package.structs.empty() && package.enums.empty() && package.contracts.empty() &&
            package.attributes.empty() && package.functions.empty()) {
            output << "No exported API.\n\n";
            continue;
        }

        if (!package.structs.empty()) {
            output << "### Types\n\n";
            for (const auto id : package.structs) {
                const auto &declaration = analysis.program.structs[id];
                const auto *entry = symbol(index, LanguageSymbolKind::Struct, id);
                if (entry == nullptr) {
                    continue;
                }
                output << "#### `" << entry->name << "`\n\n";
                writeDocumentation(output, entry->documentation);
                writeSignature(output, entry->detail);
                if (!declaration.implementations.empty()) {
                    output << "Implements: ";
                    for (std::size_t implementation = 0;
                         implementation < declaration.implementations.size(); ++implementation) {
                        if (implementation != 0) {
                            output << ", ";
                        }
                        output << '`'
                               << languageTypeSyntax(
                                      declaration.implementations[implementation].contract)
                               << '`';
                    }
                    output << ".\n\n";
                }
                std::vector<const LanguageSymbol *> fields;
                for (std::size_t field = 0; field < declaration.fields.size(); ++field) {
                    if (!declaration.fields[field].exported) {
                        continue;
                    }
                    if (const auto *fieldSymbol =
                            symbol(index, LanguageSymbolKind::Field, id, field);
                        fieldSymbol != nullptr) {
                        fields.push_back(fieldSymbol);
                    }
                }
                if (!fields.empty()) {
                    output << "Fields:\n\n";
                    for (const auto *field : fields) {
                        writeDocumentedListItem(output, field->detail,
                                                field->documentation);
                    }
                    output << '\n';
                }
                const auto methods = package.methods.find(declaration.name);
                if (methods != package.methods.end()) {
                    writeMethods(output, analysis, index, methods->second);
                }
            }
        }

        if (!package.enums.empty()) {
            output << "### Enums\n\n";
            for (const auto id : package.enums) {
                const auto &declaration = analysis.program.enums[id];
                const auto *entry = symbol(index, LanguageSymbolKind::Enum, id);
                if (entry == nullptr) {
                    continue;
                }
                output << "#### `" << entry->name << "`\n\n";
                writeDocumentation(output, entry->documentation);
                writeSignature(output, entry->detail);
                std::vector<const LanguageSymbol *> variants;
                for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                    if (!declaration.variants[variant].exported) {
                        continue;
                    }
                    if (const auto *variantSymbol =
                            symbol(index, LanguageSymbolKind::EnumVariant, id, variant);
                        variantSymbol != nullptr) {
                        variants.push_back(variantSymbol);
                    }
                }
                if (!variants.empty()) {
                    output << "Variants:\n\n";
                    for (const auto *variant : variants) {
                        writeDocumentedListItem(output, variant->detail,
                                                variant->documentation);
                    }
                    output << '\n';
                }
            }
        }

        if (!package.contracts.empty()) {
            output << "### Contracts\n\n";
            for (const auto id : package.contracts) {
                const auto &declaration = analysis.program.contracts[id];
                const auto *entry = symbol(index, LanguageSymbolKind::Contract, id);
                if (entry == nullptr) {
                    continue;
                }
                output << "#### `" << entry->name << "`\n\n";
                writeDocumentation(output, entry->documentation);
                writeSignature(output, entry->detail);
                if (!declaration.parents.empty()) {
                    output << "Extends: ";
                    for (std::size_t parent = 0; parent < declaration.parents.size(); ++parent) {
                        if (parent != 0) {
                            output << ", ";
                        }
                        output << '`' << languageTypeSyntax(declaration.parents[parent]) << '`';
                    }
                    output << ".\n\n";
                }
                std::vector<std::size_t> methods;
                for (std::size_t method = 0; method < declaration.methods.size(); ++method) {
                    if (declaration.methods[method].exported) {
                        methods.push_back(method);
                    }
                }
                sortByName(methods, [&](std::size_t method) {
                    return std::string_view(declaration.methods[method].name);
                });
                if (!methods.empty()) {
                    output << "Methods:\n\n";
                    for (const auto method : methods) {
                        const auto *methodSymbol =
                            symbol(index, LanguageSymbolKind::ContractMethod, id, method);
                        if (methodSymbol == nullptr) {
                            continue;
                        }
                        output << "##### `" << methodSymbol->name << "`\n\n";
                        writeDocumentation(output, methodSymbol->documentation);
                        writeSignature(output, methodSymbol->detail);
                        writeParameterDocumentation(output, analysis,
                                                    declaration.methods[method].parameters,
                                                    false);
                    }
                }
            }
        }

        if (!package.attributes.empty()) {
            output << "### Attributes\n\n";
            for (const auto id : package.attributes) {
                const auto &declaration = analysis.program.attributeDeclarations[id];
                const auto *entry = symbol(index, LanguageSymbolKind::Attribute, id);
                if (entry == nullptr) {
                    continue;
                }
                output << "#### `@" << entry->name << "`\n\n";
                writeDocumentation(output, entry->documentation);
                writeSignature(output, entry->detail);
                output << "Targets: ";
                for (std::size_t target = 0; target < declaration.targets.size(); ++target) {
                    if (target != 0) {
                        output << ", ";
                    }
                    output << '`' << targetName(declaration.targets[target]) << '`';
                }
                if (declaration.repeatable) {
                    output << ". Repeatable";
                }
                output << ".\n\n";
                writeParameterDocumentation(output, analysis, declaration.parameters, false);
            }
        }

        if (!package.functions.empty()) {
            output << "### Functions\n\n";
            for (const auto id : package.functions) {
                const auto &function = analysis.program.functions[id];
                const auto *entry = symbol(index, LanguageSymbolKind::Function, id);
                if (entry == nullptr) {
                    continue;
                }
                output << "#### `" << entry->name << "`\n\n";
                writeDocumentation(output, entry->documentation);
                writeSignature(output, entry->detail);
                writeParameterDocumentation(output, analysis, function.parameters, false);
            }
        }
    }
    auto result = output.str();
    while (result.size() >= 2 && result.ends_with("\n\n")) {
        result.pop_back();
    }
    return result;
}

} // namespace foundation
