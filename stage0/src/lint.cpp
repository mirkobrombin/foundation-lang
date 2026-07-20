#include "foundation/lint.hpp"

#include "foundation/driver.hpp"
#include "foundation/language_service.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

namespace {

struct Finding {
    std::string code;
    std::string message;
    SourceSpan span;
};

bool generatedSource(std::string_view source) {
    const auto start = source.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return false;
    }
    source.remove_prefix(start);
    return source.starts_with("// foundation:generated package/v1") ||
           source.starts_with("// foundation:generated application/v1");
}

bool projectSource(const ProjectAnalysis &analysis, std::size_t source,
                   const std::set<std::size_t> &projectSources) {
    if (source >= analysis.sources.size()) {
        return false;
    }
    const auto &input = analysis.sources[source];
    if (input.path == ".foundation.generated.fdn" || generatedSource(input.contents)) {
        return false;
    }
    if (!projectSources.empty()) {
        return projectSources.contains(source);
    }
    return !input.path.starts_with("std/") &&
           !input.path.starts_with("foundation/") &&
           !input.path.starts_with("packages/");
}

std::string typeKey(std::string_view packageName, std::string_view typeName) {
    std::string result;
    result.reserve(packageName.size() + typeName.size() + 1);
    result.append(packageName);
    result.push_back('\0');
    result.append(typeName);
    return result;
}

std::size_t nextColumn(std::size_t column, unsigned char byte) {
    if (byte == '\t') {
        return ((column - 1) / 4 + 1) * 4 + 1;
    }
    return (byte & 0xc0U) == 0x80U ? column : column + 1;
}

void findLongLines(const ProjectAnalysis &analysis, std::size_t source, std::size_t width,
                   std::vector<Finding> &findings) {
    const auto &contents = analysis.sources[source].contents;
    std::size_t offset = 0;
    std::size_t line = 1;
    while (offset <= contents.size()) {
        const auto end = contents.find('\n', offset);
        const auto lineEnd = end == std::string::npos ? contents.size() : end;
        auto column = std::size_t{1};
        auto excess = lineEnd;
        for (auto cursor = offset; cursor < lineEnd; ++cursor) {
            column = nextColumn(column, static_cast<unsigned char>(contents[cursor]));
            if (column > width + 1) {
                excess = cursor;
                break;
            }
        }
        if (excess != lineEnd) {
            findings.push_back({"FCS1001",
                                "line exceeds the " + std::to_string(width) +
                                    " column profile width",
                                {excess, lineEnd - excess, line, width + 1, source}});
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1;
        ++line;
    }
}

bool exportedSymbol(const ProjectAnalysis &analysis, const LanguageSymbol &symbol,
                    const std::set<std::string> &exportedTypes,
                    const std::set<std::string> &contracts) {
    switch (symbol.id.kind) {
    case LanguageSymbolKind::Struct:
        return symbol.id.owner < analysis.program.structs.size() &&
               analysis.program.structs[symbol.id.owner].exported;
    case LanguageSymbolKind::Field:
        return symbol.id.owner < analysis.program.structs.size() &&
               symbol.id.member < analysis.program.structs[symbol.id.owner].fields.size() &&
               analysis.program.structs[symbol.id.owner].exported &&
               analysis.program.structs[symbol.id.owner].fields[symbol.id.member].exported;
    case LanguageSymbolKind::Enum:
        return symbol.id.owner < analysis.program.enums.size() &&
               analysis.program.enums[symbol.id.owner].exported;
    case LanguageSymbolKind::EnumVariant:
        return symbol.id.owner < analysis.program.enums.size() &&
               symbol.id.member < analysis.program.enums[symbol.id.owner].variants.size() &&
               analysis.program.enums[symbol.id.owner].exported &&
               analysis.program.enums[symbol.id.owner].variants[symbol.id.member].exported;
    case LanguageSymbolKind::Contract:
        return symbol.id.owner < analysis.program.contracts.size() &&
               analysis.program.contracts[symbol.id.owner].exported;
    case LanguageSymbolKind::ContractMethod:
        return symbol.id.owner < analysis.program.contracts.size() &&
               symbol.id.member < analysis.program.contracts[symbol.id.owner].methods.size() &&
               analysis.program.contracts[symbol.id.owner].exported &&
               analysis.program.contracts[symbol.id.owner].methods[symbol.id.member].exported;
    case LanguageSymbolKind::Attribute:
        return symbol.id.owner < analysis.program.attributeDeclarations.size() &&
               analysis.program.attributeDeclarations[symbol.id.owner].exported;
    case LanguageSymbolKind::Function:
        return symbol.id.owner < analysis.program.functions.size() &&
               analysis.program.functions[symbol.id.owner].exported &&
               analysis.program.functions[symbol.id.owner].ownerType.empty();
    case LanguageSymbolKind::Method: {
        if (symbol.id.owner >= analysis.program.functions.size()) {
            return false;
        }
        const auto &function = analysis.program.functions[symbol.id.owner];
        const auto owner = typeKey(function.packageName, function.ownerType);
        return function.exported && exportedTypes.contains(owner) && !contracts.contains(owner);
    }
    case LanguageSymbolKind::EnumPayload:
    case LanguageSymbolKind::Parameter:
    case LanguageSymbolKind::Local:
        return false;
    }
    return false;
}

std::string_view symbolKind(LanguageSymbolKind kind) {
    switch (kind) {
    case LanguageSymbolKind::Struct:
        return "type";
    case LanguageSymbolKind::Field:
        return "field";
    case LanguageSymbolKind::Enum:
        return "enum";
    case LanguageSymbolKind::EnumVariant:
        return "variant";
    case LanguageSymbolKind::Contract:
        return "contract";
    case LanguageSymbolKind::ContractMethod:
    case LanguageSymbolKind::Method:
        return "method";
    case LanguageSymbolKind::Attribute:
        return "attribute";
    case LanguageSymbolKind::Function:
        return "function";
    case LanguageSymbolKind::EnumPayload:
    case LanguageSymbolKind::Parameter:
    case LanguageSymbolKind::Local:
        return "symbol";
    }
    return "symbol";
}

} // namespace

