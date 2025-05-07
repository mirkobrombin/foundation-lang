#include "foundation/type.hpp"

namespace foundation {

const char *typeName(TypeKind type) {
    switch (type) {
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
    }
    return "<invalid>";
}

} // namespace foundation
