#ifndef FOUNDATION_CODE_STANDARD_HPP
#define FOUNDATION_CODE_STANDARD_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace foundation {

enum class CodeStandardProfile {
    Valid,
    Standard,
    Strict,
};

enum class CodeStandardSeverity {
    Off,
    Warning,
    Error,
};

struct CodeStandardRuleSetting {
    std::string code;
    CodeStandardSeverity severity{CodeStandardSeverity::Warning};
};

[[nodiscard]] std::optional<CodeStandardProfile>
parseCodeStandardProfile(std::string_view value);
[[nodiscard]] std::optional<CodeStandardSeverity>
parseCodeStandardSeverity(std::string_view value);
[[nodiscard]] std::string_view codeStandardProfileName(CodeStandardProfile profile);
[[nodiscard]] std::string_view codeStandardSeverityName(CodeStandardSeverity severity);
[[nodiscard]] std::size_t codeStandardWidth(CodeStandardProfile profile);
[[nodiscard]] bool configurableCodeStandardRule(std::string_view code);

} // namespace foundation

#endif
