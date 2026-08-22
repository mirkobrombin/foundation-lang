#include "foundation/package_export.hpp"

#include "foundation/codegen.hpp"
#include "foundation/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

namespace {

FirBinaryOperator assignmentBinary(FirAssignmentOperator operation) {
    switch (operation) {
    case FirAssignmentOperator::Add:
        return FirBinaryOperator::Add;
    case FirAssignmentOperator::Subtract:
        return FirBinaryOperator::Subtract;
    case FirAssignmentOperator::Multiply:
        return FirBinaryOperator::Multiply;
    case FirAssignmentOperator::Divide:
        return FirBinaryOperator::Divide;
    case FirAssignmentOperator::Remainder:
        return FirBinaryOperator::Remainder;
    case FirAssignmentOperator::ShiftLeft:
        return FirBinaryOperator::ShiftLeft;
    case FirAssignmentOperator::ShiftRight:
        return FirBinaryOperator::ShiftRight;
    case FirAssignmentOperator::Assign:
        break;
    }
    std::terminate();
}

std::string_view shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return name.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

std::string_view memberOwnerName(std::string_view name) {
    const auto memberSeparator = name.rfind('.');
    if (memberSeparator == std::string_view::npos || memberSeparator == 0) {
        return {};
    }
    const auto ownerSeparator = name.rfind('.', memberSeparator - 1);
    const auto start = ownerSeparator == std::string_view::npos ? 0 : ownerSeparator + 1;
    return name.substr(start, memberSeparator - start);
}

std::vector<std::string> words(std::string_view value) {
    std::vector<std::string> result;
    std::string current;
    for (std::size_t index{}; index < value.size(); ++index) {
        const auto raw = static_cast<unsigned char>(value[index]);
        if (!std::isalnum(raw)) {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        const auto upper = std::isupper(raw) != 0;
        const auto previousLower =
            !current.empty() && std::islower(static_cast<unsigned char>(current.back())) != 0;
        const auto nextLower = index + 1 < value.size() &&
                               std::islower(static_cast<unsigned char>(value[index + 1])) != 0;
        if (upper && !current.empty() && (previousLower || nextLower)) {
            result.push_back(std::move(current));
            current.clear();
        }
        current.push_back(static_cast<char>(std::tolower(raw)));
    }
    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

std::string snakeName(std::string_view value) {
    const auto parts = words(value);
    std::string result;
    for (const auto &part : parts) {
        if (!result.empty()) {
            result.push_back('_');
        }
        result += part;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::string camelName(std::string_view value) {
    const auto parts = words(value);
    std::string result;
    for (std::size_t index{}; index < parts.size(); ++index) {
        auto part = parts[index];
        if (index != 0 && !part.empty()) {
            part.front() =
                static_cast<char>(std::toupper(static_cast<unsigned char>(part.front())));
        }
        result += part;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::string typeName(std::string_view value) {
    const auto parts = words(shortName(value));
    std::string result;
    for (auto part : parts) {
        if (!part.empty()) {
            part.front() =
                static_cast<char>(std::toupper(static_cast<unsigned char>(part.front())));
        }
        result += part;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

bool reservedGo(std::string_view value) {
    static const std::set<std::string_view> names{
        "any",    "append",      "bool",   "break",      "byte",    "cap",        "case",
        "chan",   "clear",       "close",  "comparable", "complex", "complex128", "complex64",
        "const",  "continue",    "copy",   "default",    "defer",   "delete",     "else",
        "error",  "fallthrough", "false",  "float32",    "float64", "for",        "func",
        "go",     "goto",        "if",     "imag",       "import",  "int",        "int16",
        "int32",  "int64",       "int8",   "interface",  "iota",    "len",        "make",
        "map",    "max",         "min",    "new",        "nil",     "package",    "panic",
        "print",  "println",     "range",  "real",       "recover", "return",     "rune",
        "select", "string",      "struct", "switch",     "true",    "type",       "uint",
        "uint16", "uint32",      "uint64", "uint8",      "uintptr", "var",
    };
    return names.contains(value);
}

std::string goIdentifier(std::string_view value) {
    auto result = camelName(value);
    if (reservedGo(result)) {
        result.push_back('_');
    }
    return result;
}

bool reservedRust(std::string_view value) {
    static const std::set<std::string_view> names{
        "as",    "break",  "const",    "continue", "crate",  "else", "enum",  "extern", "false",
        "fn",    "for",    "if",       "impl",     "in",     "let",  "loop",  "match",  "mod",
        "move",  "mut",    "pub",      "ref",      "return", "self", "Self",  "static", "struct",
        "super", "trait",  "true",     "type",     "unsafe", "use",  "where", "while",  "async",
        "await", "dyn",    "abstract", "become",   "box",    "do",   "final", "macro",  "override",
        "priv",  "typeof", "unsized",  "virtual",  "yield",  "try",
    };
    return names.contains(value);
}

std::string rustIdentifier(std::string_view value) {
    auto result = snakeName(value);
    if (reservedRust(result)) {
        result.push_back('_');
    }
    return result;
}

bool reservedZig(std::string_view value) {
    static const std::set<std::string_view> names{
        "align",   "allowzero", "and",         "anyframe",
        "anytype", "asm",       "async",       "await",
        "break",   "callconv",  "catch",       "comptime",
        "const",   "continue",  "defer",       "else",
        "enum",    "errdefer",  "error",       "export",
        "extern",  "fn",        "for",         "if",
        "inline",  "noalias",   "nosuspend",   "opaque",
        "or",      "orelse",    "packed",      "pub",
        "resume",  "return",    "linksection", "struct",
        "suspend", "switch",    "test",        "threadlocal",
        "try",     "union",     "unreachable", "usingnamespace",
        "var",     "volatile",  "while",
    };
    return names.contains(value);
}

std::string zigIdentifier(std::string_view value) {
    auto result = camelName(value);
    if (reservedZig(result)) {
        result.push_back('_');
    }
    return result;
}

std::optional<std::string> scalarZigType(PiiTypeKind kind) {
    switch (kind) {
    case PiiTypeKind::Void:
        return "void";
    case PiiTypeKind::I8:
        return "i8";
    case PiiTypeKind::I16:
        return "i16";
    case PiiTypeKind::I32:
        return "i32";
    case PiiTypeKind::I64:
        return "i64";
    case PiiTypeKind::ISize:
        return "isize";
    case PiiTypeKind::U8:
        return "u8";
    case PiiTypeKind::U16:
        return "u16";
    case PiiTypeKind::U32:
        return "u32";
    case PiiTypeKind::U64:
        return "u64";
    case PiiTypeKind::USize:
        return "usize";
    case PiiTypeKind::F32:
        return "f32";
    case PiiTypeKind::F64:
        return "f64";
    case PiiTypeKind::Bool:
        return "bool";
    case PiiTypeKind::String:
        return "FoundationString";
    default:
        return std::nullopt;
    }
}

std::optional<std::string> scalarRustType(PiiTypeKind kind) {
    switch (kind) {
    case PiiTypeKind::Void:
        return "()";
    case PiiTypeKind::I8:
        return "i8";
    case PiiTypeKind::I16:
        return "i16";
    case PiiTypeKind::I32:
        return "i32";
    case PiiTypeKind::I64:
        return "i64";
    case PiiTypeKind::ISize:
        return "isize";
    case PiiTypeKind::U8:
        return "u8";
    case PiiTypeKind::U16:
        return "u16";
    case PiiTypeKind::U32:
        return "u32";
    case PiiTypeKind::U64:
        return "u64";
    case PiiTypeKind::USize:
        return "usize";
    case PiiTypeKind::F32:
        return "f32";
    case PiiTypeKind::F64:
        return "f64";
    case PiiTypeKind::Bool:
        return "bool";
    case PiiTypeKind::String:
        return "FoundationString";
    default:
        return std::nullopt;
    }
}

std::optional<std::string> zigType(const PiiType &type);
std::optional<std::string> rustType(const PiiType &type);

std::optional<std::string> zigFunctionType(const PiiType &type) {
    if (type.abi != "c11" || type.arguments.empty()) {
        return std::nullopt;
    }
    const auto result = zigType(type.arguments.front());
    if (!result.has_value()) {
        return std::nullopt;
    }
    std::ostringstream output;
    output << "*const fn (";
    for (std::size_t index = 1; index < type.arguments.size(); ++index) {
        const auto parameter = zigType(type.arguments[index]);
        if (!parameter.has_value()) {
            return std::nullopt;
        }
        if (index != 1) {
            output << ", ";
        }
        output << *parameter;
    }
    output << ") callconv(.c) " << *result;
    return output.str();
}

std::optional<std::string> rustFunctionType(const PiiType &type) {
    if (type.abi != "c11" || type.arguments.empty()) {
        return std::nullopt;
    }
    const auto result = rustType(type.arguments.front());
    if (!result.has_value()) {
        return std::nullopt;
    }
    std::ostringstream output;
    output << "unsafe extern \"C\" fn(";
    for (std::size_t index = 1; index < type.arguments.size(); ++index) {
        const auto parameter = rustType(type.arguments[index]);
        if (!parameter.has_value()) {
            return std::nullopt;
        }
        if (index != 1) {
            output << ", ";
        }
        output << *parameter;
    }
    output << ") -> " << *result;
    return output.str();
}

std::optional<std::string> zigType(const PiiType &type) {
    if (const auto scalar = scalarZigType(type.kind); scalar.has_value()) {
        return scalar;
    }
    if (type.kind == PiiTypeKind::Struct) {
        return typeName(type.name);
    }
    if ((type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) &&
        type.arguments.size() == 1) {
        auto target = zigType(type.arguments.front());
        if (!target.has_value()) {
            return std::nullopt;
        }
        if (*target == "void") {
            *target = "anyopaque";
        }
        return std::string("?*") + (type.kind == PiiTypeKind::RawConst ? "const " : "") + *target;
    }
    if (type.kind == PiiTypeKind::Function) {
        return zigFunctionType(type);
    }
    return std::nullopt;
}

std::optional<std::string> rustType(const PiiType &type) {
    if (const auto scalar = scalarRustType(type.kind); scalar.has_value()) {
        return scalar;
    }
    if (type.kind == PiiTypeKind::Struct) {
        return typeName(type.name);
    }
    if ((type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) &&
        type.arguments.size() == 1) {
        auto target = rustType(type.arguments.front());
        if (!target.has_value()) {
            return std::nullopt;
        }
        if (*target == "()") {
            *target = "core::ffi::c_void";
        }
        return std::string(type.kind == PiiTypeKind::RawConst ? "*const " : "*mut ") + *target;
    }
    if (type.kind == PiiTypeKind::Function) {
        return rustFunctionType(type);
    }
    return std::nullopt;
}

std::string goPackageName(const PackageInterface &packageInterface) {
    auto result = snakeName(packageInterface.library);
    if (reservedGo(result)) {
        result.push_back('_');
    }
    return result;
}

std::string goModulePath(const PackageInterface &packageInterface) {
    if (packageInterface.package.find('.') != std::string::npos) {
        return packageInterface.package;
    }
    return "foundation.local/" + snakeName(packageInterface.package);
}

std::optional<std::string> scalarGoType(PiiTypeKind kind) {
    switch (kind) {
    case PiiTypeKind::Void:
        return "";
    case PiiTypeKind::I8:
        return "int8";
    case PiiTypeKind::I16:
        return "int16";
    case PiiTypeKind::I32:
        return "int32";
    case PiiTypeKind::I64:
        return "int64";
    case PiiTypeKind::ISize:
        return "int";
    case PiiTypeKind::U8:
        return "uint8";
    case PiiTypeKind::U16:
        return "uint16";
    case PiiTypeKind::U32:
        return "uint32";
    case PiiTypeKind::U64:
        return "uint64";
    case PiiTypeKind::USize:
        return "uint";
    case PiiTypeKind::F32:
        return "float32";
    case PiiTypeKind::F64:
        return "float64";
    case PiiTypeKind::Bool:
        return "bool";
    case PiiTypeKind::String:
        return "string";
    default:
        return std::nullopt;
    }
}

std::optional<std::string> goSourceBoundaryType(const PiiType &type) {
    return scalarGoType(type.kind);
}

std::optional<std::string> goSourceType(Type type) {
    if (type.kind == TypeKind::View && type.arguments.size() == 1) {
        type = type.arguments.front();
    }
    switch (type.kind) {
    case TypeKind::Void:
        return "";
    case TypeKind::I8:
        return "int8";
    case TypeKind::I16:
        return "int16";
    case TypeKind::I32:
        return "int32";
    case TypeKind::I64:
        return "int64";
    case TypeKind::Isize:
        return "int";
    case TypeKind::U8:
        return "uint8";
    case TypeKind::U16:
        return "uint16";
    case TypeKind::U32:
        return "uint32";
    case TypeKind::U64:
        return "uint64";
    case TypeKind::Usize:
        return "uint";
    case TypeKind::F32:
        return "float32";
    case TypeKind::F64:
        return "float64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "string";
    default:
        return std::nullopt;
    }
}

Type substituteGoSourceType(const Type &type, const std::vector<Type> &arguments) {
    if (type.kind == TypeKind::Parameter) {
        return type.declaration < arguments.size() ? arguments[type.declaration] : invalidType;
    }
    auto result = type;
    for (auto &argument : result.arguments) {
        argument = substituteGoSourceType(argument, arguments);
    }
    return result;
}

std::string goSourceTypeKey(const Type &type) {
    std::string result = std::to_string(static_cast<unsigned int>(type.kind)) + ':' +
                         std::to_string(type.declaration);
    if (!type.arguments.empty()) {
        result += '<';
        for (const auto &argument : type.arguments) {
            result += goSourceTypeKey(argument) + ';';
        }
        result += '>';
    }
    return result;
}

std::string goQuotedString(std::string_view value) {
    static constexpr std::string_view hex = "0123456789abcdef";
    std::string result{"\""};
    for (const auto raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20 || byte == 0x7f) {
                result += "\\x";
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0x0f]);
            } else {
                result.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

std::string goSourceTypeTag(Type type) {
    if (type.kind == TypeKind::View && type.arguments.size() == 1) {
        type = type.arguments.front();
    }
    switch (type.kind) {
    case TypeKind::I8:
        return "I8";
    case TypeKind::I16:
        return "I16";
    case TypeKind::I32:
        return "I32";
    case TypeKind::I64:
        return "I64";
    case TypeKind::Isize:
        return "Isize";
    case TypeKind::U8:
        return "U8";
    case TypeKind::U16:
        return "U16";
    case TypeKind::U32:
        return "U32";
    case TypeKind::U64:
        return "U64";
    case TypeKind::Usize:
        return "Usize";
    default:
        return {};
    }
}

std::string piiTypeKey(const PiiType &type) {
    std::ostringstream output;
    output << static_cast<int>(type.kind) << ':' << type.name << ':' << type.abi << ':'
           << type.nullable << '<';
    for (const auto &argument : type.arguments) {
        output << piiTypeKey(argument) << ';';
    }
    output << '>';
    return output.str();
}

using GoCallbackTypes = std::map<std::string, PiiType>;

void collectGoCallbacks(const PiiType &type, GoCallbackTypes &callbacks) {
    if (type.kind == PiiTypeKind::Function) {
        callbacks.emplace(piiTypeKey(type), type);
    }
    for (const auto &argument : type.arguments) {
        collectGoCallbacks(argument, callbacks);
    }
}

GoCallbackTypes goCallbackTypes(const PackageInterface &packageInterface) {
    GoCallbackTypes result;
    for (const auto &layout : packageInterface.layouts) {
        for (const auto &field : layout.fields) {
            collectGoCallbacks(field.type, result);
        }
    }
    for (const auto &function : packageInterface.exports) {
        collectGoCallbacks(function.result, result);
        for (const auto &parameter : function.parameters) {
            collectGoCallbacks(parameter.type, result);
        }
    }
    return result;
}

std::string goCallbackName(const PiiType &type, const GoCallbackTypes &callbacks) {
    const auto found = callbacks.find(piiTypeKey(type));
    const auto index = static_cast<std::size_t>(std::distance(callbacks.begin(), found));
    return "Callback" + std::to_string(index);
}

std::optional<std::string> goType(const PiiType &type, const GoCallbackTypes &callbacks,
                                  bool dynamicFunction, bool layoutField = false);

std::optional<std::string> goFunctionType(const PiiType &type, const GoCallbackTypes &callbacks) {
    if (type.abi != "c11" || type.arguments.empty()) {
        return std::nullopt;
    }
    std::ostringstream output;
    output << "func(";
    for (std::size_t index = 1; index < type.arguments.size(); ++index) {
        const auto parameter = goType(type.arguments[index], callbacks, true);
        if (!parameter.has_value()) {
            return std::nullopt;
        }
        if (index != 1) {
            output << ", ";
        }
        output << *parameter;
    }
    output << ')';
    const auto result = goType(type.arguments.front(), callbacks, true);
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (!result->empty()) {
        output << ' ' << *result;
    }
    return output.str();
}

std::optional<std::string> goType(const PiiType &type, const GoCallbackTypes &callbacks,
                                  bool dynamicFunction, bool layoutField) {
    if (const auto scalar = scalarGoType(type.kind); scalar.has_value()) {
        return scalar;
    }
    if (type.kind == PiiTypeKind::Struct) {
        return typeName(type.name);
    }
    if ((type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) &&
        type.arguments.size() == 1) {
        const auto target = goType(type.arguments.front(), callbacks, dynamicFunction);
        if (!target.has_value()) {
            return std::nullopt;
        }
        if (target->empty()) {
            return "unsafe.Pointer";
        }
        return "*" + *target;
    }
    if (type.kind == PiiTypeKind::Function) {
        if (dynamicFunction && !layoutField) {
            return goFunctionType(type, callbacks);
        }
        return goCallbackName(type, callbacks);
    }
    return std::nullopt;
}

std::optional<std::string> scalarCType(PiiTypeKind kind) {
    switch (kind) {
    case PiiTypeKind::Void:
        return "void";
    case PiiTypeKind::I8:
        return "int8_t";
    case PiiTypeKind::I16:
        return "int16_t";
    case PiiTypeKind::I32:
        return "int32_t";
    case PiiTypeKind::I64:
        return "int64_t";
    case PiiTypeKind::ISize:
        return "intptr_t";
    case PiiTypeKind::U8:
        return "uint8_t";
    case PiiTypeKind::U16:
        return "uint16_t";
    case PiiTypeKind::U32:
        return "uint32_t";
    case PiiTypeKind::U64:
        return "uint64_t";
    case PiiTypeKind::USize:
        return "uintptr_t";
    case PiiTypeKind::F32:
        return "float";
    case PiiTypeKind::F64:
        return "double";
    case PiiTypeKind::Bool:
        return "bool";
    case PiiTypeKind::String:
        return "fdn_string";
    default:
        return std::nullopt;
    }
}

const PiiStructLayout *goLayout(const PackageInterface &packageInterface, std::string_view name) {
    const auto found =
        std::find_if(packageInterface.layouts.begin(), packageInterface.layouts.end(),
                     [&](const auto &layout) { return layout.foundationName == name; });
    return found == packageInterface.layouts.end() ? nullptr : &*found;
}

std::optional<std::string> cGoType(const PiiType &type, const PackageInterface &packageInterface,
                                   const GoCallbackTypes &callbacks) {
    static_cast<void>(callbacks);
    if (const auto scalar = scalarCType(type.kind); scalar.has_value()) {
        return scalar;
    }
    if (type.kind == PiiTypeKind::Struct) {
        const auto *layout = goLayout(packageInterface, type.name);
        return layout == nullptr ? std::nullopt : std::optional<std::string>{layout->cName};
    }
    if ((type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) &&
        type.arguments.size() == 1) {
        auto target = cGoType(type.arguments.front(), packageInterface, callbacks);
        if (!target.has_value()) {
            return std::nullopt;
        }
        return std::string(type.kind == PiiTypeKind::RawConst ? "const " : "") + *target + " *";
    }
    if (type.kind == PiiTypeKind::Function) {
        return "uintptr_t";
    }
    return std::nullopt;
}

std::string cGoCallbackType(const PiiType &type, const PackageInterface &packageInterface,
                            const GoCallbackTypes &callbacks, std::size_t index) {
    std::ostringstream output;
    output << "typedef " << *cGoType(type.arguments.front(), packageInterface, callbacks)
           << " (*fdn_go_callback_" << index << ")(";
    if (type.arguments.size() == 1) {
        output << "void";
    } else {
        for (std::size_t argument = 1; argument < type.arguments.size(); ++argument) {
            if (argument != 1) {
                output << ", ";
            }
            output << *cGoType(type.arguments[argument], packageInterface, callbacks);
        }
    }
    output << ");\n";
    return output.str();
}

std::string cGoCallHelpers(const PackageInterface &packageInterface,
                           const GoCallbackTypes &callbacks) {
    std::ostringstream output;
    std::size_t callbackIndex{};
    for (const auto &[key, type] : callbacks) {
        static_cast<void>(key);
        output << cGoCallbackType(type, packageInterface, callbacks, callbackIndex++);
    }
    if (!callbacks.empty()) {
        output << '\n';
    }
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        output << "static inline " << *cGoType(function.result, packageInterface, callbacks)
               << " fdn_go_call_" << index << '(';
        if (function.parameters.empty()) {
            output << "void";
        } else {
            for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
                if (parameter != 0) {
                    output << ", ";
                }
                output << *cGoType(function.parameters[parameter].type, packageInterface, callbacks)
                       << " arg" << parameter;
            }
        }
        output << ") {\n    ";
        if (function.result.kind != PiiTypeKind::Void) {
            output << "return ";
        }
        output << function.cSymbol << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            if (function.parameters[parameter].type.kind == PiiTypeKind::Function) {
                output << "(fdn_go_callback_"
                       << std::distance(
                              callbacks.begin(),
                              callbacks.find(piiTypeKey(function.parameters[parameter].type)))
                       << ")";
            }
            output << "arg" << parameter;
        }
        output << ");\n}\n";
    }
    return output.str();
}

std::string cgoLinkFlags(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "-L${SRCDIR}/native/lib -l" << packageInterface.library;
    for (const auto &link : packageInterface.links) {
        output << " -l" << link.name;
    }
    return output.str();
}

std::string cgoScalarCast(PiiTypeKind kind) {
    switch (kind) {
    case PiiTypeKind::I8:
        return "C.int8_t";
    case PiiTypeKind::I16:
        return "C.int16_t";
    case PiiTypeKind::I32:
        return "C.int32_t";
    case PiiTypeKind::I64:
        return "C.int64_t";
    case PiiTypeKind::ISize:
        return "C.intptr_t";
    case PiiTypeKind::U8:
        return "C.uint8_t";
    case PiiTypeKind::U16:
        return "C.uint16_t";
    case PiiTypeKind::U32:
        return "C.uint32_t";
    case PiiTypeKind::U64:
        return "C.uint64_t";
    case PiiTypeKind::USize:
        return "C.uintptr_t";
    case PiiTypeKind::F32:
        return "C.float";
    case PiiTypeKind::F64:
        return "C.double";
    case PiiTypeKind::Bool:
        return "C.bool";
    default:
        return {};
    }
}

std::string cgoArgument(const PiiParameter &parameter, const PackageInterface &packageInterface,
                        const GoCallbackTypes &callbacks) {
    const auto name = camelName(parameter.name);
    if (scalarGoType(parameter.type.kind).has_value() &&
        parameter.type.kind != PiiTypeKind::String) {
        return cgoScalarCast(parameter.type.kind) + '(' + name + ')';
    }
    if (parameter.type.kind == PiiTypeKind::String) {
        return "foundationBorrowString(" + name + ')';
    }
    if (parameter.type.kind == PiiTypeKind::Function) {
        return "C.uintptr_t(" + name + ')';
    }
    if (parameter.type.kind == PiiTypeKind::Raw || parameter.type.kind == PiiTypeKind::RawConst) {
        if (parameter.type.arguments.front().kind == PiiTypeKind::Void) {
            return name;
        }
        const auto *layout = goLayout(packageInterface, parameter.type.arguments.front().name);
        return "(*C." + layout->cName + ")(unsafe.Pointer(" + name + "))";
    }
    static_cast<void>(callbacks);
    return name;
}

std::string cgoResultExpression(const PiiType &type, std::string_view expression) {
    if (const auto scalar = scalarGoType(type.kind);
        scalar.has_value() && type.kind != PiiTypeKind::String) {
        return *scalar + '(' + std::string(expression) + ')';
    }
    if (type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) {
        if (type.arguments.front().kind == PiiTypeKind::Void) {
            return "unsafe.Pointer(" + std::string(expression) + ')';
        }
        return "(*" + typeName(type.arguments.front().name) + ")(unsafe.Pointer(" +
               std::string(expression) + "))";
    }
    return std::string(expression);
}

bool rawBoundary(const PiiFunction &function) {
    const auto raw = [](const PiiType &type) {
        return type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst;
    };
    return raw(function.result) ||
           std::any_of(function.parameters.begin(), function.parameters.end(),
                       [&](const auto &parameter) { return raw(parameter.type); });
}

bool validateMappedTypes(const PackageInterface &packageInterface, PackageExportFormat format,
                         Diagnostics &diagnostics) {
    const auto callbacks = goCallbackTypes(packageInterface);
    const auto mapped = [&](const PiiType &type) {
        switch (format) {
        case PackageExportFormat::Zig:
            return zigType(type);
        case PackageExportFormat::Rust:
            return rustType(type);
        case PackageExportFormat::GoCgo:
            return goType(type, callbacks, false);
        case PackageExportFormat::GoDynamic:
            return goType(type, callbacks, true);
        case PackageExportFormat::GoSource:
            return goSourceBoundaryType(type);
        }
        return std::optional<std::string>{};
    };
    for (const auto &layout : packageInterface.layouts) {
        for (const auto &field : layout.fields) {
            if (!mapped(field.type).has_value()) {
                const auto sourceMode = format == PackageExportFormat::GoSource;
                diagnostics.error(
                    sourceMode ? "FDN4120" : "FDN2130",
                    "package export cannot map field " + layout.foundationName + '.' +
                        field.foundationName +
                        (sourceMode ? "; use go-cgo or go-dynamic for this boundary" : ""),
                    {0, 0, 1, 1});
                return false;
            }
        }
    }
    for (const auto &function : packageInterface.exports) {
        if (!mapped(function.result).has_value()) {
            const auto sourceMode = format == PackageExportFormat::GoSource;
            diagnostics.error(
                sourceMode ? "FDN4120" : "FDN2130",
                "package export cannot map result of " + function.foundationName +
                    (sourceMode ? "; use go-cgo or go-dynamic for this boundary" : ""),
                {0, 0, 1, 1});
            return false;
        }
        for (const auto &parameter : function.parameters) {
            if (!mapped(parameter.type).has_value()) {
                const auto sourceMode = format == PackageExportFormat::GoSource;
                diagnostics.error(
                    sourceMode ? "FDN4120" : "FDN2130",
                    "package export cannot map parameter " + parameter.name + " of " +
                        function.foundationName +
                        (sourceMode ? "; use go-cgo or go-dynamic for this boundary" : ""),
                    {0, 0, 1, 1});
                return false;
            }
        }
    }
    return true;
}

std::string staticLibraryPath(const PackageInterface &packageInterface) {
#ifdef _WIN32
    return "native/lib/" + packageInterface.library + ".lib";
#else
    return "native/lib/lib" + packageInterface.library + ".a";
#endif
}

std::uint32_t crc32(std::string_view value) {
    auto result = std::uint32_t{0xffffffff};
    for (const auto raw : value) {
        result ^= static_cast<unsigned char>(raw);
        for (auto bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(result & 1));
            result = (result >> 1) ^ (0xedb88320 & mask);
        }
    }
    return ~result;
}

std::uint32_t packageFingerprintId(const PackageInterface &packageInterface) {
    const auto digest = sha256Hex("foundation-zig-package:" + packageInterface.package);
    std::uint32_t result{};
    for (std::size_t index{}; index < 8; ++index) {
        const auto raw = static_cast<unsigned char>(digest[index]);
        const auto digit =
            static_cast<std::uint32_t>(std::isdigit(raw) != 0 ? raw - '0' : raw - 'a' + 10);
        result = (result << 4) | digit;
    }
    if (result == 0 || result == 0xffffffff) {
        result ^= 0x5a17c9e3;
    }
    return result;
}

std::string renderZigSource(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "pub const FoundationString = extern struct {\n"
           << "    data: [*c]const u8,\n"
           << "    length: usize,\n"
           << "    owned: u8,\n"
           << "};\n\n";
    for (const auto &layout : packageInterface.layouts) {
        output << "pub const " << typeName(layout.foundationName) << " = extern struct {\n";
        for (const auto &field : layout.fields) {
            output << "    " << zigIdentifier(field.foundationName) << ": " << *zigType(field.type)
                   << ",\n";
        }
        output << "};\n\n";
    }
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        output << "extern fn " << function.cSymbol << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << zigIdentifier(function.parameters[parameter].name) << ": "
                   << *zigType(function.parameters[parameter].type);
        }
        output << ") callconv(.c) " << *zigType(function.result) << ";\n";
        output << "pub fn " << zigIdentifier(shortName(function.foundationName)) << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << zigIdentifier(function.parameters[parameter].name) << ": "
                   << *zigType(function.parameters[parameter].type);
        }
        output << ") " << *zigType(function.result) << " {\n    ";
        if (function.result.kind != PiiTypeKind::Void) {
            output << "return ";
        }
        output << function.cSymbol << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << zigIdentifier(function.parameters[parameter].name);
        }
        output << ");\n}\n";
        if (index + 1 != packageInterface.exports.size()) {
            output << '\n';
        }
    }
    return output.str();
}

std::string renderZigBuild(const PackageInterface &packageInterface) {
    const auto module = snakeName(packageInterface.library);
    std::ostringstream output;
    output << "const std = @import(\"std\");\n\n"
           << "pub fn build(b: *std.Build) void {\n"
           << "    const target = b.standardTargetOptions(.{});\n"
           << "    const module = b.addModule(\"" << module << "\", .{\n"
           << "        .root_source_file = b.path(\"src/root.zig\"),\n"
           << "        .target = target,\n"
           << "    });\n"
           << "    module.addObjectFile(b.path(\"" << staticLibraryPath(packageInterface)
           << "\"));\n"
           << "    module.link_libc = true;\n";
    for (const auto &link : packageInterface.links) {
        output << "    module.linkSystemLibrary(\"" << link.name
               << "\", .{ .use_pkg_config = .no });\n";
    }
    output << "}\n";
    return output.str();
}

std::string renderZigManifest(const PackageInterface &packageInterface) {
    const auto checksum = crc32(snakeName(packageInterface.library));
    const auto id = packageFingerprintId(packageInterface);
    std::ostringstream output;
    output << ".{\n"
           << "    .name = ." << snakeName(packageInterface.library) << ",\n"
           << "    .version = \"" << packageInterface.version.string() << "\",\n"
           << "    .fingerprint = 0x" << std::hex << std::setfill('0') << std::setw(8) << checksum
           << std::setw(8) << id << std::dec << ",\n"
           << "    .minimum_zig_version = \"0.16.0\",\n"
           << "    .dependencies = .{},\n"
           << "    .paths = .{ \"build.zig\", \"build.zig.zon\", \"src\", \"native\", "
              "\"foundation.pii.json\" },\n"
           << "}\n";
    return output.str();
}

std::string renderRustSource(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "#[repr(C)]\n"
           << "#[derive(Clone, Copy)]\n"
           << "pub struct FoundationString {\n"
           << "    pub data: *const u8,\n"
           << "    pub length: usize,\n"
           << "    pub owned: u8,\n"
           << "}\n\n";
    for (const auto &layout : packageInterface.layouts) {
        output << "#[repr(C)]\n#[derive(Clone, Copy)]\npub struct "
               << typeName(layout.foundationName) << " {\n";
        for (const auto &field : layout.fields) {
            output << "    pub " << rustIdentifier(field.foundationName) << ": "
                   << *rustType(field.type) << ",\n";
        }
        output << "}\n\n";
    }
    output << "#[link(name = \"" << packageInterface.library
           << "\", kind = \"static\")]\nunsafe extern \"C\" {\n";
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        output << "    #[link_name = \"" << function.cSymbol << "\"]\n"
               << "    fn ffi_" << index << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << rustIdentifier(function.parameters[parameter].name) << ": "
                   << *rustType(function.parameters[parameter].type);
        }
        output << ") -> " << *rustType(function.result) << ";\n";
    }
    output << "}\n\n";
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        if (rawBoundary(function)) {
            output << "pub unsafe fn ";
        } else {
            output << "pub fn ";
        }
        output << rustIdentifier(shortName(function.foundationName)) << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << rustIdentifier(function.parameters[parameter].name) << ": "
                   << *rustType(function.parameters[parameter].type);
        }
        output << ") -> " << *rustType(function.result) << " {\n    unsafe { ffi_" << index << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << rustIdentifier(function.parameters[parameter].name);
        }
        output << ") }\n}\n";
        if (index + 1 != packageInterface.exports.size()) {
            output << '\n';
        }
    }
    return output.str();
}

