#pragma once

#include <filesystem>
#include <string_view>

namespace foundation {

std::filesystem::path sdkAsset(const std::filesystem::path &relative,
                               const std::filesystem::path &fallback);

// Reports whether an SDK package may be imported under the freestanding target. The set grows
// only when a package's natives move into the runtime core.
[[nodiscard]] bool freestandingSdkPackage(std::string_view packageName);

} // namespace foundation
