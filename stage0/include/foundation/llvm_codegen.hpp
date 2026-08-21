#ifndef FOUNDATION_LLVM_CODEGEN_HPP
#define FOUNDATION_LLVM_CODEGEN_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace foundation {

struct LlvmCodegenOptions {
    std::string targetTriple;
    bool optimize{true};
    bool verifyAllocations{};
    bool debugInfo{true};
    std::optional<FirFunctionId> entry;
    std::optional<std::string> libraryPackage;
};

[[nodiscard]] std::string defaultLlvmTargetTriple();
[[nodiscard]] std::optional<std::string> emitLlvmIr(
    const FirProgram &program, std::string_view sourcePath,
    const LlvmCodegenOptions &options, Diagnostics &diagnostics);
[[nodiscard]] bool emitLlvmObject(
    const FirProgram &program, const std::filesystem::path &output,
    std::string_view sourcePath, const LlvmCodegenOptions &options,
    Diagnostics &diagnostics);

} // namespace foundation

#endif
