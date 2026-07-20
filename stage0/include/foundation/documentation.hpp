#ifndef FOUNDATION_DOCUMENTATION_HPP
#define FOUNDATION_DOCUMENTATION_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace foundation {

struct ProjectAnalysis;

[[nodiscard]] std::string emitDocumentation(
    const ProjectAnalysis &analysis,
    const std::vector<std::size_t> &projectSources = {});

} // namespace foundation

#endif
