#include "foundation/target.hpp"

namespace foundation {

TargetPlatform hostTargetPlatform() {
#if defined(_WIN32)
    return TargetPlatform::Windows;
#elif defined(__APPLE__)
    return TargetPlatform::MacOS;
#elif defined(__linux__)
    return TargetPlatform::Linux;
#else
    return TargetPlatform::Unknown;
#endif
}

std::optional<TargetPlatform> parseTargetPlatform(std::string_view value) {
    if (value == "linux") {
        return TargetPlatform::Linux;
    }
    if (value == "macos") {
        return TargetPlatform::MacOS;
    }
    if (value == "windows") {
        return TargetPlatform::Windows;
    }
    if (value == "freestanding") {
        return TargetPlatform::Freestanding;
    }
    return std::nullopt;
}

std::string_view targetPlatformName(TargetPlatform target) {
    switch (target) {
    case TargetPlatform::Linux:
        return "linux";
    case TargetPlatform::MacOS:
        return "macos";
    case TargetPlatform::Windows:
        return "windows";
    case TargetPlatform::Freestanding:
        return "freestanding";
    case TargetPlatform::Unknown:
        return "unknown";
    }
    return "unknown";
}

bool hostedTargetPlatform(TargetPlatform target) {
    return target == TargetPlatform::Linux || target == TargetPlatform::MacOS ||
           target == TargetPlatform::Windows;
}

std::optional<TargetSelector> parseTargetSelector(std::string_view value) {
    if (value == "hosted") {
        return TargetSelector::Hosted;
    }
    const auto platform = parseTargetPlatform(value);
    if (!platform.has_value()) {
        return std::nullopt;
    }
    switch (*platform) {
    case TargetPlatform::Linux:
        return TargetSelector::Linux;
    case TargetPlatform::MacOS:
        return TargetSelector::MacOS;
    case TargetPlatform::Windows:
        return TargetSelector::Windows;
    case TargetPlatform::Freestanding:
        return TargetSelector::Freestanding;
    case TargetPlatform::Unknown:
        break;
    }
    return std::nullopt;
}

std::string_view targetSelectorName(TargetSelector selector) {
    switch (selector) {
    case TargetSelector::Linux:
        return "linux";
    case TargetSelector::MacOS:
        return "macos";
    case TargetSelector::Windows:
        return "windows";
    case TargetSelector::Freestanding:
        return "freestanding";
    case TargetSelector::Hosted:
        return "hosted";
    }
    return "hosted";
}

bool targetSelected(TargetSelector selector, TargetPlatform target) {
    switch (selector) {
    case TargetSelector::Linux:
        return target == TargetPlatform::Linux;
    case TargetSelector::MacOS:
        return target == TargetPlatform::MacOS;
    case TargetSelector::Windows:
        return target == TargetPlatform::Windows;
    case TargetSelector::Freestanding:
        return target == TargetPlatform::Freestanding;
    case TargetSelector::Hosted:
        return hostedTargetPlatform(target);
    }
    return false;
}

bool targetSelectorsOverlap(TargetSelector left, TargetSelector right) {
    for (const auto target : {TargetPlatform::Linux, TargetPlatform::MacOS,
                              TargetPlatform::Windows, TargetPlatform::Freestanding}) {
        if (targetSelected(left, target) && targetSelected(right, target)) {
            return true;
        }
    }
    return false;
}

} // namespace foundation
