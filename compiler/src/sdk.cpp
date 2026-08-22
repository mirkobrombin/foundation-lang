#include "foundation/sdk.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace foundation {

namespace {

std::optional<std::filesystem::path> executablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size{};
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
#else
    std::vector<char> buffer(256);
    for (;;) {
        const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return std::nullopt;
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return std::filesystem::path(
                std::string(buffer.data(), static_cast<std::size_t>(length)));
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

std::optional<std::filesystem::path> installedSdkRoot() {
    const auto executable = executablePath();
    if (!executable.has_value()) {
        return std::nullopt;
    }
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(*executable, error);
    const auto resolved = error ? *executable : canonical;
    auto root = resolved.parent_path();
    for (unsigned int depth = 0; depth < 4 && !root.empty(); ++depth) {
        std::error_code existsError;
        const auto hasStandardLibrary = std::filesystem::is_directory(root / "std", existsError);
        existsError.clear();
        const auto hasFramework =
            std::filesystem::is_directory(root / "foundation", existsError);
        if (hasStandardLibrary && hasFramework && !existsError) {
            return root;
        }
        root = root.parent_path();
    }
    return std::nullopt;
}

} // namespace

std::filesystem::path sdkAsset(const std::filesystem::path &relative,
                               const std::filesystem::path &fallback) {
    if (const auto *configured = std::getenv("FOUNDATION_SDK_ROOT");
        configured != nullptr && configured[0] != '\0') {
        return std::filesystem::path(configured) / relative;
    }

    if (const auto root = installedSdkRoot(); root.has_value()) {
        return *root / relative;
    }

    return fallback;
}

} // namespace foundation