std::string renderRustManifest(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "[package]\n"
           << "name = \"" << snakeName(packageInterface.library) << "\"\n"
           << "version = \"" << packageInterface.version.string() << "\"\n"
           << "edition = \"2024\"\n"
           << "links = \"" << packageInterface.library << "\"\n"
           << "build = \"build.rs\"\n\n"
           << "[lib]\npath = \"src/lib.rs\"\n";
    return output.str();
}

std::string renderRustBuild(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "fn main() {\n"
           << "    println!(\"cargo:rerun-if-changed=native\");\n"
           << "    println!(\"cargo:rustc-link-search=native={}/native/lib\", "
              "env!(\"CARGO_MANIFEST_DIR\"));\n"
           << "    println!(\"cargo:rustc-link-lib=static=" << packageInterface.library << "\");\n";
    for (const auto &link : packageInterface.links) {
        output << "    println!(\"cargo:rustc-link-lib=" << link.name << "\");\n";
    }
    output << "}\n";
    return output.str();
}

bool goUsesUnsafe(const PackageInterface &packageInterface) {
    if (!packageInterface.layouts.empty()) {
        return true;
    }
    const auto uses = [](const PiiType &type) {
        return type.kind == PiiTypeKind::String || type.kind == PiiTypeKind::Raw ||
               type.kind == PiiTypeKind::RawConst;
    };
    for (const auto &function : packageInterface.exports) {
        if (uses(function.result) ||
            std::any_of(function.parameters.begin(), function.parameters.end(),
                        [&](const auto &parameter) { return uses(parameter.type); })) {
            return true;
        }
    }
    return false;
}

