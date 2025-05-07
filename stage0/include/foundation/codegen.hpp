#ifndef FOUNDATION_CODEGEN_HPP
#define FOUNDATION_CODEGEN_HPP

#include "foundation/fir.hpp"

#include <string>

namespace foundation {

[[nodiscard]] std::string emitC(const FirProgram &program);

} // namespace foundation

#endif
