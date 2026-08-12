#ifndef FOUNDATION_CODEGEN_HPP
#define FOUNDATION_CODEGEN_HPP

#include "foundation/fir.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace foundation {

[[nodiscard]] FirProgram prepareFirForBackend(
    const FirProgram &program,
    std::optional<FirFunctionId> entry = std::nullopt);
[[nodiscard]] std::string emitC(const FirProgram &program,
                                std::string_view sourcePath = "<memory>");
[[nodiscard]] std::string emitTestC(const FirProgram &program, FirFunctionId test,
                                    std::string_view sourcePath = "<memory>");
[[nodiscard]] std::string emitCHeader(const FirProgram &program);
[[nodiscard]] FirProgram specializePackageInterface(const FirProgram &program,
                                                    std::string_view packageName);

} // namespace foundation

#endif