bool goUsesString(const PackageInterface &packageInterface) {
    for (const auto &function : packageInterface.exports) {
        if (function.result.kind == PiiTypeKind::String ||
            std::any_of(
                function.parameters.begin(), function.parameters.end(),
                [](const auto &parameter) { return parameter.type.kind == PiiTypeKind::String; })) {
            return true;
        }
    }
    return false;
}

bool goUsesUnsafePointer(const PackageInterface &packageInterface) {
    const auto uses = [](const PiiType &type) {
        return (type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) &&
               type.arguments.size() == 1 && type.arguments.front().kind == PiiTypeKind::Void;
    };
    for (const auto &layout : packageInterface.layouts) {
        if (std::any_of(layout.fields.begin(), layout.fields.end(),
                        [&](const auto &field) { return uses(field.type); })) {
            return true;
        }
    }
    for (const auto &function : packageInterface.exports) {
        if (uses(function.result) ||
            std::any_of(function.parameters.begin(), function.parameters.end(),
                        [&](const auto &parameter) { return uses(parameter.type); })) {
            return true;
        }
    }
    return false;
}

bool goReturnsString(const PackageInterface &packageInterface) {
    return std::any_of(
        packageInterface.exports.begin(), packageInterface.exports.end(),
        [](const auto &function) { return function.result.kind == PiiTypeKind::String; });
}

bool functionUsesStringParameter(const PiiFunction &function) {
    return std::any_of(
        function.parameters.begin(), function.parameters.end(),
        [](const auto &parameter) { return parameter.type.kind == PiiTypeKind::String; });
}

std::string renderGoTypes(const PackageInterface &packageInterface,
                          const GoCallbackTypes &callbacks) {
    std::ostringstream output;
    std::size_t callbackIndex{};
    for (const auto &[key, type] : callbacks) {
        static_cast<void>(key);
        static_cast<void>(type);
        output << "type Callback" << callbackIndex++ << " uintptr\n";
    }
    if (!callbacks.empty()) {
        output << '\n';
    }
    for (const auto &layout : packageInterface.layouts) {
        output << "type " << typeName(layout.foundationName) << " struct {\n";
        std::size_t fieldWidth{};
        for (const auto &field : layout.fields) {
            fieldWidth = std::max(fieldWidth, typeName(field.foundationName).size());
        }
        for (const auto &field : layout.fields) {
            const auto name = typeName(field.foundationName);
            output << "\t" << name << std::string(fieldWidth - name.size() + 1, ' ')
                   << *goType(field.type, callbacks, false, true) << "\n";
        }
        output << "}\n\n";
    }
    return output.str();
}

std::string renderCgoPreamble(const PackageInterface &packageInterface,
                              const GoCallbackTypes &callbacks) {
    std::ostringstream output;
    output << "/*\n"
           << "#cgo CFLAGS: -I${SRCDIR}/native/include\n"
           << "#cgo LDFLAGS: " << cgoLinkFlags(packageInterface) << "\n"
           << "#include <stddef.h>\n"
           << "#include <stdint.h>\n"
           << "#include \"" << packageInterface.library << ".h\"\n\n"
           << cGoCallHelpers(packageInterface, callbacks);
    for (std::size_t layoutIndex{}; layoutIndex < packageInterface.layouts.size(); ++layoutIndex) {
        const auto &layout = packageInterface.layouts[layoutIndex];
        output << "enum { fdn_go_size_" << layoutIndex << " = sizeof(" << layout.cName << ") };\n";
        for (std::size_t fieldIndex{}; fieldIndex < layout.fields.size(); ++fieldIndex) {
            output << "enum { fdn_go_offset_" << layoutIndex << '_' << fieldIndex << " = offsetof("
                   << layout.cName << ", " << layout.fields[fieldIndex].cName << ") };\n";
        }
    }
    output << "*/\nimport \"C\"\n";
    return output.str();
}

void renderGoImports(std::ostringstream &output, bool runtime, bool unsafe, bool dynamic) {
    std::vector<std::string> imports;
    if (dynamic) {
        imports.push_back("\"fmt\"");
    }
    if (runtime) {
        imports.push_back("\"runtime\"");
    }
    if (unsafe) {
        imports.push_back("\"unsafe\"");
    }
    if (dynamic) {
        imports.push_back("\"github.com/ebitengine/purego\"");
    }
    if (imports.empty()) {
        return;
    }
    if (imports.size() == 1) {
        output << "\nimport " << imports.front() << "\n";
        return;
    }
    output << "\nimport (\n";
    for (const auto &import : imports) {
        output << "\t" << import << "\n";
    }
    output << ")\n";
}

std::string renderCgoSource(const PackageInterface &packageInterface) {
    const auto callbacks = goCallbackTypes(packageInterface);
    const auto usesString = goUsesString(packageInterface);
    const auto usesUnsafe = goUsesUnsafe(packageInterface);
    std::ostringstream output;
    output << "package " << goPackageName(packageInterface) << "\n\n"
           << renderCgoPreamble(packageInterface, callbacks);
    renderGoImports(output, usesString, usesUnsafe, false);
    output << '\n' << renderGoTypes(packageInterface, callbacks);
    for (std::size_t layoutIndex{}; layoutIndex < packageInterface.layouts.size(); ++layoutIndex) {
        const auto &layout = packageInterface.layouts[layoutIndex];
        const auto goLayoutName = typeName(layout.foundationName);
        output << "const _ = uint8(unsafe.Sizeof(" << goLayoutName << "{}) - C.fdn_go_size_"
               << layoutIndex << ")\n"
               << "const _ = uint8(C.fdn_go_size_" << layoutIndex << " - unsafe.Sizeof("
               << goLayoutName << "{}))\n";
        for (std::size_t fieldIndex{}; fieldIndex < layout.fields.size(); ++fieldIndex) {
            const auto goFieldName = typeName(layout.fields[fieldIndex].foundationName);
            output << "const _ = uint8(unsafe.Offsetof(" << goLayoutName << "{}." << goFieldName
                   << ") - C.fdn_go_offset_" << layoutIndex << '_' << fieldIndex << ")\n"
                   << "const _ = uint8(C.fdn_go_offset_" << layoutIndex << '_' << fieldIndex
                   << " - unsafe.Offsetof(" << goLayoutName << "{}." << goFieldName << "))\n";
        }
        output << '\n';
    }
    if (usesString) {
        output << "func foundationBorrowString(value string) C.fdn_string {\n"
               << "\tvar data *C.char\n"
               << "\tif len(value) != 0 {\n"
               << "\t\tdata = (*C.char)(unsafe.Pointer(unsafe.StringData(value)))\n"
               << "\t}\n"
               << "\treturn C.fdn_string_static(data, C.size_t(len(value)))\n"
               << "}\n\n";
    }
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        output << "func " << typeName(shortName(function.foundationName)) << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << camelName(function.parameters[parameter].name) << ' '
                   << *goType(function.parameters[parameter].type, callbacks, false);
        }
        output << ')';
        const auto publicResult = goType(function.result, callbacks, false);
        if (!publicResult->empty()) {
            output << ' ' << *publicResult;
        }
        output << " {\n";
        const auto storesResult = function.result.kind != PiiTypeKind::Void;
        output << '\t';
        if (storesResult) {
            output << "result := ";
        } else if (function.result.kind != PiiTypeKind::Void) {
            output << "return ";
        }
        output << "C.fdn_go_call_" << index << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << cgoArgument(function.parameters[parameter], packageInterface, callbacks);
        }
        output << ")\n";
        for (const auto &parameter : function.parameters) {
            if (parameter.type.kind == PiiTypeKind::String) {
                output << "\truntime.KeepAlive(" << camelName(parameter.name) << ")\n";
            }
        }
        if (function.result.kind == PiiTypeKind::String) {
            output << "\tvalue := C.GoStringN(result.data, C.int(result.length))\n"
                   << "\tC.fdn_string_drop(&result)\n"
                   << "\treturn value\n";
        } else if (storesResult) {
            output << "\treturn " << cgoResultExpression(function.result, "result") << "\n";
        }
        output << "}\n";
        if (index + 1 != packageInterface.exports.size()) {
            output << '\n';
        }
    }
    return output.str();
}

std::string renderGoModule(const PackageInterface &packageInterface, bool dynamic) {
    std::ostringstream output;
    output << "module " << goModulePath(packageInterface) << "\n\ngo 1.24.0\n";
    if (dynamic) {
        output << "\nrequire github.com/ebitengine/purego v0.10.2\n";
    }
    return output.str();
}

std::string renderGoSum() {
    return "github.com/ebitengine/purego v0.10.2 "
           "h1:W809HbnvzAxgdm+aOvlSekrM16wGCdT/e76+9tS7gzE=\n"
           "github.com/ebitengine/purego v0.10.2/go.mod "
           "h1:iIjxzd6CiRiOG0UyXP+V1+jWqUXVjPKLAI0mRfJZTmQ=\n";
}

std::string dynamicAbiType(const PiiType &type, const GoCallbackTypes &callbacks) {
    if (type.kind == PiiTypeKind::String) {
        return "foundationString";
    }
    return *goType(type, callbacks, true);
}

std::string dynamicArgument(const PiiParameter &parameter) {
    const auto name = camelName(parameter.name);
    if (parameter.type.kind == PiiTypeKind::String) {
        return "foundationBorrowString(" + name + ')';
    }
    return name;
}

std::string renderGoDynamicSource(const PackageInterface &packageInterface) {
    const auto callbacks = goCallbackTypes(packageInterface);
    const auto usesString = goUsesString(packageInterface);
    std::ostringstream output;
    output << "package " << goPackageName(packageInterface) << "\n";
    renderGoImports(output, usesString, usesString || goUsesUnsafePointer(packageInterface), true);
    output << '\n';
    if (usesString) {
        output << "type foundationString struct {\n"
               << "\tData *byte\n"
               << "\tLength uint\n"
               << "\tOwned uint8\n"
               << "}\n\n";
    }
    output << renderGoTypes(packageInterface, callbacks) << "type Library struct {\n";
    auto libraryFieldWidth = std::string_view{"handle"}.size();
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        libraryFieldWidth = std::max(libraryFieldWidth, ("call" + std::to_string(index)).size());
    }
    const auto handleField = std::string_view{"handle"};
    output << '\t' << handleField << std::string(libraryFieldWidth - handleField.size() + 1, ' ')
           << "uintptr\n";
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        const auto field = "call" + std::to_string(index);
        output << '\t' << field << std::string(libraryFieldWidth - field.size() + 1, ' ')
               << "func(";
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << dynamicAbiType(function.parameters[parameter].type, callbacks);
        }
        output << ')';
        const auto result = dynamicAbiType(function.result, callbacks);
        if (!result.empty()) {
            output << ' ' << result;
        }
        output << "\n";
    }
    if (goReturnsString(packageInterface)) {
        output << "\tstringDrop func(*foundationString)\n";
    }
    output << "}\n\n"
           << "func Open(path string) (*Library, error) {\n"
           << "\thandle, err := purego.Dlopen(path, purego.RTLD_NOW|purego.RTLD_LOCAL)\n"
           << "\tif err != nil {\n"
           << "\t\treturn nil, err\n"
           << "\t}\n"
           << "\tlibrary := &Library{handle: handle}\n";
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        output << "\tif err := bind(&library.call" << index << ", handle, \""
               << packageInterface.exports[index].cSymbol << "\"); err != nil {\n"
               << "\t\t_ = purego.Dlclose(handle)\n"
               << "\t\treturn nil, err\n"
               << "\t}\n";
    }
    if (goReturnsString(packageInterface)) {
        output
            << "\tif err := bind(&library.stringDrop, handle, \"fdn_string_drop\"); err != nil {\n"
            << "\t\t_ = purego.Dlclose(handle)\n"
            << "\t\treturn nil, err\n"
            << "\t}\n";
    }
    output << "\treturn library, nil\n"
           << "}\n\n"
           << "func bind(target any, handle uintptr, symbol string) error {\n"
           << "\taddress, err := purego.Dlsym(handle, symbol)\n"
           << "\tif err != nil {\n"
           << "\t\treturn fmt.Errorf(\"foundation symbol %s: %w\", symbol, err)\n"
           << "\t}\n"
           << "\tpurego.RegisterFunc(target, address)\n"
           << "\treturn nil\n"
           << "}\n\n"
           << "func (library *Library) Close() error {\n"
           << "\tif library.handle == 0 {\n"
           << "\t\treturn nil\n"
           << "\t}\n"
           << "\terr := purego.Dlclose(library.handle)\n"
           << "\tif err == nil {\n"
           << "\t\tlibrary.handle = 0\n"
           << "\t}\n"
           << "\treturn err\n"
           << "}\n\n"
           << "func (library *Library) requireOpen() {\n"
           << "\tif library.handle == 0 {\n"
           << "\t\tpanic(\"foundation library is closed\")\n"
           << "\t}\n"
           << "}\n";
    if (usesString) {
        output << "\nfunc foundationBorrowString(value string) foundationString {\n"
               << "\tvar data *byte\n"
               << "\tif len(value) != 0 {\n"
               << "\t\tdata = unsafe.StringData(value)\n"
               << "\t}\n"
               << "\treturn foundationString{Data: data, Length: uint(len(value))}\n"
               << "}\n";
    }
    for (std::size_t index{}; index < packageInterface.exports.size(); ++index) {
        const auto &function = packageInterface.exports[index];
        output << "\nfunc (library *Library) " << typeName(shortName(function.foundationName))
               << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << camelName(function.parameters[parameter].name) << ' '
                   << *goType(function.parameters[parameter].type, callbacks, true);
        }
        output << ')';
        const auto publicResult = goType(function.result, callbacks, true);
        if (!publicResult->empty()) {
            output << ' ' << *publicResult;
        }
        output << " {\n\tlibrary.requireOpen()\n\t";
        const auto stringParameter = functionUsesStringParameter(function);
        const auto storesResult = function.result.kind != PiiTypeKind::Void &&
                                  (stringParameter || function.result.kind == PiiTypeKind::String);
        if (storesResult) {
            output << "result := ";
        } else if (function.result.kind != PiiTypeKind::Void) {
            output << "return ";
        }
        output << "library.call" << index << '(';
        for (std::size_t parameter{}; parameter < function.parameters.size(); ++parameter) {
            if (parameter != 0) {
                output << ", ";
            }
            output << dynamicArgument(function.parameters[parameter]);
        }
        output << ")\n";
        for (const auto &parameter : function.parameters) {
            if (parameter.type.kind == PiiTypeKind::String) {
                output << "\truntime.KeepAlive(" << camelName(parameter.name) << ")\n";
            }
        }
        if (function.result.kind == PiiTypeKind::String) {
            output << "\tvalue := unsafe.String(result.Data, result.Length)\n"
                   << "\tcopy := string([]byte(value))\n"
                   << "\tlibrary.stringDrop(&result)\n"
                   << "\treturn copy\n";
        } else if (storesResult) {
            output << "\treturn result\n";
        }
        output << "}\n";
    }
    return output.str();
}

class GoSourceEmitter {
  public:
    GoSourceEmitter(const FirProgram &program, const PackageInterface &packageInterface,
                    Diagnostics &diagnostics)
        : program_(program), packageInterface_(packageInterface), diagnostics_(diagnostics),
          states_(program.functions.size()) {}

    std::optional<std::string> emit() {
        if (!packageInterface_.imports.empty()) {
            const auto &imported = packageInterface_.imports.front();
            fail("go-source cannot translate external function " + imported.foundationName,
                 sourceSpan(imported));
        }
        if (!packageInterface_.foreign.empty()) {
            fail("go-source cannot translate foreign package metadata", {0, 0, 1, 1});
        }
        if (!packageInterface_.links.empty()) {
            fail("go-source cannot translate native link requirements", {0, 0, 1, 1});
        }
        if (failed_) {
            return std::nullopt;
        }

        std::vector<FirFunctionId> roots;
        for (FirFunctionId id{}; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            if (!function.hasBody || !function.exported ||
                function.packageName != packageInterface_.package) {
                continue;
            }
            prepareFunctionName(id);
            roots.push_back(id);
        }
        if (roots.empty()) {
            fail("go-source package exports no Foundation functions", {0, 0, 1, 1});
        }
        for (const auto function : roots) {
            scanFunction(function);
        }
        if (failed_) {
            return std::nullopt;
        }

        std::ostringstream functions;
        for (const auto function : order_) {
            renderFunction(functions, function);
            if (failed_) {
                return std::nullopt;
            }
        }

        std::ostringstream output;
        output << "package " << goPackageName(packageInterface_) << "\n\n";
        renderStructs(output);
        if (!reachableStructs_.empty() && !reachableEnums_.empty()) {
            output << '\n';
        }
        renderEnums(output);
        if ((!reachableStructs_.empty() || !reachableEnums_.empty()) && !order_.empty()) {
            output << '\n';
        }
        output << functions.str();
        if (!helpers_.empty()) {
            if (!order_.empty()) {
                output << '\n';
            }
            auto first = true;
            for (const auto &[key, helper] : helpers_) {
                static_cast<void>(key);
                if (!first) {
                    output << '\n';
                }
                first = false;
                output << helper;
            }
        }
        return output.str();
    }

