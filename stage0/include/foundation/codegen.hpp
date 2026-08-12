#ifndef FOUNDATION_CODEGEN_HPP
#define FOUNDATION_CODEGEN_HPP

#include "foundation/fir.hpp"

#include <string>
#include <string_view>

namespace foundation {

[[nodiscard]] std::string emitC(const FirProgram &program,
                                std::string_view sourcePath = "<memory>");
[[nodiscard]] std::string emitTestC(const FirProgram &program, FirFunctionId test,
                                    std::string_view sourcePath = "<memory>");
[[nodiscard]] std::string emitCHeader(const FirProgram &program);
[[nodiscard]] FirProgram specializePackageInterface(const FirProgram &program,
                                                    std::string_view packageName);

} // namespace foundation

#endif
