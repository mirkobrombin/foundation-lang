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
    case TypeKind::U64:
        return "u64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "String";
    case TypeKind::Array:
        return "<array>";
    case TypeKind::Slice:
        return "<slice>";
    case TypeKind::Own:
        return "own";
    case TypeKind::View:
        return "view";
    case TypeKind::Edit:
        return "edit";
    case TypeKind::Parameter:
        return "<type parameter>";
    case TypeKind::Struct:
        return "<struct>";
    case TypeKind::Enum:
        return "<enum>";
    case TypeKind::Contract:
        return "<contract>";
    case TypeKind::Function:
        return "fn";
    }
    return "<invalid>";
}

} // namespace foundation
