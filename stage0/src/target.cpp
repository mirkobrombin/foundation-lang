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
    case TargetPlatform::Unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace foundation
