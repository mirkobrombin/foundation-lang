#ifndef FOUNDATION_TYPE_HPP
#define FOUNDATION_TYPE_HPP

#include <cstddef>
#include <utility>
#include <vector>

namespace foundation {

enum class TypeKind {
    Invalid,
    Void,
    I32,
    Bool,
    String,
    Array,
    Slice,
    Own,
    View,
    Edit,
    Parameter,
    Struct,
    Enum,
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

inline const Type invalidType{TypeKind::Invalid, 0, {}};
inline const Type voidType{TypeKind::Void, 0, {}};
inline const Type i32Type{TypeKind::I32, 0, {}};
inline const Type boolType{TypeKind::Bool, 0, {}};
inline const Type stringType{TypeKind::String, 0, {}};

[[nodiscard]] const char *typeName(Type type);

} // namespace foundation

#endif
