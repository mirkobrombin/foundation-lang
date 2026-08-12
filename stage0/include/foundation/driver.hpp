#ifndef FOUNDATION_DRIVER_HPP
#define FOUNDATION_DRIVER_HPP

#include "foundation/ast.hpp"
#include "foundation/backend.hpp"
#include "foundation/code_standard.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"
#include "foundation/fsm.hpp"
#include "foundation/project.hpp"
#include "foundation/sema.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace foundation {

struct Compilation {
    std::vector<DiagnosticSource> sources;
    std::optional<FirProgram> fir;
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

enum class FormatMode {
    Stdout,
    Check,
    Write,
};

[[nodiscard]] ProjectAnalysis analyzeProject(
    const std::filesystem::path &path,
    const std::vector<SourceOverlay> &overlays = {},
    AnalyzeOptions options = {},
    ProjectMode mode = ProjectMode::Production,
    TargetPlatform target = hostTargetPlatform());
[[nodiscard]] Compilation compile(const std::filesystem::path &path,
                                  const std::vector<SourceOverlay> &overlays = {},
                                  TargetPlatform target = hostTargetPlatform());
[[nodiscard]] int checkFile(const std::filesystem::path &path,
                            TargetPlatform target = hostTargetPlatform());
[[nodiscard]] int lintFile(
    const std::filesystem::path &path,
    std::optional<CodeStandardProfile> profile = std::nullopt);
[[nodiscard]] int formatPath(const std::filesystem::path &path, FormatMode mode);
[[nodiscard]] int emitCFile(const std::filesystem::path &source,
                            const std::filesystem::path &output,
                            TargetPlatform target = hostTargetPlatform());
[[nodiscard]] int emitCHeaderFile(const std::filesystem::path &source,
                                  const std::filesystem::path &output,
                                  TargetPlatform target = hostTargetPlatform());
[[nodiscard]] int emitLlvmIrFile(const std::filesystem::path &source,
                                 const std::filesystem::path &output);
[[nodiscard]] int emitMetadataFile(const std::filesystem::path &source,
                                   const std::filesystem::path &output,
                                   TargetPlatform target = hostTargetPlatform());
[[nodiscard]] int emitPackageInterfaceFile(const std::filesystem::path &source,
                                           const std::filesystem::path &output);
[[nodiscard]] int emitStateMachineDiagramFile(
    const std::filesystem::path &source, const std::filesystem::path &output,
    const std::optional<std::string> &machine, StateMachineDiagramFormat format);
[[nodiscard]] int emitDocumentationFile(const std::filesystem::path &source,
                                        const std::filesystem::path &output);
[[nodiscard]] int emitApplicationPlanFile(const std::filesystem::path &source,
                                          const std::filesystem::path &output);
[[nodiscard]] int emitOpenAPIFile(
    const std::filesystem::path &source, const std::filesystem::path &output,
    const std::optional<std::string> &title = std::nullopt,
    const std::optional<std::string> &version = std::nullopt);
[[nodiscard]] int emitApplicationHostFile(const std::filesystem::path &source,
                                          const std::filesystem::path &output);
[[nodiscard]] int buildFile(const std::filesystem::path &source,
                            const std::filesystem::path &output,
                            const std::vector<std::filesystem::path> &nativeInputs = {},
                            BackendKind backend = defaultBackendKind());
[[nodiscard]] int runFile(const std::filesystem::path &source,
                          const std::vector<std::filesystem::path> &nativeInputs = {},
                          const std::vector<std::string> &arguments = {},
                          BackendKind backend = defaultBackendKind());
[[nodiscard]] int runTests(
    const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &nativeInputs = {},
    BackendKind backend = defaultBackendKind());

} // namespace foundation

#endif
