#ifndef FOUNDATION_DRIVER_HPP
#define FOUNDATION_DRIVER_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/project.hpp"
#include "foundation/sema.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace foundation {

struct Compilation {
    std::vector<DiagnosticSource> sources;
    std::string generatedC;
    std::string generatedCHeader;
    std::string generatedMetadata;
    Diagnostics diagnostics;
};

struct ProjectAnalysis {
    std::vector<DiagnosticSource> sources;
    Program program;
    std::optional<SemanticModel> semantic;
    Diagnostics diagnostics;
};

[[nodiscard]] ProjectAnalysis analyzeProject(
    const std::filesystem::path &path,
    const std::vector<SourceOverlay> &overlays = {},
    AnalyzeOptions options = {});
[[nodiscard]] Compilation compile(const std::filesystem::path &path,
                                  const std::vector<SourceOverlay> &overlays = {});
[[nodiscard]] int checkFile(const std::filesystem::path &path);
[[nodiscard]] int emitCFile(const std::filesystem::path &source,
                            const std::filesystem::path &output);
[[nodiscard]] int emitCHeaderFile(const std::filesystem::path &source,
                                  const std::filesystem::path &output);
[[nodiscard]] int emitMetadataFile(const std::filesystem::path &source,
                                   const std::filesystem::path &output);
[[nodiscard]] int buildFile(const std::filesystem::path &source,
                            const std::filesystem::path &output,
                            const std::vector<std::filesystem::path> &nativeInputs = {});
[[nodiscard]] int runFile(const std::filesystem::path &source,
                          const std::vector<std::filesystem::path> &nativeInputs = {},
                          const std::vector<std::string> &arguments = {});

} // namespace foundation

#endif