  private:
    static SourceSpan sourceSpan(const PiiFunction &function) {
        if (!function.source.has_value()) {
            return {0, 0, 1, 1};
        }
        return {function.source->offset, function.source->length, function.source->line,
                function.source->column};
    }

    void fail(std::string message, SourceSpan span) {
        if (failed_) {
            return;
        }
        diagnostics_.error(
            "FDN4120", std::move(message) + "; use go-cgo or go-dynamic for this boundary", span);
        failed_ = true;
    }

    std::string uniqueName(std::string base) {
        if (base.empty()) {
            base = "foundationFunction";
        }
        auto candidate = base;
        for (std::size_t suffix = 2; usedNames_.contains(candidate); ++suffix) {
            candidate = base + std::to_string(suffix);
        }
        usedNames_.insert(candidate);
        return candidate;
    }

    void prepareFunctionName(FirFunctionId id) {
        if (names_.contains(id)) {
            return;
        }
        const auto &function = program_.functions[id];
        const auto member = shortName(function.name);
        if (function.receiver.has_value()) {
            names_[id] = function.exported ? typeName(member) : goIdentifier(member);
            return;
        }
        if (function.constructor) {
            auto owner = typeName(memberOwnerName(function.name));
            auto constructed = function.returnType;
            if (constructed.kind == TypeKind::Enum &&
                constructed.declaration < program_.enums.size() &&
                program_.enums[constructed.declaration].builtin &&
                program_.enums[constructed.declaration].name == "Result" &&
                !constructed.arguments.empty()) {
                constructed = constructed.arguments.front();
            }
            if (constructed.kind == TypeKind::Struct &&
                constructed.declaration < program_.structs.size()) {
                owner = structName(constructed);
            }
            const auto constructor = typeName(member);
            auto base = "New" + owner;
            if (constructor != "New") {
                base += constructor;
            }
            if (!function.exported && !base.empty()) {
                base.front() =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(base.front())));
            }
            names_[id] = uniqueName(std::move(base));
            return;
        }
        if (function.method) {
            auto base = typeName(memberOwnerName(function.name)) + typeName(member);
            if (!function.exported && !base.empty()) {
                base.front() =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(base.front())));
            }
            names_[id] = uniqueName(std::move(base));
            return;
        }
        names_[id] = uniqueName(function.exported ? typeName(member) : goIdentifier(member));
    }

    std::string structName(const Type &type) {
        const auto key = goSourceTypeKey(type);
        if (!structNames_.contains(key)) {
            auto base = typeName(shortName(program_.structs[type.declaration].name));
            for (const auto &argument : type.arguments) {
                base += enumTypeLabel(argument);
            }
            structNames_[key] = uniqueName(std::move(base));
        }
        return structNames_.at(key);
    }

    void prepareStructFields(FirStructId id) {
        if (preparedStructFields_.contains(id)) {
            return;
        }
        preparedStructFields_.insert(id);
        std::set<std::string> used;
        const auto &declaration = program_.structs[id];
        for (FirFieldId field{}; field < declaration.fields.size(); ++field) {
            const auto &source = declaration.fields[field];
            auto base = source.exported ? typeName(source.name) : goIdentifier(source.name);
            if (base.empty()) {
                base = source.exported ? "Field" : "field";
            }
            auto candidate = base;
            for (std::size_t suffix = 2; used.contains(candidate); ++suffix) {
                candidate = base + std::to_string(suffix);
            }
            used.insert(candidate);
            structFieldNames_[{id, field}] = std::move(candidate);
        }
    }

    std::string fieldName(FirStructId declaration, FirFieldId field) {
        prepareStructFields(declaration);
        return structFieldNames_.at({declaration, field});
    }

    std::string enumTypeLabel(Type type) {
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        if (type == stringType) {
            return "String";
        }
        if (const auto tag = goSourceTypeTag(type); !tag.empty()) {
            return tag;
        }
        if (type == f32Type) {
            return "F32";
        }
        if (type == f64Type) {
            return "F64";
        }
        if (type == boolType) {
            return "Bool";
        }
        if (type == voidType) {
            return "Void";
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return "Array" + std::to_string(type.declaration) +
                   enumTypeLabel(type.arguments.front());
        }
        if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
            return "Slice" + enumTypeLabel(type.arguments.front());
        }
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            return structName(type);
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            return enumName(type);
        }
        return "Value";
    }

    std::string enumName(const Type &type) {
        const auto key = goSourceTypeKey(type);
        if (!enumNames_.contains(key)) {
            auto base = typeName(shortName(program_.enums[type.declaration].name));
            for (const auto &argument : type.arguments) {
                base += enumTypeLabel(argument);
            }
            enumNames_[key] = uniqueName(std::move(base));
        }
        return enumNames_.at(key);
    }

    void prepareEnum(const Type &type) {
        const auto key = goSourceTypeKey(type);
        if (preparedEnums_.contains(key)) {
            return;
        }
        preparedEnums_.insert(key);
        const auto &declaration = program_.enums[type.declaration];
        const auto generatedType = enumName(type);
        std::set<std::string> fields{"tag"};
        std::set<std::string> methods;
        for (FirVariantId variant{}; variant < declaration.variants.size(); ++variant) {
            const auto &source = declaration.variants[variant];
            auto field = goIdentifier(source.name);
            auto fieldCandidate = field;
            for (std::size_t suffix = 2; fields.contains(fieldCandidate); ++suffix) {
                fieldCandidate = field + std::to_string(suffix);
            }
            fields.insert(fieldCandidate);
            enumFieldNames_[{key, variant}] = std::move(fieldCandidate);

            auto method = typeName(source.name);
            auto methodCandidate = method;
            for (std::size_t suffix = 2; methods.contains(methodCandidate); ++suffix) {
                methodCandidate = method + std::to_string(suffix);
            }
            methods.insert(methodCandidate);
            enumMethodNames_[{key, variant}] = std::move(methodCandidate);
            enumConstructorNames_[{key, variant}] =
                uniqueName("New" + generatedType + typeName(source.name));
        }
    }

    std::string enumFieldName(const Type &type, FirVariantId variant) {
        prepareEnum(type);
        return enumFieldNames_.at({goSourceTypeKey(type), variant});
    }

    std::string enumMethodName(const Type &type, FirVariantId variant) {
        prepareEnum(type);
        return enumMethodNames_.at({goSourceTypeKey(type), variant});
    }

    std::string enumConstructorName(const Type &type, FirVariantId variant) {
        prepareEnum(type);
        return enumConstructorNames_.at({goSourceTypeKey(type), variant});
    }

    std::optional<Type> enumPayloadType(const Type &type, FirVariantId variant) const {
        if (type.kind != TypeKind::Enum || type.declaration >= program_.enums.size()) {
            return std::nullopt;
        }
        const auto &declaration = program_.enums[type.declaration];
        if (variant >= declaration.variants.size() ||
            !declaration.variants[variant].payload.has_value()) {
            return std::nullopt;
        }
        const auto payload =
            substituteGoSourceType(*declaration.variants[variant].payload, type.arguments);
        if (payload == voidType && declaration.name == "Result") {
            return std::nullopt;
        }
        return payload;
    }

    bool builtinEnum(const Type &type, std::string_view name) const {
        return type.kind == TypeKind::Enum && type.declaration < program_.enums.size() &&
               program_.enums[type.declaration].builtin &&
               program_.enums[type.declaration].name == name;
    }

    static bool pointerParameter(const Type &type) {
        return type.kind == TypeKind::Edit && type.arguments.size() == 1 &&
               type.arguments.front().kind != TypeKind::Slice;
    }

    std::optional<std::string> sourceParameterType(Type type) {
        if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
             type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1) {
            const auto pointer = pointerParameter(type);
            type = type.arguments.front();
            const auto rendered = sourceType(type);
            if (!rendered.has_value() || rendered->empty()) {
                return std::nullopt;
            }
            return pointer ? std::optional<std::string>{'*' + *rendered} : rendered;
        }
        return sourceType(type);
    }

    std::optional<std::string> sourceType(Type type) {
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        if (const auto scalar = goSourceType(type); scalar.has_value()) {
            return scalar;
        }
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            return structName(type);
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            return enumName(type);
        }
        if ((type.kind == TypeKind::Array || type.kind == TypeKind::Slice) &&
            type.arguments.size() == 1) {
            const auto element = sourceType(type.arguments.front());
            if (!element.has_value() || element->empty()) {
                return std::nullopt;
            }
            if (type.kind == TypeKind::Array) {
                return '[' + std::to_string(type.declaration) + ']' + *element;
            }
            return "[]" + *element;
        }
        if (type.kind == TypeKind::Function && !isCFunction(type) && !type.arguments.empty()) {
            std::ostringstream output;
            output << "func(";
            for (std::size_t index = 1; index < type.arguments.size(); ++index) {
                const auto parameter = sourceParameterType(type.arguments[index]);
                if (!parameter.has_value() || parameter->empty()) {
                    return std::nullopt;
                }
                if (index != 1) {
                    output << ", ";
                }
                output << *parameter;
            }
            output << ')';
            const auto result = sourceType(type.arguments.front());
            if (!result.has_value()) {
                return std::nullopt;
            }
            if (!result->empty()) {
                output << ' ' << *result;
            }
            return output.str();
        }
        return std::nullopt;
    }

    bool scanType(Type type, SourceSpan span) {
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        if (goSourceType(type).has_value()) {
            return true;
        }
        if (type.kind == TypeKind::Array || type.kind == TypeKind::Slice) {
            if (type.arguments.size() != 1) {
                fail("go-source reached an incomplete sequence type", span);
                return false;
            }
            return scanType(type.arguments.front(), span);
        }
        if (type.kind == TypeKind::Function) {
            if (isCFunction(type) || type.arguments.empty()) {
                fail("go-source cannot translate this function type", span);
                return false;
            }
            for (std::size_t index{}; index < type.arguments.size(); ++index) {
                auto argument = type.arguments[index];
                if (index != 0 &&
                    (argument.kind == TypeKind::Own || argument.kind == TypeKind::View ||
                     argument.kind == TypeKind::Edit) &&
                    argument.arguments.size() == 1) {
                    argument = argument.arguments.front();
                }
                if (!scanType(argument, span)) {
                    return false;
                }
            }
            return true;
        }
        if (type.kind == TypeKind::Enum) {
            if (type.declaration >= program_.enums.size()) {
                fail("go-source reached an invalid enum type", span);
                return false;
            }
            const auto &declaration = program_.enums[type.declaration];
            if (type.arguments.size() != declaration.typeParameterCount) {
                fail("go-source reached an incomplete enum type", span);
                return false;
            }
            const auto key = goSourceTypeKey(type);
            if (enumStates_[key] == 2) {
                return true;
            }
            if (enumStates_[key] == 1) {
                fail("go-source cannot translate recursive value enums", span);
                return false;
            }
            enumStates_[key] = 1;
            enumName(type);
            prepareEnum(type);
            for (FirVariantId variant{}; variant < declaration.variants.size(); ++variant) {
                const auto payload = enumPayloadType(type, variant);
                if (payload.has_value() && !scanType(*payload, span)) {
                    return false;
                }
            }
            enumStates_[key] = 2;
            reachableEnums_[key] = type;
            return true;
        }
        if (type.kind != TypeKind::Struct || type.declaration >= program_.structs.size()) {
            fail("go-source reached an unsupported type", span);
            return false;
        }
        const auto id = type.declaration;
        const auto &declaration = program_.structs[id];
        if (type.arguments.size() != declaration.typeParameterCount) {
            fail("go-source reached an incomplete struct type", span);
            return false;
        }
        const auto key = goSourceTypeKey(type);
        if (structStates_[key] == 2) {
            return true;
        }
        if (structStates_[key] == 1) {
            fail("go-source cannot translate recursive value structs", span);
            return false;
        }
        if (declaration.dropFunction.has_value() || declaration.service) {
            fail("go-source cannot translate struct " + declaration.name, span);
            return false;
        }
        structStates_[key] = 1;
        structName(type);
        prepareStructFields(id);
        for (const auto &field : declaration.fields) {
            if (!scanType(substituteGoSourceType(field.type, type.arguments),
                          declaration.sourceSpan)) {
                return false;
            }
        }
        structStates_[key] = 2;
        reachableStructs_[key] = type;
        return true;
    }

    bool validFunction(const FirFunction &function, bool closure = false) {
        if (!function.hasBody || function.packageName != packageInterface_.package) {
            fail("go-source reached a function without a same-package body", function.sourceSpan);
            return false;
        }
        if (function.typeParameterCount != 0 || function.closure != closure || function.task ||
            function.blocking || function.callback || function.action ||
            function.stateTransition.has_value() || function.workflow.has_value() ||
            function.stateTimeout.has_value() || !function.attributes.empty()) {
            fail("go-source reached unsupported function " + function.name, function.sourceSpan);
            return false;
        }
        if (function.constructor && function.receiver.has_value()) {
            fail("go-source reached a constructor with a receiver", function.sourceSpan);
            return false;
        }
        if (function.receiver.has_value()) {
            if (!function.method || function.constructor || function.parameters.empty()) {
                fail("go-source found invalid method receiver metadata", function.sourceSpan);
                return false;
            }
            if (*function.receiver == FirReceiverKind::Own) {
                fail("go-source cannot preserve a consuming method receiver", function.sourceSpan);
                return false;
            }
            const auto receiver = function.parameters.front();
            if (receiver >= function.locals.size()) {
                fail("go-source found an invalid method receiver", function.sourceSpan);
                return false;
            }
            auto receiverType = function.locals[receiver].type;
            if ((receiverType.kind == TypeKind::View || receiverType.kind == TypeKind::Edit) &&
                receiverType.arguments.size() == 1) {
                receiverType = receiverType.arguments.front();
            }
            if (receiverType.kind != TypeKind::Struct && receiverType.kind != TypeKind::Enum) {
                fail("go-source requires a nominal method receiver", function.sourceSpan);
                return false;
            }
        }
        if (!scanType(function.returnType, function.sourceSpan)) {
            fail("go-source cannot translate result type of " + function.name, function.sourceSpan);
            return false;
        }
        for (const auto &local : function.locals) {
            if (!scanType(local.type, function.sourceSpan) || local.type == voidType) {
                fail("go-source cannot translate local " + local.name + " in " + function.name,
                     function.sourceSpan);
                return false;
            }
        }
        for (const auto parameter : function.parameters) {
            if (parameter >= function.locals.size()) {
                fail("go-source found invalid parameter metadata in " + function.name,
                     function.sourceSpan);
                return false;
            }
        }
        return true;
    }

    bool scanFunction(FirFunctionId id) {
        if (id >= program_.functions.size()) {
            fail("go-source reached an invalid function", {0, 0, 1, 1});
            return false;
        }
        if (states_[id] == 2) {
            return true;
        }
        if (states_[id] == 1) {
            return true;
        }
        states_[id] = 1;
        const auto &function = program_.functions[id];
        if (!validFunction(function)) {
            return false;
        }
        prepareFunctionName(id);
        if (!scanBlock(function, function.body)) {
            return false;
        }
        states_[id] = 2;
        order_.push_back(id);
        return true;
    }

    bool scanClosure(const FirFunction &outer, const FirClosureExpression &closure,
                     SourceSpan span) {
        if (closure.function >= program_.functions.size()) {
            fail("go-source reached an invalid closure", span);
            return false;
        }
        const auto &target = program_.functions[closure.function];
        std::vector<FirLocalId> captureLocals;
        for (FirLocalId local{}; local < target.locals.size(); ++local) {
            if (target.locals[local].capture) {
                captureLocals.push_back(local);
            }
        }
        if (captureLocals.size() != closure.captures.size()) {
            fail("go-source found invalid closure capture metadata", span);
            return false;
        }
        for (std::size_t index{}; index < closure.captures.size(); ++index) {
            const auto &capture = closure.captures[index];
            if (capture.local >= outer.locals.size() ||
                !scanType(outer.locals[capture.local].type, span) ||
                !scanType(target.locals[captureLocals[index]].type, span)) {
                if (!failed_) {
                    fail("go-source cannot translate a closure capture", span);
                }
                return false;
            }
        }
        if (states_[closure.function] == 2) {
            return true;
        }
        if (states_[closure.function] == 1) {
            return true;
        }
        states_[closure.function] = 1;
        if (!validFunction(target, true) || !scanBlock(target, target.body)) {
            return false;
        }
        states_[closure.function] = 2;
        return true;
    }

    bool scanBlock(const FirFunction &function, FirBlockId id) {
        if (id >= function.blocks.size()) {
            fail("go-source found an invalid block in " + function.name, function.sourceSpan);
            return false;
        }
        for (const auto statement : function.blocks[id].statements) {
            if (statement >= function.statements.size() ||
                !scanStatement(function, function.statements[statement])) {
                if (!failed_) {
                    fail("go-source found an invalid statement in " + function.name,
                         function.sourceSpan);
                }
                return false;
            }
        }
        return true;
    }

    bool blockEscapesExpression(const FirFunction &function, FirBlockId id,
                                unsigned int loopDepth = 0) const {
        if (id >= function.blocks.size()) {
            return true;
        }
        for (const auto statementId : function.blocks[id].statements) {
            if (statementId >= function.statements.size()) {
                return true;
            }
            const auto &statement = function.statements[statementId].value;
            if (std::holds_alternative<FirReturnStatement>(statement)) {
                return true;
            }
            if ((std::holds_alternative<FirBreakStatement>(statement) ||
                 std::holds_alternative<FirContinueStatement>(statement)) &&
                loopDepth == 0) {
                return true;
            }
            if (const auto *branch = std::get_if<FirIfStatement>(&statement)) {
                if (blockEscapesExpression(function, branch->thenBlock, loopDepth) ||
                    (branch->elseBlock.has_value() &&
                     blockEscapesExpression(function, *branch->elseBlock, loopDepth))) {
                    return true;
                }
            }
            if (const auto *loop = std::get_if<FirWhileStatement>(&statement);
                loop != nullptr && blockEscapesExpression(function, loop->body, loopDepth + 1)) {
                return true;
            }
            if (const auto *loop = std::get_if<FirForStatement>(&statement);
                loop != nullptr && blockEscapesExpression(function, loop->body, loopDepth + 1)) {
                return true;
            }
        }
        return false;
    }

    bool scanStatement(const FirFunction &function, const FirStatement &statement) {
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            return variable->local < function.locals.size() &&
                   scanExpression(function, variable->initializer);
        }
        if (const auto *binding = std::get_if<FirLetElseStatement>(&statement.value)) {
            if (binding->local >= function.locals.size() ||
                binding->errorLocal >= function.locals.size() ||
                !scanExpression(function, binding->initializer) ||
                !scanBlock(function, binding->elseBlock)) {
                return false;
            }
            const auto type = function.expressions[binding->initializer].type;
            if (!builtinEnum(type, "Result")) {
                fail("go-source let-else requires Result", statement.span);
                return false;
            }
            return true;
        }
        if (const auto *binding = std::get_if<FirResultElseStatement>(&statement.value)) {
            if (binding->errorLocal >= function.locals.size() ||
                !scanExpression(function, binding->expression) ||
                !scanBlock(function, binding->elseBlock)) {
                return false;
            }
            const auto type = function.expressions[binding->expression].type;
            if (!builtinEnum(type, "Result")) {
                fail("go-source result-else requires Result", statement.span);
                return false;
            }
            return true;
        }
        if (const auto *destructure =
                std::get_if<FirStructDestructureStatement>(&statement.value)) {
            if (destructure->owned) {
                fail("go-source cannot preserve owner destructuring", statement.span);
                return false;
            }
            if (destructure->type.kind != TypeKind::Struct ||
                destructure->type.declaration >= program_.structs.size() ||
                !scanType(destructure->type, statement.span) ||
                !scanExpression(function, destructure->initializer) ||
                function.expressions[destructure->initializer].type != destructure->type) {
                if (!failed_) {
                    fail("go-source supports value-struct destructuring only", statement.span);
                }
                return false;
            }
            const auto &declaration = program_.structs[destructure->type.declaration];
            std::vector<bool> bound(declaration.fields.size());
            for (const auto &binding : destructure->bindings) {
                if (binding.local >= function.locals.size() ||
                    binding.field >= declaration.fields.size() || bound[binding.field] ||
                    function.locals[binding.local].type !=
                        substituteGoSourceType(declaration.fields[binding.field].type,
                                               destructure->type.arguments)) {
                    fail("go-source found invalid struct destructuring metadata", statement.span);
                    return false;
                }
                bound[binding.field] = true;
            }
            if (std::ranges::find(bound, false) != bound.end()) {
                fail("go-source requires complete struct destructuring", statement.span);
                return false;
            }
            return true;
        }
        if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            if (!scanExpression(function, assignment->target) ||
                !scanExpression(function, assignment->value)) {
                return false;
            }
            const auto &target = function.expressions[assignment->target].value;
            if (!std::holds_alternative<FirLocalExpression>(target) &&
                !std::holds_alternative<FirReadExpression>(target) &&
                !std::holds_alternative<FirMoveExpression>(target) &&
                !std::holds_alternative<FirFieldExpression>(target) &&
                !std::holds_alternative<FirIndexExpression>(target)) {
                fail("go-source supports assignment to locals, fields, and sequence elements",
                     statement.span);
                return false;
            }
            if (assignment->operation != FirAssignmentOperator::Assign) {
                const auto type = function.expressions[assignment->target].type;
                const auto operation = assignmentBinary(assignment->operation);
                const auto stringAddition =
                    operation == FirBinaryOperator::Add && type == stringType;
                const auto shift = operation == FirBinaryOperator::ShiftLeft ||
                                   operation == FirBinaryOperator::ShiftRight;
                if (!stringAddition && (!isNumeric(type) || (shift && !isInteger(type)))) {
                    fail("go-source supports compound assignment for numeric values and String",
                         statement.span);
                    return false;
                }
                if (operation == FirBinaryOperator::Remainder && !isInteger(type)) {
                    fail("go-source requires an integer compound remainder target", statement.span);
                    return false;
                }
            }
            return true;
        }
        if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            return scanExpression(function, expression->expression);
        }
        if (const auto *discarded = std::get_if<FirDiscardStatement>(&statement.value)) {
            return scanExpression(function, discarded->expression);
        }
        if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
            return !returned->value.has_value() || scanExpression(function, *returned->value);
        }
        if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            return scanExpression(function, branch->condition) &&
                   scanBlock(function, branch->thenBlock) &&
                   (!branch->elseBlock.has_value() || scanBlock(function, *branch->elseBlock));
        }
        if (const auto *loop = std::get_if<FirWhileStatement>(&statement.value)) {
            return scanExpression(function, loop->condition) && scanBlock(function, loop->body);
        }
        if (const auto *loop = std::get_if<FirForStatement>(&statement.value)) {
            if (loop->sequenceStorage >= function.locals.size() ||
                loop->index >= function.locals.size() || loop->value >= function.locals.size() ||
                loop->next.has_value() || loop->ownsSequence ||
                !scanExpression(function, loop->sequence) || !scanBlock(function, loop->body)) {
                if (!failed_) {
                    fail("go-source supports for over arrays and slices only", statement.span);
                }
                return false;
            }
            auto type = function.expressions[loop->sequence].type;
            if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
                type.arguments.size() == 1) {
                type = type.arguments.front();
            }
            if ((type.kind != TypeKind::Array && type.kind != TypeKind::Slice) ||
                type.arguments.size() != 1) {
                fail("go-source supports for over arrays and slices only", statement.span);
                return false;
            }
            return true;
        }
        if (std::holds_alternative<FirBreakStatement>(statement.value) ||
            std::holds_alternative<FirContinueStatement>(statement.value)) {
            return true;
        }
        fail("go-source reached an unsupported statement", statement.span);
        return false;
    }

    bool scanExpression(const FirFunction &function, FirExpressionId id) {
        if (id >= function.expressions.size()) {
            fail("go-source found an invalid expression in " + function.name, function.sourceSpan);
            return false;
        }
        const auto &expression = function.expressions[id];
        if (!sourceType(expression.type).has_value()) {
            fail("go-source reached an unsupported expression type", expression.span);
            return false;
        }
        if (std::holds_alternative<FirIntegerExpression>(expression.value) ||
            std::holds_alternative<FirFloatingExpression>(expression.value) ||
            std::holds_alternative<FirBooleanExpression>(expression.value) ||
            std::holds_alternative<FirStringExpression>(expression.value)) {
            return true;
        }
        if (const auto *value = std::get_if<FirArrayExpression>(&expression.value)) {
            if (!scanType(expression.type, expression.span)) {
                return false;
            }
            for (const auto element : value->elements) {
                if (!scanExpression(function, element)) {
                    return false;
                }
            }
            return true;
        }
        const auto local = [&](FirLocalId value) {
            if (value >= function.locals.size()) {
                fail("go-source found an invalid local in " + function.name, expression.span);
                return false;
            }
            return true;
        };
        if (const auto *value = std::get_if<FirLocalExpression>(&expression.value)) {
            return local(value->local);
        }
        if (const auto *value = std::get_if<FirReadExpression>(&expression.value)) {
            return local(value->local);
        }
        if (const auto *value = std::get_if<FirMoveExpression>(&expression.value)) {
            return local(value->local);
        }
        if (const auto *value = std::get_if<FirFunctionValueExpression>(&expression.value)) {
            if (!value->typeArguments.empty()) {
                fail("go-source cannot translate a generic function value", expression.span);
                return false;
            }
            return scanFunction(value->function);
        }
        if (const auto *value = std::get_if<FirClosureExpression>(&expression.value)) {
            return scanClosure(function, *value, expression.span);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
            if (unary->operation != FirUnaryOperator::Negate &&
                unary->operation != FirUnaryOperator::Not &&
                unary->operation != FirUnaryOperator::Empty) {
                fail("go-source reached an unsupported unary operation", expression.span);
                return false;
            }
            if (unary->operation == FirUnaryOperator::Negate && !isSignedInteger(expression.type) &&
                !isFloating(expression.type)) {
                fail("go-source cannot negate this value", expression.span);
                return false;
            }
            if (!scanExpression(function, unary->operand)) {
                return false;
            }
            if (unary->operation == FirUnaryOperator::Empty) {
                auto type = function.expressions[unary->operand].type;
                if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
                    type.arguments.size() == 1) {
                    type = type.arguments.front();
                }
                if (type != stringType && type.kind != TypeKind::Array &&
                    type.kind != TypeKind::Slice) {
                    fail("go-source supports empty tests for String, arrays, and slices",
                         expression.span);
                    return false;
                }
            }
            return true;
        }
        if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
            return scanExpression(function, ownership->operand);
        }
        if (const auto *value = std::get_if<FirEnumExpression>(&expression.value)) {
            return scanType(value->type, expression.span) &&
                   (!value->payload.has_value() || scanExpression(function, *value->payload));
        }
        if (const auto *value = std::get_if<FirMatchExpression>(&expression.value)) {
            if (!scanType(value->type, expression.span) ||
                !scanExpression(function, value->value)) {
                return false;
            }
            for (const auto &arm : value->arms) {
                if (arm.pattern.has_value()) {
                    if (*arm.pattern >= function.expressions.size()) {
                        fail("go-source found an invalid match pattern", expression.span);
                        return false;
                    }
                    const auto &pattern = function.expressions[*arm.pattern].value;
                    if (!std::holds_alternative<FirIntegerExpression>(pattern) &&
                        !std::holds_alternative<FirFloatingExpression>(pattern) &&
                        !std::holds_alternative<FirBooleanExpression>(pattern) &&
                        !std::holds_alternative<FirStringExpression>(pattern)) {
                        fail("go-source match patterns must be literals", expression.span);
                        return false;
                    }
                    if (!scanExpression(function, *arm.pattern)) {
                        return false;
                    }
                }
                if ((arm.guard.has_value() && !scanExpression(function, *arm.guard)) ||
                    !scanBlock(function, arm.block) ||
                    (arm.expression.has_value() && !scanExpression(function, *arm.expression))) {
                    return false;
                }
                if (blockEscapesExpression(function, arm.block)) {
                    fail("go-source match arms cannot return or escape an outer loop",
                         expression.span);
                    return false;
                }
            }
            return true;
        }
        if (const auto *value = std::get_if<FirConditionalExpression>(&expression.value)) {
            if (!scanExpression(function, value->condition) ||
                function.expressions[value->condition].type != boolType ||
                !scanBlock(function, value->thenBlock) ||
                !scanExpression(function, value->thenValue) ||
                !scanBlock(function, value->elseBlock) ||
                !scanExpression(function, value->elseValue)) {
                if (!failed_) {
                    fail("go-source found an invalid conditional expression", expression.span);
                }
                return false;
            }
            if (blockEscapesExpression(function, value->thenBlock) ||
                blockEscapesExpression(function, value->elseBlock)) {
                fail("go-source conditional branches cannot return or escape an outer loop",
                     expression.span);
                return false;
            }
            return true;
        }
        if (const auto *value = std::get_if<FirStructExpression>(&expression.value)) {
            if (!scanType(value->type, expression.span)) {
                return false;
            }
            for (const auto &field : value->fields) {
                if (!scanExpression(function, field.value)) {
                    return false;
                }
            }
            return true;
        }
        if (const auto *value = std::get_if<FirFieldExpression>(&expression.value)) {
            return scanExpression(function, value->base);
        }
        if (const auto *value = std::get_if<FirIndexExpression>(&expression.value)) {
            if (!scanExpression(function, value->base) || !scanExpression(function, value->index)) {
                return false;
            }
            auto type = function.expressions[value->base].type;
            if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                 type.kind == TypeKind::Edit) &&
                type.arguments.size() == 1) {
                type = type.arguments.front();
            }
            if ((type.kind != TypeKind::Array && type.kind != TypeKind::Slice) ||
                type.arguments.size() != 1) {
                fail("go-source indexing requires an array or slice", expression.span);
                return false;
            }
            return true;
        }
        if (const auto *value = std::get_if<FirReplaceExpression>(&expression.value)) {
            if (!scanExpression(function, value->value) ||
                !scanExpression(function, value->target)) {
                return false;
            }
            if (!addressableExpression(function, value->target)) {
                fail("go-source cannot preserve this replace target", expression.span);
                return false;
            }
            return true;
        }
        if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
            if (!scanExpression(function, binary->left) ||
                !scanExpression(function, binary->right)) {
                return false;
            }
            const auto operandType = function.expressions[binary->left].type;
            const auto stringAddition =
                binary->operation == FirBinaryOperator::Add && operandType == stringType;
            if (!stringAddition &&
                (binary->operation == FirBinaryOperator::Add ||
                 binary->operation == FirBinaryOperator::Subtract ||
                 binary->operation == FirBinaryOperator::Multiply ||
                 binary->operation == FirBinaryOperator::Divide ||
                 binary->operation == FirBinaryOperator::Remainder ||
                 binary->operation == FirBinaryOperator::ShiftLeft ||
                 binary->operation == FirBinaryOperator::ShiftRight) &&
                !isNumeric(operandType)) {
                fail("go-source reached unsupported arithmetic", expression.span);
                return false;
            }
            if (binary->operation == FirBinaryOperator::Remainder && !isInteger(operandType)) {
                fail("go-source reached unsupported remainder arithmetic", expression.span);
                return false;
            }
            if ((binary->operation == FirBinaryOperator::ShiftLeft ||
                 binary->operation == FirBinaryOperator::ShiftRight) &&
                !isInteger(operandType)) {
                fail("go-source reached unsupported shift arithmetic", expression.span);
                return false;
            }
            return true;
        }
        if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
            if (call->kind == FirCallKind::Len) {
                if (call->arguments.size() != 1 || !call->typeArguments.empty()) {
                    fail("go-source supports len for String, arrays, and slices", expression.span);
                    return false;
                }
                if (!scanExpression(function, call->arguments.front())) {
                    return false;
                }
                auto type = function.expressions[call->arguments.front()].type;
                if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
                    type.arguments.size() == 1) {
                    type = type.arguments.front();
                }
                if (type != stringType && type.kind != TypeKind::Array &&
                    type.kind != TypeKind::Slice) {
                    fail("go-source supports len for String, arrays, and slices", expression.span);
                    return false;
                }
                return true;
            }
            if ((call->kind != FirCallKind::Function && call->kind != FirCallKind::FunctionValue) ||
                !call->typeArguments.empty()) {
                fail("go-source reached an unsupported call", expression.span);
                return false;
            }
            for (const auto argument : call->arguments) {
                if (!scanExpression(function, argument)) {
                    return false;
                }
            }
            const auto arguments = orderedArguments(*call, expression.span);
            if (!arguments.has_value()) {
                return false;
            }
            if (call->kind == FirCallKind::FunctionValue) {
                if (call->local >= function.locals.size()) {
                    fail("go-source found an invalid callable local", expression.span);
                    return false;
                }
                auto callable = function.locals[call->local].type;
                if ((callable.kind == TypeKind::View || callable.kind == TypeKind::Edit) &&
                    callable.arguments.size() == 1) {
                    callable = callable.arguments.front();
                }
                if (callable.kind != TypeKind::Function || isCFunction(callable) ||
                    callable.arguments.size() != arguments->size() + 1) {
                    fail("go-source found an invalid function value call", expression.span);
                    return false;
                }
                for (std::size_t index{}; index < arguments->size(); ++index) {
                    if (pointerParameter(callable.arguments[index + 1]) &&
                        !addressableExpression(function, (*arguments)[index])) {
                        fail("go-source cannot preserve an editable function argument",
                             expression.span);
                        return false;
                    }
                }
                return true;
            }
            if (!scanFunction(call->function)) {
                return false;
            }
            const auto &target = program_.functions[call->function];
            if (target.parameters.size() != arguments->size()) {
                fail("go-source found invalid function call metadata", expression.span);
                return false;
            }
            for (std::size_t index{}; index < arguments->size(); ++index) {
                const auto parameter = target.parameters[index];
                if (parameter >= target.locals.size()) {
                    fail("go-source found invalid function parameter metadata", expression.span);
                    return false;
                }
                if (pointerParameter(target.locals[parameter].type) &&
                    !addressableExpression(function, (*arguments)[index])) {
                    fail("go-source cannot preserve an editable function argument",
                         expression.span);
                    return false;
                }
            }
            return true;
        }
        fail("go-source reached an unsupported expression", expression.span);
        return false;
    }

    std::vector<std::string> localNames(const FirFunction &function) const {
        std::vector<std::string> result(function.locals.size());
        auto used = usedNames_;
        for (std::size_t id{}; id < function.locals.size(); ++id) {
            auto base = goIdentifier(function.locals[id].name);
            if (base.empty()) {
                base = "value";
            }
            if (base.starts_with("foundation")) {
                base += "Value";
            }
            auto candidate = base;
            for (std::size_t suffix = 2; used.contains(candidate); ++suffix) {
                candidate = base + std::to_string(suffix);
            }
            used.insert(candidate);
            result[id] = std::move(candidate);
        }
        return result;
    }

    std::string temporaryLocal(std::string_view purpose) {
        auto base = "foundation" + typeName(purpose);
        auto candidate = base;
        for (std::size_t suffix = 2; currentGeneratedLocals_.contains(candidate); ++suffix) {
            candidate = base + std::to_string(suffix);
        }
        currentGeneratedLocals_.insert(candidate);
        return candidate;
    }

    std::optional<std::vector<FirExpressionId>> orderedArguments(const FirCallExpression &call,
                                                                 SourceSpan span) {
        if (call.argumentParameters.empty()) {
            return call.arguments;
        }
        if (call.argumentParameters.size() != call.arguments.size()) {
            fail("go-source found invalid call argument metadata", span);
            return std::nullopt;
        }
        std::vector<FirExpressionId> result(call.arguments.size(),
                                            std::numeric_limits<FirExpressionId>::max());
        for (std::size_t source{}; source < call.arguments.size(); ++source) {
            const auto target = call.argumentParameters[source];
            if (target >= result.size() ||
                result[target] != std::numeric_limits<FirExpressionId>::max()) {
                fail("go-source found invalid call argument order", span);
                return std::nullopt;
            }
            result[target] = call.arguments[source];
        }
        return result;
    }

    std::string integerHelper(FirBinaryOperator operation, Type type) {
        std::string operationName;
        switch (operation) {
        case FirBinaryOperator::Add:
            operationName = "Add";
            break;
        case FirBinaryOperator::Subtract:
            operationName = "Subtract";
            break;
        case FirBinaryOperator::Multiply:
            operationName = "Multiply";
            break;
        case FirBinaryOperator::Divide:
            operationName = "Divide";
            break;
        case FirBinaryOperator::Remainder:
            operationName = "Remainder";
            break;
        case FirBinaryOperator::ShiftLeft:
            operationName = "ShiftLeft";
            break;
        case FirBinaryOperator::ShiftRight:
            operationName = "ShiftRight";
            break;
        default:
            return {};
        }
        const auto tag = goSourceTypeTag(type);
        const auto key = operationName + tag;
        if (!helperNames_.contains(key)) {
            const auto name = uniqueName("foundation" + key);
            helperNames_[key] = name;
            helpers_[name] = renderIntegerHelper(name, operation, type);
        }
        return helperNames_.at(key);
    }

    std::string negateHelper(Type type) {
        const auto tag = goSourceTypeTag(type);
        const auto key = "Negate" + tag;
        if (!helperNames_.contains(key)) {
            const auto name = uniqueName("foundation" + key);
            helperNames_[key] = name;
            const auto goType = *goSourceType(type);
            const auto label = snakeName(tag);
            std::ostringstream output;
            output << "func " << name << "(value " << goType << ") " << goType << " {\n"
                   << "\tresult := -value\n"
                   << "\tif value < 0 && result < 0 {\n"
                   << "\t\tpanic(\"" << label << " overflow\")\n"
                   << "\t}\n"
                   << "\treturn result\n"
                   << "}\n";
            helpers_[name] = output.str();
        }
        return helperNames_.at(key);
    }

    static std::string renderIntegerHelper(std::string_view name, FirBinaryOperator operation,
                                           Type type) {
        const auto goType = *goSourceType(type);
        const auto label = snakeName(goSourceTypeTag(type));
        const auto signedType = isSignedInteger(type);
        std::ostringstream output;
        output << "func " << name << "(left, right " << goType << ") " << goType << " {\n";
        if (operation == FirBinaryOperator::ShiftLeft ||
            operation == FirBinaryOperator::ShiftRight) {
            const auto width = type.kind == TypeKind::I8 || type.kind == TypeKind::U8     ? "8"
                               : type.kind == TypeKind::I16 || type.kind == TypeKind::U16 ? "16"
                               : type.kind == TypeKind::I32 || type.kind == TypeKind::U32 ? "32"
                               : type.kind == TypeKind::I64 || type.kind == TypeKind::U64
                                   ? "64"
                                   : "uint(32) << (^uint(0) >> 63)";
            output << "\tif ";
            if (signedType) {
                output << "right < 0 || ";
            }
            output << "uint64(right) >= uint64(" << width << ") {\n"
                   << "\t\tpanic(\"shift count out of range\")\n"
                   << "\t}\n"
                   << "\tresult := left "
                   << (operation == FirBinaryOperator::ShiftLeft ? "<<" : ">>") << " uint(right)\n";
            if (operation == FirBinaryOperator::ShiftLeft) {
                output << "\tif right != 0 && result>>uint(right) != left {\n"
                       << "\t\tpanic(\"" << label << " overflow\")\n"
                       << "\t}\n";
            }
            output << "\treturn result\n}\n";
            return output.str();
        }
        if (operation == FirBinaryOperator::Divide || operation == FirBinaryOperator::Remainder) {
            output << "\tif right == 0 {\n"
                   << "\t\tpanic(\"division by zero\")\n"
                   << "\t}\n";
        }
        if (operation == FirBinaryOperator::Subtract && !signedType) {
            output << "\tif right > left {\n"
                   << "\t\tpanic(\"" << label << " overflow\")\n"
                   << "\t}\n";
        }
        if (operation == FirBinaryOperator::Multiply) {
            output << "\tif left == 0 || right == 0 {\n"
                   << "\t\treturn 0\n"
                   << "\t}\n";
        }
        const auto symbol = operation == FirBinaryOperator::Add        ? "+"
                            : operation == FirBinaryOperator::Subtract ? "-"
                            : operation == FirBinaryOperator::Multiply ? "*"
                            : operation == FirBinaryOperator::Divide   ? "/"
                                                                       : "%";
        output << "\tresult := left " << symbol << " right\n";
        if (operation == FirBinaryOperator::Add) {
            if (signedType) {
                output << "\tif (right > 0 && result < left) || "
                          "(right < 0 && result > left) {\n";
            } else {
                output << "\tif result < left {\n";
            }
            output << "\t\tpanic(\"" << label << " overflow\")\n\t}\n";
        } else if (operation == FirBinaryOperator::Subtract && signedType) {
            output << "\tif (right > 0 && result > left) || "
                      "(right < 0 && result < left) {\n"
                   << "\t\tpanic(\"" << label << " overflow\")\n\t}\n";
        } else if (operation == FirBinaryOperator::Multiply) {
            if (signedType) {
                output << "\tif (left == -1 && result == right) || result/left != right {\n";
            } else {
                output << "\tif result/left != right {\n";
            }
            output << "\t\tpanic(\"" << label << " overflow\")\n\t}\n";
        } else if (operation == FirBinaryOperator::Divide && signedType) {
            output << "\tif left < 0 && right == -1 && result == left {\n"
                   << "\t\tpanic(\"" << label << " overflow\")\n\t}\n";
        }
        output << "\treturn result\n}\n";
        return output.str();
    }

    std::optional<std::string> renderMatchExpression(const FirFunction &function,
                                                     const FirMatchExpression &match,
                                                     const Type &resultType, SourceSpan span,
                                                     unsigned int depth) {
        if (match.type.kind != TypeKind::Enum || match.type.declaration >= program_.enums.size()) {
            fail("go-source cannot render an invalid match", span);
            return std::nullopt;
        }
        const auto inspected = renderExpression(function, match.value, depth + 1);
        if (!inspected.has_value()) {
            return std::nullopt;
        }
        const auto temporary = temporaryLocal("match");
        const auto indentation = [](unsigned int value) { return std::string(value, '\t'); };
        std::ostringstream output;
        output << "func()";
        const auto renderedResult = sourceType(resultType);
        if (!renderedResult.has_value()) {
            fail("go-source cannot render match result type", span);
            return std::nullopt;
        }
        if (!renderedResult->empty()) {
            output << ' ' << *renderedResult;
        }
        output << " {\n" << indentation(depth + 1) << temporary << " := " << *inspected << "\n";

        for (const auto &arm : match.arms) {
            std::optional<Type> payload;
            std::string armCondition = "true";
            if (!arm.wildcard) {
                if (arm.variant >= program_.enums[match.type.declaration].variants.size()) {
                    fail("go-source cannot render an invalid match variant", span);
                    return std::nullopt;
                }
                payload = enumPayloadType(match.type, arm.variant);
                armCondition = temporary + ".tag == " + std::to_string(arm.variant);
                if (arm.pattern.has_value()) {
                    if (!payload.has_value()) {
                        fail("go-source cannot render a pattern for a unit variant", span);
                        return std::nullopt;
                    }
                    const auto pattern = renderExpression(function, *arm.pattern, depth + 1);
                    if (!pattern.has_value()) {
                        return std::nullopt;
                    }
                    armCondition += " && " + temporary + '.' +
                                    enumFieldName(match.type, arm.variant) + " == " + *pattern;
                }
            }
            output << indentation(depth + 1) << "if " << armCondition << " {\n";
            auto armDepth = depth + 2;
            if (arm.guardBinding.has_value()) {
                if (!payload.has_value()) {
                    fail("go-source cannot bind a unit match variant", span);
                    return std::nullopt;
                }
                const auto local = currentLocals_[*arm.guardBinding];
                output << indentation(armDepth) << local << " := " << temporary << '.'
                       << enumFieldName(match.type, arm.variant) << "\n"
                       << indentation(armDepth) << "_ = " << local << "\n";
            }
            const auto guarded = arm.guard.has_value();
            if (guarded) {
                const auto guard = renderExpression(function, *arm.guard, armDepth);
                if (!guard.has_value()) {
                    return std::nullopt;
                }
                output << indentation(armDepth) << "if " << condition(*guard) << " {\n";
                ++armDepth;
            }
            if (arm.binding.has_value()) {
                if (!payload.has_value()) {
                    fail("go-source cannot bind a unit match variant", span);
                    return std::nullopt;
                }
                const auto local = currentLocals_[*arm.binding];
                output << indentation(armDepth) << local << " := " << temporary << '.'
                       << enumFieldName(match.type, arm.variant) << "\n"
                       << indentation(armDepth) << "_ = " << local << "\n";
            }
            renderBlock(output, function, arm.block, armDepth);
            if (failed_) {
                return std::nullopt;
            }
            output << indentation(armDepth) << "return";
            if (!renderedResult->empty()) {
                if (!arm.expression.has_value()) {
                    fail("go-source cannot render a value match arm without a value", span);
                    return std::nullopt;
                }
                const auto value = renderExpression(function, *arm.expression, armDepth);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                output << ' ' << *value;
            }
            output << "\n";
            if (guarded) {
                --armDepth;
                output << indentation(armDepth) << "}\n";
            }
            output << indentation(depth + 1) << "}\n";
        }
        output << indentation(depth + 1) << "panic(\"invalid enum tag\")\n"
               << indentation(depth) << "}()";
        return output.str();
    }

    std::optional<std::string>
    renderConditionalExpression(const FirFunction &function,
                                const FirConditionalExpression &conditional, const Type &type,
                                SourceSpan span, unsigned int depth) {
        const auto renderedCondition = renderExpression(function, conditional.condition, depth);
        const auto renderedType = sourceType(type);
        if (!renderedCondition.has_value() || !renderedType.has_value()) {
            if (!failed_) {
                fail("go-source cannot render a conditional expression", span);
            }
            return std::nullopt;
        }

        const auto indentation = [](unsigned int value) { return std::string(value, '\t'); };
        std::ostringstream output;
        output << "func()";
        if (!renderedType->empty()) {
            output << ' ' << *renderedType;
        }
        output << " {\n"
               << indentation(depth + 1) << "if " << condition(*renderedCondition) << " {\n";

        const auto renderBranch = [&](FirBlockId block, FirExpressionId value,
                                      unsigned int branchDepth) {
            renderBlock(output, function, block, branchDepth);
            if (failed_) {
                return false;
            }
            const auto rendered = renderExpression(function, value, branchDepth);
            if (!rendered.has_value()) {
                return false;
            }
            output << indentation(branchDepth);
            if (!renderedType->empty()) {
                output << "return ";
            }
            output << *rendered << "\n";
            if (renderedType->empty()) {
                output << indentation(branchDepth) << "return\n";
            }
            return true;
        };

        if (!renderBranch(conditional.thenBlock, conditional.thenValue, depth + 2)) {
            return std::nullopt;
        }
        output << indentation(depth + 1) << "} else {\n";
        if (!renderBranch(conditional.elseBlock, conditional.elseValue, depth + 2)) {
            return std::nullopt;
        }
        output << indentation(depth + 1) << "}\n" << indentation(depth) << "}()";
        return output.str();
    }

    bool addressableExpression(const FirFunction &function, FirExpressionId id) const {
        if (id >= function.expressions.size()) {
            return false;
        }
        const auto &expression = function.expressions[id].value;
        if (std::holds_alternative<FirLocalExpression>(expression) ||
            std::holds_alternative<FirReadExpression>(expression) ||
            std::holds_alternative<FirMoveExpression>(expression)) {
            return true;
        }
        if (const auto *value = std::get_if<FirFieldExpression>(&expression)) {
            return addressableExpression(function, value->base);
        }
        if (const auto *value = std::get_if<FirIndexExpression>(&expression)) {
            return addressableExpression(function, value->base);
        }
        if (const auto *value = std::get_if<FirOwnershipExpression>(&expression)) {
            return addressableExpression(function, value->operand);
        }
        return false;
    }

    std::optional<std::string> renderReplaceExpression(const FirFunction &function,
                                                       const FirReplaceExpression &replace,
                                                       const Type &type, SourceSpan span,
                                                       unsigned int depth) {
        const auto value = renderExpression(function, replace.value, depth);
        const auto target = renderExpression(function, replace.target, depth);
        const auto renderedType = sourceType(type);
        if (!value.has_value() || !target.has_value() || !renderedType.has_value() ||
            renderedType->empty() || !addressableExpression(function, replace.target)) {
            if (!failed_) {
                fail("go-source cannot render replace", span);
            }
            return std::nullopt;
        }

        const auto indentation = [](unsigned int value) { return std::string(value, '\t'); };
        std::ostringstream output;
        output << "func(value " << *renderedType << ", target *" << *renderedType << ") "
               << *renderedType << " {\n"
               << indentation(depth + 1) << "previous := *target\n"
               << indentation(depth + 1) << "*target = value\n"
               << indentation(depth + 1) << "return previous\n"
               << indentation(depth) << "}(" << *value << ", &(" << *target << "))";
        return output.str();
    }

    std::optional<std::string> renderCallArgument(const FirFunction &function,
                                                  FirExpressionId argument, const Type &parameter,
                                                  unsigned int depth) {
        const auto rendered = renderExpression(function, argument, depth);
        if (!rendered.has_value()) {
            return std::nullopt;
        }
        if (pointerParameter(parameter)) {
            return "&(" + *rendered + ')';
        }
        return rendered;
    }

    std::optional<std::string> renderClosureExpression(const FirFunction &outer,
                                                       const FirClosureExpression &closure,
                                                       const Type &type, SourceSpan span,
                                                       unsigned int depth) {
        if (closure.function >= program_.functions.size()) {
            fail("go-source cannot render an invalid closure", span);
            return std::nullopt;
        }
        const auto &target = program_.functions[closure.function];
        auto targetLocals = localNames(target);
        const auto rawTargetLocals = targetLocals;
        std::vector<FirLocalId> captureLocals;
        for (FirLocalId local{}; local < target.locals.size(); ++local) {
            if (target.locals[local].capture) {
                captureLocals.push_back(local);
            }
        }
        if (captureLocals.size() != closure.captures.size()) {
            fail("go-source cannot render closure capture metadata", span);
            return std::nullopt;
        }

        const auto outerLocals = currentLocals_;
        const auto outerGeneratedLocals = currentGeneratedLocals_;
        std::vector<std::string> captureParameters;
        std::vector<std::string> captureArguments;
        for (std::size_t index{}; index < closure.captures.size(); ++index) {
            const auto &capture = closure.captures[index];
            if (capture.local >= outer.locals.size() || capture.local >= outerLocals.size()) {
                fail("go-source cannot render an invalid closure capture", span);
                return std::nullopt;
            }
            const auto renderedType = sourceType(outer.locals[capture.local].type);
            if (!renderedType.has_value() || renderedType->empty()) {
                fail("go-source cannot render a closure capture type", span);
                return std::nullopt;
            }
            const auto targetLocal = captureLocals[index];
            const auto &name = rawTargetLocals[targetLocal];
            if (capture.mode == FirCaptureMode::View || capture.mode == FirCaptureMode::Edit) {
                captureParameters.push_back(name + " *" + *renderedType);
                captureArguments.push_back("&(" + outerLocals[capture.local] + ')');
                targetLocals[targetLocal] = '*' + name;
            } else {
                captureParameters.push_back(name + ' ' + *renderedType);
                captureArguments.push_back(outerLocals[capture.local]);
            }
        }

        std::vector<std::string> parameters;
        for (const auto local : target.parameters) {
            if (local >= target.locals.size() || local >= rawTargetLocals.size()) {
                fail("go-source cannot render closure parameter metadata", span);
                return std::nullopt;
            }
            const auto renderedType = sourceParameterType(target.locals[local].type);
            if (!renderedType.has_value() || renderedType->empty()) {
                fail("go-source cannot render a closure parameter type", span);
                return std::nullopt;
            }
            const auto &name = rawTargetLocals[local];
            parameters.push_back(name + ' ' + *renderedType);
            if (pointerParameter(target.locals[local].type)) {
                targetLocals[local] = '*' + name;
            }
        }

        const auto callableType = sourceType(type);
        const auto resultType = sourceType(target.returnType);
        if (!callableType.has_value() || callableType->empty() || !resultType.has_value()) {
            fail("go-source cannot render a closure signature", span);
            return std::nullopt;
        }

        currentLocals_ = std::move(targetLocals);
        currentGeneratedLocals_ =
            std::set<std::string>(rawTargetLocals.begin(), rawTargetLocals.end());
        const auto indentation = [](unsigned int value) { return std::string(value, '\t'); };
        const auto wrapped = !captureParameters.empty();
        std::ostringstream output;
        if (wrapped) {
            output << "func(";
            for (std::size_t index{}; index < captureParameters.size(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                output << captureParameters[index];
            }
            output << ") " << *callableType << " {\n" << indentation(depth + 1) << "return ";
        }
        output << "func(";
        for (std::size_t index{}; index < parameters.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << parameters[index];
        }
        output << ')';
        if (!resultType->empty()) {
            output << ' ' << *resultType;
        }
        output << " {\n";
        renderBlock(output, target, target.body, depth + (wrapped ? 2 : 1));
        output << indentation(depth + (wrapped ? 1 : 0)) << '}';
        if (wrapped) {
            output << "\n" << indentation(depth) << "}(";
            for (std::size_t index{}; index < captureArguments.size(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                output << captureArguments[index];
            }
            output << ')';
        }
        currentLocals_ = outerLocals;
        currentGeneratedLocals_ = outerGeneratedLocals;
        if (failed_) {
            return std::nullopt;
        }
        return output.str();
    }

    std::optional<std::string> renderExpression(const FirFunction &function, FirExpressionId id,
                                                unsigned int depth = 0) {
        const auto &expression = function.expressions[id];
        if (const auto *integer = std::get_if<FirIntegerExpression>(&expression.value)) {
            const auto sign = integer->negative ? "-" : "";
            return *goSourceType(expression.type) + '(' + sign +
                   std::to_string(integer->magnitude) + ')';
        }
        if (const auto *floating = std::get_if<FirFloatingExpression>(&expression.value)) {
            return *goSourceType(expression.type) + '(' + floating->text + ')';
        }
        if (const auto *boolean = std::get_if<FirBooleanExpression>(&expression.value)) {
            return boolean->value ? "true" : "false";
        }
        if (const auto *string = std::get_if<FirStringExpression>(&expression.value)) {
            return goQuotedString(string->value);
        }
        if (const auto *array = std::get_if<FirArrayExpression>(&expression.value)) {
            const auto type = sourceType(expression.type);
            if (!type.has_value()) {
                fail("go-source cannot render an array type", expression.span);
                return std::nullopt;
            }
            std::ostringstream output;
            output << *type << '{';
            for (std::size_t index{}; index < array->elements.size(); ++index) {
                const auto element = renderExpression(function, array->elements[index], depth);
                if (!element.has_value()) {
                    return std::nullopt;
                }
                if (index != 0) {
                    output << ", ";
                }
                output << *element;
            }
            output << '}';
            return output.str();
        }
        const auto localName = [&](FirLocalId local) -> std::optional<std::string> {
            if (local >= currentLocals_.size()) {
                fail("go-source found an invalid local while rendering", expression.span);
                return std::nullopt;
            }
            return currentLocals_[local];
        };
        if (const auto *local = std::get_if<FirLocalExpression>(&expression.value)) {
            return localName(local->local);
        }
        if (const auto *local = std::get_if<FirReadExpression>(&expression.value)) {
            return localName(local->local);
        }
        if (const auto *local = std::get_if<FirMoveExpression>(&expression.value)) {
            return localName(local->local);
        }
        if (const auto *value = std::get_if<FirFunctionValueExpression>(&expression.value)) {
            if (value->function >= program_.functions.size() || !names_.contains(value->function)) {
                fail("go-source cannot resolve a function value", expression.span);
                return std::nullopt;
            }
            return names_.at(value->function);
        }
        if (const auto *value = std::get_if<FirClosureExpression>(&expression.value)) {
            return renderClosureExpression(function, *value, expression.type, expression.span,
                                           depth);
        }
        if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
            const auto operand = renderExpression(function, ownership->operand, depth);
            if (!operand.has_value()) {
                return std::nullopt;
            }
            auto source = function.expressions[ownership->operand].type;
            auto target = expression.type;
            if ((source.kind == TypeKind::Own || source.kind == TypeKind::View ||
                 source.kind == TypeKind::Edit) &&
                source.arguments.size() == 1) {
                source = source.arguments.front();
            }
            if ((target.kind == TypeKind::View || target.kind == TypeKind::Edit) &&
                target.arguments.size() == 1) {
                target = target.arguments.front();
            }
            if (source.kind == TypeKind::Array && target.kind == TypeKind::Slice) {
                if (!addressableExpression(function, ownership->operand)) {
                    if (ownership->operation == FirOwnershipOperator::Edit) {
                        fail("go-source cannot edit a temporary array", expression.span);
                        return std::nullopt;
                    }
                    const auto sourceName = sourceType(source);
                    const auto targetName = sourceType(target);
                    if (!sourceName.has_value() || !targetName.has_value()) {
                        fail("go-source cannot render an array-to-slice conversion",
                             expression.span);
                        return std::nullopt;
                    }
                    return "func(value " + *sourceName + ") " + *targetName +
                           " { return value[:] }(" + *operand + ')';
                }
                return '(' + *operand + ")[:]";
            }
            return operand;
        }
        if (const auto *value = std::get_if<FirEnumExpression>(&expression.value)) {
            if (value->type.declaration >= program_.enums.size() ||
                value->variant >= program_.enums[value->type.declaration].variants.size()) {
                fail("go-source cannot render an enum value", expression.span);
                return std::nullopt;
            }
            std::ostringstream output;
            output << enumName(value->type) << "{tag: " << value->variant;
            if (value->payload.has_value()) {
                const auto payload = renderExpression(function, *value->payload, depth);
                if (!payload.has_value()) {
                    return std::nullopt;
                }
                output << ", " << enumFieldName(value->type, value->variant) << ": " << *payload;
            }
            output << '}';
            return output.str();
        }
        if (const auto *value = std::get_if<FirMatchExpression>(&expression.value)) {
            return renderMatchExpression(function, *value, expression.type, expression.span, depth);
        }
        if (const auto *value = std::get_if<FirConditionalExpression>(&expression.value)) {
            return renderConditionalExpression(function, *value, expression.type, expression.span,
                                               depth);
        }
        if (const auto *value = std::get_if<FirStructExpression>(&expression.value)) {
            if (value->type.declaration >= program_.structs.size()) {
                fail("go-source cannot render a struct value", expression.span);
                return std::nullopt;
            }
            std::ostringstream output;
            output << structName(value->type) << '{';
            for (std::size_t index{}; index < value->fields.size(); ++index) {
                const auto field = value->fields[index];
                const auto rendered = renderExpression(function, field.value, depth);
                if (!rendered.has_value()) {
                    return std::nullopt;
                }
                if (index != 0) {
                    output << ", ";
                }
                output << fieldName(value->type.declaration, field.field) << ": " << *rendered;
            }
            output << '}';
            return output.str();
        }
        if (const auto *value = std::get_if<FirFieldExpression>(&expression.value)) {
            const auto base = renderExpression(function, value->base, depth);
            if (!base.has_value()) {
                return std::nullopt;
            }
            auto type = function.expressions[value->base].type;
            if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
                type.arguments.size() == 1) {
                type = type.arguments.front();
            }
            if (type.kind != TypeKind::Struct || type.declaration >= program_.structs.size() ||
                value->field >= program_.structs[type.declaration].fields.size()) {
                fail("go-source cannot render a field access", expression.span);
                return std::nullopt;
            }
            return '(' + *base + ")." + fieldName(type.declaration, value->field);
        }
        if (const auto *value = std::get_if<FirIndexExpression>(&expression.value)) {
            const auto base = renderExpression(function, value->base, depth);
            const auto index = renderExpression(function, value->index, depth);
            if (!base.has_value() || !index.has_value()) {
                return std::nullopt;
            }
            return '(' + *base + ")[" + *index + ']';
        }
        if (const auto *value = std::get_if<FirReplaceExpression>(&expression.value)) {
            return renderReplaceExpression(function, *value, expression.type, expression.span,
                                           depth);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
            const auto operand = renderExpression(function, unary->operand, depth);
            if (!operand.has_value()) {
                return std::nullopt;
            }
            if (unary->operation == FirUnaryOperator::Not) {
                return "(!" + *operand + ')';
            }
            if (unary->operation == FirUnaryOperator::Empty) {
                return "(len(" + *operand + ") == 0)";
            }
            if (isFloating(expression.type)) {
                return "(-" + *operand + ')';
            }
            return negateHelper(expression.type) + '(' + *operand + ')';
        }
        if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
            const auto left = renderExpression(function, binary->left, depth);
            const auto right = renderExpression(function, binary->right, depth);
            if (!left.has_value() || !right.has_value()) {
                return std::nullopt;
            }
            const auto operandType = function.expressions[binary->left].type;
            if (binary->operation == FirBinaryOperator::Add && operandType == stringType) {
                return '(' + *left + " + " + *right + ')';
            }
            if (binary->operation == FirBinaryOperator::Add ||
                binary->operation == FirBinaryOperator::Subtract ||
                binary->operation == FirBinaryOperator::Multiply ||
                binary->operation == FirBinaryOperator::Divide ||
                binary->operation == FirBinaryOperator::Remainder ||
                binary->operation == FirBinaryOperator::ShiftLeft ||
                binary->operation == FirBinaryOperator::ShiftRight) {
                if (isInteger(operandType)) {
                    return integerHelper(binary->operation, operandType) + '(' + *left + ", " +
                           *right + ')';
                }
            }
            const auto symbol = binary->operation == FirBinaryOperator::Add            ? "+"
                                : binary->operation == FirBinaryOperator::Subtract     ? "-"
                                : binary->operation == FirBinaryOperator::Multiply     ? "*"
                                : binary->operation == FirBinaryOperator::Divide       ? "/"
                                : binary->operation == FirBinaryOperator::Remainder    ? "%"
                                : binary->operation == FirBinaryOperator::ShiftLeft    ? "<<"
                                : binary->operation == FirBinaryOperator::ShiftRight   ? ">>"
                                : binary->operation == FirBinaryOperator::Equal        ? "=="
                                : binary->operation == FirBinaryOperator::NotEqual     ? "!="
                                : binary->operation == FirBinaryOperator::Less         ? "<"
                                : binary->operation == FirBinaryOperator::LessEqual    ? "<="
                                : binary->operation == FirBinaryOperator::Greater      ? ">"
                                : binary->operation == FirBinaryOperator::GreaterEqual ? ">="
                                : binary->operation == FirBinaryOperator::And          ? "&&"
                                                                                       : "||";
            return '(' + *left + ' ' + symbol + ' ' + *right + ')';
        }
        if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
            const auto arguments = orderedArguments(*call, expression.span);
            if (!arguments.has_value()) {
                return std::nullopt;
            }
            if (call->kind == FirCallKind::Len) {
                if (arguments->size() != 1) {
                    fail("go-source cannot render len arguments", expression.span);
                    return std::nullopt;
                }
                const auto argument = renderExpression(function, arguments->front(), depth);
                if (!argument.has_value()) {
                    return std::nullopt;
                }
                return *goSourceType(expression.type) + "(len(" + *argument + "))";
            }
            if (call->kind == FirCallKind::FunctionValue) {
                if (call->local >= function.locals.size() || call->local >= currentLocals_.size()) {
                    fail("go-source cannot resolve a function value call", expression.span);
                    return std::nullopt;
                }
                auto callable = function.locals[call->local].type;
                if ((callable.kind == TypeKind::View || callable.kind == TypeKind::Edit) &&
                    callable.arguments.size() == 1) {
                    callable = callable.arguments.front();
                }
                if (callable.kind != TypeKind::Function ||
                    callable.arguments.size() != arguments->size() + 1) {
                    fail("go-source cannot render a function value call", expression.span);
                    return std::nullopt;
                }
                std::ostringstream output;
                output << '(' << currentLocals_[call->local] << ")(";
                for (std::size_t index{}; index < arguments->size(); ++index) {
                    const auto argument = renderCallArgument(function, (*arguments)[index],
                                                             callable.arguments[index + 1], depth);
                    if (!argument.has_value()) {
                        return std::nullopt;
                    }
                    if (index != 0) {
                        output << ", ";
                    }
                    output << *argument;
                }
                output << ')';
                return output.str();
            }
            if (!names_.contains(call->function) || call->function >= program_.functions.size()) {
                if (!failed_) {
                    fail("go-source cannot resolve a called function", expression.span);
                }
                return std::nullopt;
            }
            std::ostringstream output;
            std::size_t firstArgument{};
            const auto &target = program_.functions[call->function];
            if (target.receiver.has_value()) {
                if (arguments->empty()) {
                    fail("go-source cannot render a method call without a receiver",
                         expression.span);
                    return std::nullopt;
                }
                const auto receiver = renderExpression(function, arguments->front(), depth);
                if (!receiver.has_value()) {
                    return std::nullopt;
                }
                output << '(' << *receiver << ")." << names_.at(call->function) << '(';
                firstArgument = 1;
            } else {
                output << names_.at(call->function) << '(';
            }
            for (std::size_t index = firstArgument; index < arguments->size(); ++index) {
                if (index >= target.parameters.size() ||
                    target.parameters[index] >= target.locals.size()) {
                    fail("go-source cannot render function parameter metadata", expression.span);
                    return std::nullopt;
                }
                const auto parameter = target.parameters[index];
                const auto argument = renderCallArgument(function, (*arguments)[index],
                                                         target.locals[parameter].type, depth);
                if (!argument.has_value()) {
                    return std::nullopt;
                }
                if (index != firstArgument) {
                    output << ", ";
                }
                output << *argument;
            }
            output << ')';
            return output.str();
        }
        fail("go-source cannot render an expression", expression.span);
        return std::nullopt;
    }

    void renderBlock(std::ostringstream &output, const FirFunction &function, FirBlockId id,
                     unsigned int depth) {
        for (const auto statementId : function.blocks[id].statements) {
            renderStatement(output, function, function.statements[statementId], depth);
            if (failed_) {
                return;
            }
        }
    }

    static std::string condition(std::string value) {
        if (value.size() < 2 || value.front() != '(' || value.back() != ')') {
            return value;
        }
        std::size_t depth{};
        auto quoted = false;
        auto escaped = false;
        for (std::size_t index{}; index < value.size(); ++index) {
            const auto character = value[index];
            if (quoted) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    quoted = false;
                }
                continue;
            }
            if (character == '"') {
                quoted = true;
            } else if (character == '(') {
                ++depth;
            } else if (character == ')') {
                if (depth == 0) {
                    return value;
                }
                --depth;
                if (depth == 0 && index + 1 != value.size()) {
                    return value;
                }
            }
        }
        if (quoted || depth != 0) {
            return value;
        }
        return value.substr(1, value.size() - 2);
    }

    void renderStatement(std::ostringstream &output, const FirFunction &function,
                         const FirStatement &statement, unsigned int depth) {
        const auto indentation = std::string(depth, '\t');
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            const auto value = renderExpression(function, variable->initializer, depth);
            if (value.has_value()) {
                output << indentation << currentLocals_[variable->local] << " := " << *value
                       << "\n";
            }
            return;
        }
        if (const auto *binding = std::get_if<FirLetElseStatement>(&statement.value)) {
            const auto value = renderExpression(function, binding->initializer, depth);
            if (!value.has_value()) {
                return;
            }
            const auto type = function.expressions[binding->initializer].type;
            const auto temporary = temporaryLocal("result");
            output << indentation << temporary << " := " << *value << "\n"
                   << indentation << "if " << temporary << ".tag == 1 {\n"
                   << indentation << '\t' << currentLocals_[binding->errorLocal]
                   << " := " << temporary << '.' << enumFieldName(type, 1) << "\n"
                   << indentation << "\t_ = " << currentLocals_[binding->errorLocal] << "\n";
            renderBlock(output, function, binding->elseBlock, depth + 1);
            output << indentation << "}\n"
                   << indentation << currentLocals_[binding->local] << " := " << temporary << '.'
                   << enumFieldName(type, 0) << "\n"
                   << indentation << "_ = " << currentLocals_[binding->local] << "\n";
            return;
        }
        if (const auto *binding = std::get_if<FirResultElseStatement>(&statement.value)) {
            const auto value = renderExpression(function, binding->expression, depth);
            if (!value.has_value()) {
                return;
            }
            const auto type = function.expressions[binding->expression].type;
            const auto temporary = temporaryLocal("result");
            output << indentation << temporary << " := " << *value << "\n"
                   << indentation << "if " << temporary << ".tag == 1 {\n"
                   << indentation << '\t' << currentLocals_[binding->errorLocal]
                   << " := " << temporary << '.' << enumFieldName(type, 1) << "\n"
                   << indentation << "\t_ = " << currentLocals_[binding->errorLocal] << "\n";
            renderBlock(output, function, binding->elseBlock, depth + 1);
            output << indentation << "}\n";
            return;
        }
        if (const auto *destructure =
                std::get_if<FirStructDestructureStatement>(&statement.value)) {
            const auto initializer = renderExpression(function, destructure->initializer, depth);
            if (!initializer.has_value() || destructure->type.kind != TypeKind::Struct ||
                destructure->type.declaration >= program_.structs.size()) {
                if (!failed_) {
                    fail("go-source cannot render struct destructuring", statement.span);
                }
                return;
            }
            const auto temporary = temporaryLocal("destructure");
            output << indentation << temporary << " := " << *initializer << "\n";
            for (const auto &binding : destructure->bindings) {
                if (binding.local >= currentLocals_.size()) {
                    fail("go-source found an invalid struct binding while rendering",
                         statement.span);
                    return;
                }
                const auto &local = currentLocals_[binding.local];
                output << indentation << local << " := (" << temporary << ")."
                       << fieldName(destructure->type.declaration, binding.field) << "\n"
                       << indentation << "_ = " << local << "\n";
            }
            return;
        }
        if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            const auto target = renderExpression(function, assignment->target, depth);
            if (!target.has_value()) {
                return;
            }
            if (assignment->operation == FirAssignmentOperator::Assign) {
                const auto value = renderExpression(function, assignment->value, depth);
                if (value.has_value()) {
                    output << indentation << *target << " = " << *value << "\n";
                }
                return;
            }
            const auto type = function.expressions[assignment->target].type;
            const auto operation = assignmentBinary(assignment->operation);
            const auto pointer = temporaryLocal("compound");
            output << indentation << pointer << " := &(" << *target << ")\n";
            const auto value = renderExpression(function, assignment->value, depth);
            if (!value.has_value()) {
                return;
            }
            output << indentation << '*' << pointer << " = ";
            if (isInteger(type)) {
                output << integerHelper(operation, type) << "(*" << pointer << ", " << *value
                       << ")\n";
            } else {
                const auto symbol = operation == FirBinaryOperator::Add          ? "+"
                                    : operation == FirBinaryOperator::Subtract   ? "-"
                                    : operation == FirBinaryOperator::Multiply   ? "*"
                                    : operation == FirBinaryOperator::Divide     ? "/"
                                    : operation == FirBinaryOperator::ShiftLeft  ? "<<"
                                    : operation == FirBinaryOperator::ShiftRight ? ">>"
                                                                                 : "%";
                output << '*' << pointer << ' ' << symbol << ' ' << *value << "\n";
            }
            return;
        }
        if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            const auto value = renderExpression(function, expression->expression, depth);
            if (value.has_value()) {
                if (function.expressions[expression->expression].type == voidType) {
                    output << indentation << *value << "\n";
                } else {
                    output << indentation << "_ = " << *value << "\n";
                }
            }
            return;
        }
        if (const auto *discarded = std::get_if<FirDiscardStatement>(&statement.value)) {
            const auto value = renderExpression(function, discarded->expression, depth);
            if (value.has_value()) {
                output << indentation << "_ = " << *value << "\n";
            }
            return;
        }
        if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
            output << indentation << "return";
            if (returned->value.has_value()) {
                const auto value = renderExpression(function, *returned->value, depth);
                if (!value.has_value()) {
                    return;
                }
                output << ' ' << *value;
            }
            output << "\n";
            return;
        }
        if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            const auto condition = renderExpression(function, branch->condition, depth);
            if (!condition.has_value()) {
                return;
            }
            output << indentation << "if " << GoSourceEmitter::condition(*condition) << " {\n";
            renderBlock(output, function, branch->thenBlock, depth + 1);
            output << indentation << '}';
            if (branch->elseBlock.has_value()) {
                output << " else {\n";
                renderBlock(output, function, *branch->elseBlock, depth + 1);
                output << indentation << '}';
            }
            output << "\n";
            return;
        }
        if (const auto *loop = std::get_if<FirWhileStatement>(&statement.value)) {
            const auto condition = renderExpression(function, loop->condition, depth);
            if (!condition.has_value()) {
                return;
            }
            output << indentation << "for " << GoSourceEmitter::condition(*condition) << " {\n";
            renderBlock(output, function, loop->body, depth + 1);
            output << indentation << "}\n";
            return;
        }
        if (const auto *loop = std::get_if<FirForStatement>(&statement.value)) {
            const auto sequence = renderExpression(function, loop->sequence, depth);
            if (!sequence.has_value()) {
                return;
            }
            const auto sequenceName = currentLocals_[loop->sequenceStorage];
            const auto indexName = currentLocals_[loop->index];
            const auto rawIndex = temporaryLocal("index");
            const auto previousIndex = currentLocals_[loop->index];
            const auto previousValue = currentLocals_[loop->value];
            currentLocals_[loop->value] = sequenceName + '[' + indexName + ']';
            output << indentation << sequenceName << " := " << *sequence << "\n"
                   << indentation << "for " << rawIndex << " := range " << sequenceName << " {\n"
                   << indentation << '\t' << indexName << " := uint(" << rawIndex << ")\n"
                   << indentation << "\t_ = " << indexName << "\n";
            renderBlock(output, function, loop->body, depth + 1);
            currentLocals_[loop->index] = previousIndex;
            currentLocals_[loop->value] = previousValue;
            output << indentation << "}\n";
            return;
        }
        if (std::holds_alternative<FirBreakStatement>(statement.value)) {
            output << indentation << "break\n";
            return;
        }
        if (std::holds_alternative<FirContinueStatement>(statement.value)) {
            output << indentation << "continue\n";
        }
    }

    void renderFunction(std::ostringstream &output, FirFunctionId id) {
        const auto &function = program_.functions[id];
        currentLocals_ = localNames(function);
        const auto rawLocals = currentLocals_;
        currentGeneratedLocals_ = std::set<std::string>(rawLocals.begin(), rawLocals.end());
        std::size_t firstParameter{};
        if (function.receiver.has_value()) {
            const auto local = function.parameters.front();
            output << "func (" << rawLocals[local] << ' ';
            if (*function.receiver == FirReceiverKind::Edit) {
                output << '*';
            }
            output << *sourceType(function.locals[local].type) << ") " << names_.at(id) << '(';
            firstParameter = 1;
        } else {
            output << "func " << names_.at(id) << '(';
        }
        for (std::size_t index = firstParameter; index < function.parameters.size(); ++index) {
            const auto local = function.parameters[index];
            if (index != firstParameter) {
                output << ", ";
            }
            output << rawLocals[local] << ' ' << *sourceParameterType(function.locals[local].type);
            if (pointerParameter(function.locals[local].type)) {
                currentLocals_[local] = '*' + rawLocals[local];
            }
        }
        output << ')';
        const auto result = *sourceType(function.returnType);
        if (!result.empty()) {
            output << ' ' << result;
        }
        output << " {\n";
        renderBlock(output, function, function.body, 1);
        output << "}\n";
        const auto next = std::find(order_.begin(), order_.end(), id);
        if (next != order_.end() && std::next(next) != order_.end()) {
            output << '\n';
        }
    }

    void renderStructs(std::ostringstream &output) {
        auto first = true;
        for (const auto &[key, type] : reachableStructs_) {
            static_cast<void>(key);
            if (!first) {
                output << '\n';
            }
            first = false;
            output << "type " << structName(type) << " struct {\n";
            const auto id = type.declaration;
            const auto &declaration = program_.structs[id];
            std::size_t fieldWidth{};
            for (FirFieldId field{}; field < declaration.fields.size(); ++field) {
                fieldWidth = std::max(fieldWidth, fieldName(id, field).size());
            }
            for (FirFieldId field{}; field < declaration.fields.size(); ++field) {
                const auto name = fieldName(id, field);
                output << '\t' << name << std::string(fieldWidth - name.size() + 1, ' ')
                       << *sourceType(substituteGoSourceType(declaration.fields[field].type,
                                                             type.arguments))
                       << "\n";
            }
            output << "}\n";
        }
    }

    void renderEnums(std::ostringstream &output) {
        auto first = true;
        for (const auto &[key, type] : reachableEnums_) {
            static_cast<void>(key);
            if (!first) {
                output << '\n';
            }
            first = false;
            const auto generatedType = enumName(type);
            const auto &declaration = program_.enums[type.declaration];
            std::size_t fieldWidth = 3;
            for (FirVariantId variant{}; variant < declaration.variants.size(); ++variant) {
                if (enumPayloadType(type, variant).has_value()) {
                    fieldWidth = std::max(fieldWidth, enumFieldName(type, variant).size());
                }
            }
            output << "type " << generatedType << " struct {\n"
                   << "\ttag" << std::string(fieldWidth - 2, ' ') << "uint32\n";
            for (FirVariantId variant{}; variant < declaration.variants.size(); ++variant) {
                const auto payload = enumPayloadType(type, variant);
                if (!payload.has_value()) {
                    continue;
                }
                const auto field = enumFieldName(type, variant);
                output << '\t' << field << std::string(fieldWidth - field.size() + 1, ' ')
                       << *sourceType(*payload) << "\n";
            }
            output << "}\n\n";

            for (FirVariantId variant{}; variant < declaration.variants.size(); ++variant) {
                const auto payload = enumPayloadType(type, variant);
                output << "func " << enumConstructorName(type, variant) << '(';
                if (payload.has_value()) {
                    output << "value " << *sourceType(*payload);
                }
                output << ") " << generatedType << " {\n\treturn " << generatedType
                       << "{tag: " << variant;
                if (payload.has_value()) {
                    output << ", " << enumFieldName(type, variant) << ": value";
                }
                output << "}\n}\n\n";

                const auto method = enumMethodName(type, variant);
                output << "func (value " << generatedType << ") Is" << method
                       << "() bool {\n\treturn value.tag == " << variant << "\n}\n";
                if (payload.has_value()) {
                    output << "\nfunc (value " << generatedType << ") Get" << method << "() ("
                           << *sourceType(*payload) << ", bool) {\n\treturn value."
                           << enumFieldName(type, variant) << ", value.tag == " << variant
                           << "\n}\n";
                }
                if (variant + 1 != declaration.variants.size()) {
                    output << '\n';
                }
            }
        }
    }

    const FirProgram &program_;
    const PackageInterface &packageInterface_;
    Diagnostics &diagnostics_;
    std::vector<unsigned char> states_;
    std::map<FirFunctionId, std::string> names_;
    std::set<std::string> usedNames_;
    std::vector<FirFunctionId> order_;
    std::map<std::string, std::string> helperNames_;
    std::map<std::string, std::string> helpers_;
    std::map<std::string, unsigned char> structStates_;
    std::map<std::string, Type> reachableStructs_;
    std::map<std::string, std::string> structNames_;
    std::set<FirStructId> preparedStructFields_;
    std::map<std::pair<FirStructId, FirFieldId>, std::string> structFieldNames_;
    std::map<std::string, unsigned char> enumStates_;
    std::map<std::string, Type> reachableEnums_;
    std::map<std::string, std::string> enumNames_;
    std::set<std::string> preparedEnums_;
    std::map<std::pair<std::string, FirVariantId>, std::string> enumFieldNames_;
    std::map<std::pair<std::string, FirVariantId>, std::string> enumMethodNames_;
    std::map<std::pair<std::string, FirVariantId>, std::string> enumConstructorNames_;
    std::vector<std::string> currentLocals_;
    std::set<std::string> currentGeneratedLocals_;
    bool failed_{};
};

