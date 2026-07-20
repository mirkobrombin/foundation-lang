#ifndef FOUNDATION_CODE_STANDARD_HPP
#define FOUNDATION_CODE_STANDARD_HPP

#include <cstddef>
#include <optional>
#include <string_view>

namespace foundation {

enum class CodeStandardProfile {
    Valid,
    Standard,
    Strict,
};

[[nodiscard]] std::optional<CodeStandardProfile>
parseCodeStandardProfile(std::string_view value);
[[nodiscard]] std::string_view codeStandardProfileName(CodeStandardProfile profile);
[[nodiscard]] std::size_t codeStandardWidth(CodeStandardProfile profile);

} // namespace foundation

#endif
