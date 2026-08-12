#ifndef FOUNDATION_TARGET_HPP
#define FOUNDATION_TARGET_HPP

#include <optional>
#include <string_view>

namespace foundation {

enum class TargetPlatform {
    Unknown,
    Linux,
    MacOS,
    Windows,
};

[[nodiscard]] TargetPlatform hostTargetPlatform();
[[nodiscard]] std::optional<TargetPlatform> parseTargetPlatform(std::string_view value);
[[nodiscard]] std::string_view targetPlatformName(TargetPlatform target);

} // namespace foundation

#endif
