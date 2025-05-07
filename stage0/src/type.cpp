#include "foundation/type.hpp"

namespace foundation {

const char *typeName(Type type) {
    switch (type.kind) {
    case TypeKind::Invalid:
        return "<invalid>";
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "i32";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "String";
    case TypeKind::Struct:
        return "<struct>";
    case TypeKind::Enum:
        return "<enum>";
    }
    return "<invalid>";
}

} // namespace foundation