Diagnostics lintProject(const ProjectAnalysis &analysis, CodeStandardProfile profile,
                        const std::vector<std::size_t> &projectSourceList) {
    Diagnostics diagnostics;
    if (profile == CodeStandardProfile::Valid) {
        return diagnostics;
    }
    const std::set<std::size_t> projectSources(projectSourceList.begin(),
                                               projectSourceList.end());
    std::vector<Finding> findings;
    for (std::size_t source = 0; source < analysis.sources.size(); ++source) {
        if (projectSource(analysis, source, projectSources)) {
            findLongLines(analysis, source, codeStandardWidth(profile), findings);
        }
    }

    if (profile == CodeStandardProfile::Strict) {
        const auto index = buildLanguageIndex(analysis);
        std::set<std::string> exportedTypes;
        std::set<std::string> contracts;
        for (const auto &declaration : analysis.program.structs) {
            if (declaration.exported) {
                exportedTypes.insert(typeKey(declaration.packageName, declaration.name));
            }
        }
        for (const auto &declaration : analysis.program.enums) {
            if (declaration.exported) {
                exportedTypes.insert(typeKey(declaration.packageName, declaration.name));
            }
        }
        for (const auto &declaration : analysis.program.contracts) {
            const auto key = typeKey(declaration.packageName, declaration.name);
            contracts.insert(key);
            if (declaration.exported) {
                exportedTypes.insert(key);
            }
        }
        for (const auto &symbol : index.symbols()) {
            if (!projectSource(analysis, symbol.definition.source, projectSources) ||
                !exportedSymbol(analysis, symbol, exportedTypes, contracts) ||
                !symbol.documentation.empty()) {
                continue;
            }
            findings.push_back({"FCS2001",
                                "strict profile requires documentation for exported " +
                                    std::string(symbolKind(symbol.id.kind)) + " " + symbol.name,
                                symbol.definition});
        }
    }

    std::sort(findings.begin(), findings.end(), [](const auto &left, const auto &right) {
        if (left.span.source != right.span.source) {
            return left.span.source < right.span.source;
        }
        if (left.span.offset != right.span.offset) {
            return left.span.offset < right.span.offset;
        }
        return left.code < right.code;
    });
    for (auto &finding : findings) {
        diagnostics.warning(std::move(finding.code), std::move(finding.message), finding.span);
    }
    return diagnostics;
}

} // namespace foundation
