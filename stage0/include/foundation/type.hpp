#ifndef FOUNDATION_TYPE_HPP
#define FOUNDATION_TYPE_HPP

#include <cstddef>

namespace foundation {

enum class TypeKind {
    Invalid,
    Void,
    I32,
    Bool,
    String,
    Struct,
};

struct Type {
    TypeKind kind{TypeKind::Invalid};
    std::size_t declaration{};

    bool operator==(const Type &) const = default;
};

inline constexpr Type invalidType{TypeKind::Invalid};
inline constexpr Type voidType{TypeKind::Void};
inline constexpr Type i32Type{TypeKind::I32};
inline constexpr Type boolType{TypeKind::Bool};
inline constexpr Type stringType{TypeKind::String};

[[nodiscard]] const char *typeName(Type type);

} // namespace foundation

#endif
