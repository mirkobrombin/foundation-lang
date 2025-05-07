#ifndef FOUNDATION_DRIVER_HPP
#define FOUNDATION_DRIVER_HPP

#include "foundation/diagnostic.hpp"

#include <filesystem>
#include <string>

namespace foundation {

struct Compilation {
    std::string source;
    std::string generatedC;
    Diagnostics diagnostics;
};

[[nodiscard]] Compilation compile(const std::filesystem::path &path);
[[nodiscard]] int checkFile(const std::filesystem::path &path);
[[nodiscard]] int emitCFile(const std::filesystem::path &source,
                            const std::filesystem::path &output);
[[nodiscard]] int buildFile(const std::filesystem::path &source,
                            const std::filesystem::path &output);
[[nodiscard]] int runFile(const std::filesystem::path &source);

} // namespace foundation

#endif
