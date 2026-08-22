#ifndef FOUNDATION_BACKEND_HPP
#define FOUNDATION_BACKEND_HPP

#include <optional>
#include <string_view>

namespace foundation {

enum class BackendKind {
    C,
    Llvm,
};

[[nodiscard]] BackendKind defaultBackendKind();
[[nodiscard]] std::optional<BackendKind> parseBackendKind(std::string_view value);
[[nodiscard]] std::string_view backendKindName(BackendKind backend);

} // namespace foundation

#endif
