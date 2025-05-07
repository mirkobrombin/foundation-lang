#ifndef FOUNDATION_TYPE_HPP
#define FOUNDATION_TYPE_HPP

namespace foundation {

enum class TypeKind {
    Invalid,
    Void,
    I32,
    Bool,
    String,
};

[[nodiscard]] const char *typeName(TypeKind type);

} // namespace foundation

#endif
