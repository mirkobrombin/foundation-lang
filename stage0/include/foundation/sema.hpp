#ifndef FOUNDATION_SEMA_HPP
#define FOUNDATION_SEMA_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"

namespace foundation {

[[nodiscard]] bool analyze(const Program &program, Diagnostics &diagnostics);

} // namespace foundation

#endif
