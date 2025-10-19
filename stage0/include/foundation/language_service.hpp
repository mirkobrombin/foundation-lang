#ifndef FOUNDATION_LANGUAGE_SERVICE_HPP
#define FOUNDATION_LANGUAGE_SERVICE_HPP

#include "foundation/diagnostic.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace foundation {

struct ProjectAnalysis;

enum class LanguageSymbolKind {
    Function,
    Method,
    Struct,
    Field,
    Enum,
    EnumVariant,
    Contract,
    ContractMethod,
    Attribute,
    Parameter,
    Local,
};

struct LanguageSymbolId {
    LanguageSymbolKind kind{LanguageSymbolKind::Function};
    std::size_t owner{};
    std::size_t member{};

    bool operator==(const LanguageSymbolId &) const = default;
};

struct LanguageSymbol {
    LanguageSymbolId id;
    std::string name;
    std::string detail;
    std::string scope;
    SourceSpan definition;
    bool renameable{true};
    std::string documentation;

    LanguageSymbol() = default;
    LanguageSymbol(LanguageSymbolId id, std::string name, std::string detail,
                   std::string scope, SourceSpan definition, bool renameable = true,
                   std::string documentation = {})
        : id(id), name(std::move(name)), detail(std::move(detail)),
          scope(std::move(scope)), definition(definition), renameable(renameable),
          documentation(std::move(documentation)) {}
};

struct LanguageOccurrence {
    LanguageSymbolId symbol;
    SourceSpan span;
    bool definition{};
};

struct LanguageCall {
    LanguageSymbolId caller;
    LanguageSymbolId callee;
    SourceSpan span;
};

struct LanguageTypeLink {
    LanguageSymbolId symbol;
    LanguageSymbolId type;
};

class LanguageIndex {
  public:
    LanguageIndex() = default;
    LanguageIndex(std::vector<LanguageSymbol> symbols,
                  std::vector<LanguageOccurrence> occurrences,
                  std::vector<LanguageCall> calls,
                  std::vector<LanguageTypeLink> typeLinks);

    [[nodiscard]] const std::vector<LanguageSymbol> &symbols() const;
    [[nodiscard]] const std::vector<LanguageOccurrence> &occurrences() const;
    [[nodiscard]] const LanguageSymbol *symbol(LanguageSymbolId id) const;
    [[nodiscard]] const LanguageOccurrence *occurrenceAt(std::size_t source,
                                                         std::size_t offset) const;
    [[nodiscard]] std::vector<LanguageOccurrence>
    references(LanguageSymbolId id, bool includeDefinition) const;
    [[nodiscard]] std::vector<LanguageCall> incomingCalls(LanguageSymbolId id) const;
    [[nodiscard]] std::vector<LanguageCall> outgoingCalls(LanguageSymbolId id) const;
    [[nodiscard]] const LanguageSymbol *typeDefinition(LanguageSymbolId id) const;
    [[nodiscard]] bool canRename(LanguageSymbolId id, std::string_view name) const;

  private:
    std::vector<LanguageSymbol> symbols_;
    std::vector<LanguageOccurrence> occurrences_;
    std::vector<LanguageCall> calls_;
    std::vector<LanguageTypeLink> typeLinks_;
};

[[nodiscard]] LanguageIndex buildLanguageIndex(const ProjectAnalysis &analysis);
[[nodiscard]] std::string languageDocumentation(const ProjectAnalysis &analysis,
                                                SourceSpan definition);

} // namespace foundation

#endif
