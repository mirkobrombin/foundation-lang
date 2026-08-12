#include "foundation/backend.hpp"

namespace foundation {

BackendKind defaultBackendKind() { return BackendKind::C; }

std::optional<BackendKind> parseBackendKind(std::string_view value) {
    if (value == "c") {
        return BackendKind::C;
    }
    if (value == "llvm") {
        return BackendKind::Llvm;
    }
    return std::nullopt;
}

std::string_view backendKindName(BackendKind backend) {
    switch (backend) {
    case BackendKind::C:
        return "c";
    case BackendKind::Llvm:
        return "llvm";
    }
    return "unknown";
}

} // namespace foundation
