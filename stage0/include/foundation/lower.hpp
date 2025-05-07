#ifndef FOUNDATION_LOWER_HPP
#define FOUNDATION_LOWER_HPP

#include "foundation/ast.hpp"
#include "foundation/fir.hpp"
#include "foundation/sema.hpp"

namespace foundation {

[[nodiscard]] FirProgram lower(const Program &program, const SemanticModel &model);

} // namespace foundation

#endif
