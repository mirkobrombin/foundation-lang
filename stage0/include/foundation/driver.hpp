#ifndef FOUNDATION_DRIVER_HPP
#define FOUNDATION_DRIVER_HPP

#include "foundation/diagnostic.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace foundation {

struct Compilation {
    std::vector<DiagnosticSource> sources;
    std::string generatedC;
    std::string generatedCHeader;
    Diagnostics diagnostics;
};

[[nodiscard]] Compilation compile(const std::filesystem::path &path);
[[nodiscard]] int checkFile(const std::filesystem::path &path);
[[nodiscard]] int emitCFile(const std::filesystem::path &source,
                            const std::filesystem::path &output);
[[nodiscard]] int emitCHeaderFile(const std::filesystem::path &source,
                                  const std::filesystem::path &output);
[[nodiscard]] int buildFile(const std::filesystem::path &source,
                            const std::filesystem::path &output,
                            const std::vector<std::filesystem::path> &nativeInputs = {});
[[nodiscard]] int runFile(const std::filesystem::path &source,
                          const std::vector<std::filesystem::path> &nativeInputs = {},
                          const std::vector<std::string> &arguments = {});

} // namespace foundation

#endif
