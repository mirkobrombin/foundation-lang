#ifndef FOUNDATION_CODEGEN_HPP
#define FOUNDATION_CODEGEN_HPP

#include "foundation/ast.hpp"

#include <string>

namespace foundation {

[[nodiscard]] std::string emitC(const Program &program);

} // namespace foundation

#endif
