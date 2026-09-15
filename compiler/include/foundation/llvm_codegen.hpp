#ifndef FOUNDATION_LLVM_CODEGEN_HPP
#define FOUNDATION_LLVM_CODEGEN_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

struct LlvmCodegenOptions {
    std::string targetTriple;
    bool optimize{true};
    bool verifyAllocations{};
    bool debugInfo{true};
    std::vector<std::string> sourcePaths;
    std::optional<FirFunctionId> entry;
    std::optional<std::string> libraryPackage;
    std::string cpu{"generic"};
    // Sorted LLVM feature entries such as "+neon".
    std::vector<std::string> features{};
    bool positionIndependent{true};
    // Freestanding objects use function and data sections and gain no C library calls beyond the
    // memory primitives.
    bool freestanding{};
};

struct LlvmTargetSelection {
    std::string triple;
    std::string cpu;
    // False when the CPU defaults to generic, a name Clang rejects for some triples.
    bool cpuSelected{};
    std::vector<std::string> features;
};

[[nodiscard]] std::string defaultLlvmTargetTriple();
// Validates a freestanding triple, CPU, and feature list. Reports FDN8002, FDN8005, FDN8006, or
// FDN8007 and returns the normalized triple and sorted features.
[[nodiscard]] std::optional<LlvmTargetSelection> selectLlvmTarget(
    std::string_view triple, const std::optional<std::string> &cpu,
    const std::optional<std::string> &features, Diagnostics &diagnostics);
[[nodiscard]] std::string llvmFeatureString(const std::vector<std::string> &features);
[[nodiscard]] std::optional<std::string> emitLlvmIr(
    const FirProgram &program, std::string_view sourcePath,
    const LlvmCodegenOptions &options, Diagnostics &diagnostics);
[[nodiscard]] bool emitLlvmObject(
    const FirProgram &program, const std::filesystem::path &output,
    std::string_view sourcePath, const LlvmCodegenOptions &options,
    Diagnostics &diagnostics);

} // namespace foundation

#endif
