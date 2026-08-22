#ifndef FOUNDATION_PROJECT_HPP
#define FOUNDATION_PROJECT_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/target.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace foundation {

struct LoadedProject {
    Program program;
    std::vector<DiagnosticSource> sources;
};

struct SourceOverlay {
    std::filesystem::path path;
    std::string contents;
};

enum class ProjectMode {
    Production,
    Test,
};

[[nodiscard]] std::optional<LoadedProject> loadProject(const std::filesystem::path &input,
                                                       Diagnostics &diagnostics,
                                                       const std::vector<SourceOverlay> &overlays = {},
                                                       ProjectMode mode = ProjectMode::Production,
                                                       TargetPlatform target = hostTargetPlatform());

} // namespace foundation

#endif
