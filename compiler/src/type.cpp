#include "foundation/type.hpp"

namespace foundation {

const char *typeName(Type type) {
    switch (type.kind) {
    case TypeKind::Invalid:
        return "<invalid>";
    case TypeKind::Void:
        return "void";
    case TypeKind::Never:
        return "never";
    case TypeKind::I8:
        return "i8";
    case TypeKind::I16:
        return "i16";
    case TypeKind::I32:
        return "i32";
    case TypeKind::I64:
        return "i64";
    case TypeKind::U8:
        return "u8";
    case TypeKind::U16:
        return "u16";
    case TypeKind::U32:
        return "u32";
    case TypeKind::U64:
        return "u64";
    case TypeKind::Isize:
        return "isize";
    case TypeKind::Usize:
        return "usize";
    case TypeKind::F32:
        return "f32";
    case TypeKind::F64:
        return "f64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "String";
    case TypeKind::Raw:
        return "*";
    case TypeKind::RawConst:
        return "*const";
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
    case TypeKind::Task:
        return "Task";
    case TypeKind::Channel:
        return "Channel";
    case TypeKind::Sender:
        return "Sender";
    case TypeKind::Receiver:
        return "Receiver";
    }
    return "<invalid>";
}

bool isSignedInteger(Type type) {
    return type.kind == TypeKind::I8 || type.kind == TypeKind::I16 ||
           type.kind == TypeKind::I32 || type.kind == TypeKind::I64 ||
           type.kind == TypeKind::Isize;
}

bool isUnsignedInteger(Type type) {
    return type.kind == TypeKind::U8 || type.kind == TypeKind::U16 ||
           type.kind == TypeKind::U32 || type.kind == TypeKind::U64 ||
           type.kind == TypeKind::Usize;
}

bool isInteger(Type type) { return isSignedInteger(type) || isUnsignedInteger(type); }

bool isFloating(Type type) {
    return type.kind == TypeKind::F32 || type.kind == TypeKind::F64;
}

bool isNumeric(Type type) { return isInteger(type) || isFloating(type); }

bool isMachineScalar(Type type) {
    return type.kind == TypeKind::Void || type.kind == TypeKind::Never ||
           isNumeric(type) || type.kind == TypeKind::Bool;
}

} // namespace foundation