std::optional<PackageExport> generateGoSource(const FirProgram &program,
                                              const PackageInterface &packageInterface,
                                              Diagnostics &diagnostics) {
    for (const auto &function : program.functions) {
        if (function.hasBody && function.exported && !function.method &&
            function.packageName == packageInterface.package && function.typeParameterCount != 0) {
            diagnostics.error("FDN4120",
                              "go-source cannot expose open generic function " + function.name +
                                  "; add a non-generic exported wrapper or use go-cgo or "
                                  "go-dynamic for this boundary",
                              function.sourceSpan);
            return std::nullopt;
        }
    }
    const auto specialized = specializeSourcePackage(program, packageInterface.package);
    GoSourceEmitter emitter(specialized, packageInterface, diagnostics);
    const auto source = emitter.emit();
    if (!source.has_value()) {
        return std::nullopt;
    }
    PackageExport result;
    result.files.push_back({goPackageName(packageInterface) + ".go", std::move(*source)});
    result.files.push_back({"go.mod", renderGoModule(packageInterface, false)});
    result.files.push_back({"foundation.pii.json", renderPackageInterfaceJson(packageInterface)});
    return result;
}

PackageExport generateGoCgo(const PackageInterface &packageInterface) {
    PackageExport result;
    result.artifact = PackageExportArtifact::Static;
    result.files.push_back(
        {goPackageName(packageInterface) + ".go", renderCgoSource(packageInterface)});
    result.files.push_back({"go.mod", renderGoModule(packageInterface, false)});
    result.files.push_back({"foundation.pii.json", renderPackageInterfaceJson(packageInterface)});
    return result;
}

