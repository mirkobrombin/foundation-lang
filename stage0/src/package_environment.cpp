#include "foundation/package.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace foundation {

namespace {

#ifdef _WIN32
std::optional<std::wstring> environment(std::wstring_view name) {
    const auto key = std::wstring{name};
    const auto required = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }
    std::wstring value(required, L'\0');
    const auto copied = GetEnvironmentVariableW(key.c_str(), value.data(), required);
    if (copied == 0 || copied >= required) {
        return std::nullopt;
    }
    value.resize(copied);
    return value;
}
#endif

} // namespace

std::optional<std::filesystem::path> defaultPackageCachePath() {
#ifdef _WIN32
    if (const auto overridePath = environment(L"FOUNDATION_PACKAGE_CACHE")) {
        return std::filesystem::path{*overridePath};
    }
    if (const auto local = environment(L"LOCALAPPDATA")) {
        return std::filesystem::path{*local} / "Foundation" / "packages";
    }
#else
    if (const auto *overridePath = std::getenv("FOUNDATION_PACKAGE_CACHE");
        overridePath != nullptr && *overridePath != '\0') {
        return std::filesystem::path{overridePath};
    }
#if defined(__APPLE__)
    if (const auto *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / "Library" / "Caches" / "Foundation" /
               "packages";
    }
#else
    if (const auto *xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path{xdg} / "foundation" / "packages";
    }
    if (const auto *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / ".cache" / "foundation" / "packages";
    }
#endif
#endif
    return std::nullopt;
}

} // namespace foundation
