#pragma once

#include <filesystem>

namespace foundation {

std::filesystem::path sdkAsset(const std::filesystem::path &relative,
                               const std::filesystem::path &fallback);

} // namespace foundation
