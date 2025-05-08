#ifndef FOUNDATION_PROJECT_HPP
#define FOUNDATION_PROJECT_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace foundation {

struct LoadedProject {
    Program program;
    std::vector<DiagnosticSource> sources;
};

[[nodiscard]] std::optional<LoadedProject> loadProject(const std::filesystem::path &input,
                                                       Diagnostics &diagnostics);

} // namespace foundation

#endif
