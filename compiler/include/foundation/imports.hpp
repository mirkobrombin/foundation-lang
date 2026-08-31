#ifndef FOUNDATION_IMPORTS_HPP
#define FOUNDATION_IMPORTS_HPP

#include "foundation/diagnostic.hpp"

#include <cstddef>
#include <string>

namespace foundation {

struct ProjectAnalysis;

struct ImportOrganizationResult {
    std::string contents;
    Diagnostics diagnostics;
};

[[nodiscard]] ImportOrganizationResult organizeImports(const ProjectAnalysis &analysis,
                                                       std::size_t sourceId);

} // namespace foundation

#endif
