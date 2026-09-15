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
    Freestanding,
};

// Names the platforms that one `@target` attribute or manifest qualifier activates. `Hosted`
// matches every operating-system platform and is never a build target or lock target. The
// declaration order is the canonical manifest order.
enum class TargetSelector {
    Linux,
    MacOS,
    Windows,
    Freestanding,
    Hosted,
};

[[nodiscard]] TargetPlatform hostTargetPlatform();
[[nodiscard]] std::optional<TargetPlatform> parseTargetPlatform(std::string_view value);
[[nodiscard]] std::string_view targetPlatformName(TargetPlatform target);
[[nodiscard]] bool hostedTargetPlatform(TargetPlatform target);
[[nodiscard]] std::optional<TargetSelector> parseTargetSelector(std::string_view value);
[[nodiscard]] std::string_view targetSelectorName(TargetSelector selector);
[[nodiscard]] bool targetSelected(TargetSelector selector, TargetPlatform target);
[[nodiscard]] bool targetSelectorsOverlap(TargetSelector left, TargetSelector right);

} // namespace foundation

#endif
