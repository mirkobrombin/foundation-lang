#ifndef FOUNDATION_CODEGEN_HPP
#define FOUNDATION_CODEGEN_HPP

#include "foundation/fir.hpp"

#include <string>
#include <string_view>

namespace foundation {

[[nodiscard]] std::string emitC(const FirProgram &program,
                                std::string_view sourcePath = "<memory>");

} // namespace foundation

#endif