PackageExport generateGoDynamic(const PackageInterface &packageInterface) {
    PackageExport result;
    result.artifact = PackageExportArtifact::Shared;
    result.files.push_back(
        {goPackageName(packageInterface) + ".go", renderGoDynamicSource(packageInterface)});
    result.files.push_back({"go.mod", renderGoModule(packageInterface, true)});
    result.files.push_back({"go.sum", renderGoSum()});
    result.files.push_back({"foundation.pii.json", renderPackageInterfaceJson(packageInterface)});
    return result;
}

PackageExport generateZig(const PackageInterface &packageInterface) {
    PackageExport result;
    result.artifact = PackageExportArtifact::Static;
    result.files.push_back({"src/root.zig", renderZigSource(packageInterface)});
    result.files.push_back({"build.zig", renderZigBuild(packageInterface)});
    result.files.push_back({"build.zig.zon", renderZigManifest(packageInterface)});
    result.files.push_back({"foundation.pii.json", renderPackageInterfaceJson(packageInterface)});
    return result;
}

PackageExport generateRust(const PackageInterface &packageInterface) {
    PackageExport result;
    result.artifact = PackageExportArtifact::Static;
    result.files.push_back({"src/lib.rs", renderRustSource(packageInterface)});
    result.files.push_back({"Cargo.toml", renderRustManifest(packageInterface)});
    result.files.push_back({"build.rs", renderRustBuild(packageInterface)});
    result.files.push_back({"foundation.pii.json", renderPackageInterfaceJson(packageInterface)});
    return result;
}

} // namespace

