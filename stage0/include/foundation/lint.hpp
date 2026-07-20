#ifndef FOUNDATION_LINT_HPP
#define FOUNDATION_LINT_HPP

#include "foundation/code_standard.hpp"
#include "foundation/diagnostic.hpp"

#include <cstddef>
#include <vector>

namespace foundation {

struct ProjectAnalysis;

[[nodiscard]] Diagnostics lintProject(
    const ProjectAnalysis &analysis, CodeStandardProfile profile,
    const std::vector<std::size_t> &projectSources = {});

} // namespace foundation

#endif
