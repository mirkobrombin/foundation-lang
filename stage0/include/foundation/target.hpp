#ifndef FOUNDATION_TARGET_HPP
#define FOUNDATION_TARGET_HPP

#include <string_view>

namespace foundation {

enum class TargetPlatform {
    Unknown,
    Linux,
    MacOS,
    Windows,
};

[[nodiscard]] TargetPlatform hostTargetPlatform();
[[nodiscard]] std::string_view targetPlatformName(TargetPlatform target);

} // namespace foundation

#endif
