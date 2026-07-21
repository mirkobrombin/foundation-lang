#ifndef FOUNDATION_TYPE_HPP
#define FOUNDATION_TYPE_HPP

#include <cstddef>
#include <utility>
#include <vector>

namespace foundation {

enum class TypeKind {
    Invalid,
    Void,
    Never,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Isize,
    Usize,
    F32,
    F64,
    Bool,
    String,
    Raw,
    RawConst,
    Array,
    Slice,
    Own,
    View,
    Edit,
    Parameter,
    Struct,
    Enum,
    Contract,
    Function,
    Task,
    Channel,
    Sender,
    Receiver,
};

struct Type {
    TypeKind kind{TypeKind::Invalid};
    std::size_t declaration{};
    std::vector<Type> arguments;

    Type() = default;
    Type(TypeKind kind, std::size_t declaration = 0, std::vector<Type> arguments = {})
        : kind(kind), declaration(declaration), arguments(std::move(arguments)) {}

    bool operator==(const Type &) const = default;
};

inline constexpr std::size_t transferableFunctionQualifier = 1;

[[nodiscard]] inline bool isTransferableFunction(const Type &type) {
    return type.kind == TypeKind::Function &&
           type.declaration == transferableFunctionQualifier;
}

inline const Type invalidType{TypeKind::Invalid, 0, {}};
inline const Type voidType{TypeKind::Void, 0, {}};
inline const Type neverType{TypeKind::Never, 0, {}};
inline const Type i8Type{TypeKind::I8, 0, {}};
inline const Type i16Type{TypeKind::I16, 0, {}};
inline const Type i32Type{TypeKind::I32, 0, {}};
inline const Type i64Type{TypeKind::I64, 0, {}};
inline const Type u8Type{TypeKind::U8, 0, {}};
inline const Type u16Type{TypeKind::U16, 0, {}};
inline const Type u32Type{TypeKind::U32, 0, {}};
inline const Type u64Type{TypeKind::U64, 0, {}};
inline const Type isizeType{TypeKind::Isize, 0, {}};
inline const Type usizeType{TypeKind::Usize, 0, {}};
inline const Type f32Type{TypeKind::F32, 0, {}};
inline const Type f64Type{TypeKind::F64, 0, {}};
inline const Type boolType{TypeKind::Bool, 0, {}};
inline const Type stringType{TypeKind::String, 0, {}};

[[nodiscard]] const char *typeName(Type type);
[[nodiscard]] bool isSignedInteger(Type type);
[[nodiscard]] bool isUnsignedInteger(Type type);
[[nodiscard]] bool isInteger(Type type);
[[nodiscard]] bool isFloating(Type type);
[[nodiscard]] bool isNumeric(Type type);
[[nodiscard]] bool isMachineScalar(Type type);

} // namespace foundation

#endif