std::optional<PackageExportFormat> parsePackageExportFormat(std::string_view value) {
    if (value == "zig") {
        return PackageExportFormat::Zig;
    }
    if (value == "rust") {
        return PackageExportFormat::Rust;
    }
    if (value == "go-cgo") {
        return PackageExportFormat::GoCgo;
    }
    if (value == "go-dynamic") {
        return PackageExportFormat::GoDynamic;
    }
    if (value == "go-source") {
        return PackageExportFormat::GoSource;
    }
    return std::nullopt;
}

std::optional<PackageExport> generatePackageExport(const FirProgram &program,
                                                   const PackageInterface &packageInterface,
                                                   PackageExportFormat format,
                                                   Diagnostics &diagnostics) {
    if (!validateMappedTypes(packageInterface, format, diagnostics)) {
        return std::nullopt;
    }
    if (format == PackageExportFormat::Zig) {
        return generateZig(packageInterface);
    }
    if (format == PackageExportFormat::Rust) {
        return generateRust(packageInterface);
    }
    if (format == PackageExportFormat::GoCgo) {
        return generateGoCgo(packageInterface);
    }
    if (format == PackageExportFormat::GoDynamic) {
        return generateGoDynamic(packageInterface);
    }
    return generateGoSource(program, packageInterface, diagnostics);
}

} // namespace foundation
