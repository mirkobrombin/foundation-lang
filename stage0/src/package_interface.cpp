#include "foundation/package_interface.hpp"

#include "foundation/codegen.hpp"
#include "foundation/sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <sstream>
#include <tuple>

namespace foundation {
namespace {

std::string quote(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const char byte : value) {
        const auto code = static_cast<unsigned char>(byte);
        if (byte == '"' || byte == '\\')
            out << '\\' << byte;
        else if (byte == '\n')
            out << "\\n";
        else if (byte == '\r')
            out << "\\r";
        else if (byte == '\t')
            out << "\\t";
        else if (code < 0x20U) {
            constexpr char digits[] = "0123456789abcdef";
            out << "\\u00" << digits[code >> 4U] << digits[code & 15U];
        } else
            out << byte;
    }
    out << '"';
    return out.str();
}

template <typename E> std::string name(E value, std::span<const std::string_view> names) {
    return std::string(names[static_cast<std::size_t>(value)]);
}

std::string typeName(PiiTypeKind value) {
    constexpr std::string_view names[]{
        "void",      "i8",       "i16",   "i32",     "i64",    "isize",   "u8",     "u16",
        "u32",       "u64",      "usize", "f32",     "f64",    "bool",    "string", "raw",
        "raw_const", "array",    "slice", "own",     "view",   "edit",    "struct", "enum",
        "contract",  "function", "task",  "channel", "sender", "receiver"};
    return name(value, names);
}
std::string ownershipName(PiiOwnership value) {
    constexpr std::string_view names[]{
        "value",         "borrowed",      "exclusive_borrow", "caller_owned_result",
        "raw_unmanaged", "opaque_borrow", "opaque_transfer"};
    return name(value, names);
}
std::string errorName(PiiErrorConvention value) {
    constexpr std::string_view names[]{"infallible", "status_out", "tagged_result", "option_tag",
                                       "foreign_status"};
    return name(value, names);
}
std::string ecosystemName(PiiEcosystem value) {
    constexpr std::string_view names[]{"foundation", "c", "zig", "rust", "go"};
    return name(value, names);
}
std::string lifetimeName(PiiCallbackLifetime value) {
    constexpr std::string_view names[]{"call_scoped", "retained", "once"};
    return name(value, names);
}
std::string protocolName(PiiCallbackProtocol value) {
    constexpr std::string_view names[]{"direct", "foundation_reactor_v1"};
    return name(value, names);
}
bool scalar(PiiTypeKind value) { return value >= PiiTypeKind::I8 && value <= PiiTypeKind::Bool; }

void typeJson(std::ostream& out, const PiiType& type) {
    out << "{\"kind\":" << quote(typeName(type.kind));
    if (!type.name.empty())
        out << ",\"name\":" << quote(type.name);
    if (type.nullable)
        out << ",\"nullable\":true";
    if (!type.arguments.empty()) {
        out << ",\"arguments\":[";
        for (std::size_t index{}; index < type.arguments.size(); ++index) {
            if (index != 0)
                out << ',';
            typeJson(out, type.arguments[index]);
        }
        out << ']';
    }
    out << '}';
}
void parameterJson(std::ostream& out, const PiiParameter& parameter) {
    out << "{\"name\":" << quote(parameter.name)
        << ",\"ownership\":" << quote(ownershipName(parameter.ownership)) << ",\"type\":";
    typeJson(out, parameter.type);
    out << '}';
}
void handleJson(std::ostream& out, const PiiHandle& handle) {
    out << "{\"identity\":" << quote(handle.identity) << ",\"name\":" << quote(handle.name)
        << ",\"ownership\":" << quote(ownershipName(handle.ownership))
        << ",\"thread_affine\":" << (handle.threadAffine ? "true" : "false") << ",\"type\":";
    typeJson(out, handle.type);
    if (handle.releaseSymbol)
        out << ",\"release_symbol\":" << quote(*handle.releaseSymbol);
    out << '}';
}
void callbackJson(std::ostream& out, const PiiCallback& callback) {
    out << "{\"name\":" << quote(callback.name) << ",\"parameters\":[";
    for (std::size_t index{}; index < callback.parameters.size(); ++index) {
        if (index)
            out << ',';
        parameterJson(out, callback.parameters[index]);
    }
    out << "],\"result\":";
    typeJson(out, callback.result);
    out << ",\"error_convention\":" << quote(errorName(callback.errors))
        << ",\"lifetime\":" << quote(lifetimeName(callback.lifetime))
        << ",\"protocol\":" << quote(protocolName(callback.protocol));
    if (callback.contextHandle)
        out << ",\"context_handle\":" << quote(*callback.contextHandle);
    if (callback.cancelSymbol)
        out << ",\"cancel_symbol\":" << quote(*callback.cancelSymbol);
    out << '}';
}
void functionJson(std::ostream& out, const PiiFunction& function) {
    out << "{\"foundation_name\":" << quote(function.foundationName)
        << ",\"c_symbol\":" << quote(function.cSymbol) << ",\"direction\":"
        << quote(function.direction == PiiDirection::Export ? "export" : "import")
        << ",\"abi\":\"c11\",\"parameters\":[";
    for (std::size_t index{}; index < function.parameters.size(); ++index) {
        if (index)
            out << ',';
        parameterJson(out, function.parameters[index]);
    }
    out << "],\"result\":";
    typeJson(out, function.result);
    out << ",\"result_ownership\":" << quote(ownershipName(function.resultOwnership))
        << ",\"error_convention\":" << quote(errorName(function.errors));
    if (function.handle) {
        out << ",\"handle\":";
        handleJson(out, *function.handle);
    }
    if (function.callback) {
        out << ",\"callback\":";
        callbackJson(out, *function.callback);
    }
    if (function.source) {
        out << ",\"source\":{\"path\":" << quote(function.source->path)
            << ",\"offset\":" << function.source->offset
            << ",\"length\":" << function.source->length << ",\"line\":" << function.source->line
            << ",\"column\":" << function.source->column << '}';
    }
    out << '}';
}
void foreignJson(std::ostream& out, const ForeignProvenance& foreign) {
    out << "{\"ecosystem\":" << quote(ecosystemName(foreign.ecosystem))
        << ",\"identifier\":" << quote(foreign.identifier)
        << ",\"version\":" << quote(foreign.version) << ",\"kind\":" << quote(foreign.kind)
        << ",\"resolver\":" << quote(foreign.resolver) << ",\"digest\":" << quote(foreign.digest)
        << ",\"target\":" << quote(targetPlatformName(foreign.target)) << ",\"abi\":\"c11\"}";
}

PiiEcosystem ecosystem(std::string_view value) {
    if (value == "zig")
        return PiiEcosystem::Zig;
    if (value == "rust")
        return PiiEcosystem::Rust;
    if (value == "go")
        return PiiEcosystem::Go;
    return PiiEcosystem::C;
}

PiiType piiType(const FirProgram& program, const Type& type) {
    PiiType result;
    switch (type.kind) {
    case TypeKind::Void:
        result.kind = PiiTypeKind::Void;
        break;
    case TypeKind::I8:
        result.kind = PiiTypeKind::I8;
        break;
    case TypeKind::I16:
        result.kind = PiiTypeKind::I16;
        break;
    case TypeKind::I32:
        result.kind = PiiTypeKind::I32;
        break;
    case TypeKind::I64:
        result.kind = PiiTypeKind::I64;
        break;
    case TypeKind::Isize:
        result.kind = PiiTypeKind::ISize;
        break;
    case TypeKind::U8:
        result.kind = PiiTypeKind::U8;
        break;
    case TypeKind::U16:
        result.kind = PiiTypeKind::U16;
        break;
    case TypeKind::U32:
        result.kind = PiiTypeKind::U32;
        break;
    case TypeKind::U64:
        result.kind = PiiTypeKind::U64;
        break;
    case TypeKind::Usize:
        result.kind = PiiTypeKind::USize;
        break;
    case TypeKind::F32:
        result.kind = PiiTypeKind::F32;
        break;
    case TypeKind::F64:
        result.kind = PiiTypeKind::F64;
        break;
    case TypeKind::Bool:
        result.kind = PiiTypeKind::Bool;
        break;
    case TypeKind::String:
        result.kind = PiiTypeKind::String;
        break;
    case TypeKind::Raw:
        result.kind = PiiTypeKind::Raw;
        break;
    case TypeKind::RawConst:
        result.kind = PiiTypeKind::RawConst;
        break;
    case TypeKind::Array:
        result.kind = PiiTypeKind::Array;
        break;
    case TypeKind::Slice:
        result.kind = PiiTypeKind::Slice;
        break;
    case TypeKind::Own:
        result.kind = PiiTypeKind::Own;
        break;
    case TypeKind::View:
        result.kind = PiiTypeKind::View;
        break;
    case TypeKind::Edit:
        result.kind = PiiTypeKind::Edit;
        break;
    case TypeKind::Struct:
        result.kind = PiiTypeKind::Struct;
        if (type.declaration < program.structs.size())
            result.name = program.structs[type.declaration].name;
        break;
    case TypeKind::Enum:
        result.kind = PiiTypeKind::Enum;
        if (type.declaration < program.enums.size())
            result.name = program.enums[type.declaration].name;
        break;
    case TypeKind::Contract:
        result.kind = PiiTypeKind::Contract;
        if (type.declaration < program.contracts.size())
            result.name = program.contracts[type.declaration].name;
        break;
    case TypeKind::Function:
        result.kind = PiiTypeKind::Function;
        break;
    case TypeKind::Task:
        result.kind = PiiTypeKind::Task;
        break;
    case TypeKind::Channel:
        result.kind = PiiTypeKind::Channel;
        break;
    case TypeKind::Sender:
        result.kind = PiiTypeKind::Sender;
        break;
    case TypeKind::Receiver:
        result.kind = PiiTypeKind::Receiver;
        break;
    case TypeKind::Invalid:
    case TypeKind::Never:
    case TypeKind::Parameter:
        result.name = "invalid";
        break;
    }
    result.arguments.reserve(type.arguments.size());
    for (const auto& argument : type.arguments) {
        result.arguments.push_back(piiType(program, argument));
    }
    return result;
}

PiiOwnership parameterOwnership(const Type& type) {
    if (type.kind == TypeKind::String)
        return PiiOwnership::Borrowed;
    if (type.kind == TypeKind::View)
        return PiiOwnership::Borrowed;
    if (type.kind == TypeKind::Edit)
        return PiiOwnership::ExclusiveBorrow;
    if (type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) {
        return PiiOwnership::RawUnmanaged;
    }
    return PiiOwnership::Value;
}

PiiType boundaryType(const FirProgram& program, const Type& type) {
    if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
        type.arguments.size() == 1) {
        return piiType(program, type.arguments.front());
    }
    return piiType(program, type);
}

PiiFunction piiFunction(const FirProgram& program, const FirFunction& function) {
    PiiFunction result;
    result.foundationName = function.name;
    result.cSymbol = *function.cSymbol;
    result.direction = function.hasBody ? PiiDirection::Export : PiiDirection::Import;
    result.result = piiType(program, function.returnType);
    if (function.returnType == stringType) {
        result.resultOwnership = PiiOwnership::CallerOwnedResult;
    } else if (function.returnType.kind == TypeKind::Raw ||
               function.returnType.kind == TypeKind::RawConst) {
        result.resultOwnership = PiiOwnership::RawUnmanaged;
    }
    for (const auto local : function.parameters) {
        const auto& value = function.locals[local];
        result.parameters.push_back(
            {value.name, boundaryType(program, value.type), parameterOwnership(value.type)});
    }
    if (function.callback) {
        PiiCallback callback;
        callback.name = result.foundationName + ".completion";
        callback.parameters.push_back({"status", piiType(program, i32Type),
                                       PiiOwnership::Value});
        callback.result = piiType(program, voidType);
        callback.errors = PiiErrorConvention::ForeignStatus;
        callback.lifetime = PiiCallbackLifetime::Once;
        callback.protocol = PiiCallbackProtocol::FoundationReactorV1;
        callback.contextHandle = "foundation.reactor.operation";
        callback.cancelSymbol = function.callbackCancelSymbol;
        result.result = piiType(program, voidType);
        result.resultOwnership = PiiOwnership::Value;
        result.callback = std::move(callback);
    }
    result.source =
        PiiSourceSpan{function.sourcePath, function.sourceSpan.offset, function.sourceSpan.length,
                      function.sourceSpan.line, function.sourceSpan.column};
    return result;
}
} // namespace

bool validateCAbiV1(const PiiType& type, PiiOwnership ownership, bool result, std::string& reason) {
    if (type.name == "invalid") {
        reason = "unresolved type cannot cross C ABI v1";
        return false;
    }
    if (type.kind == PiiTypeKind::Void) {
        if (result && ownership == PiiOwnership::Value)
            return true;
        reason = "C ABI v1 parameters cannot have type void";
        return false;
    }
    if (scalar(type.kind)) {
        if (ownership == PiiOwnership::Value ||
            (!result && ownership == PiiOwnership::ExclusiveBorrow))
            return true;
        reason = "C ABI v1 scalar ownership must be value or an exclusive parameter borrow";
        return false;
    }
    if (type.kind == PiiTypeKind::String) {
        if ((result && ownership == PiiOwnership::CallerOwnedResult) ||
            (!result &&
             (ownership == PiiOwnership::Borrowed || ownership == PiiOwnership::ExclusiveBorrow)))
            return true;
        reason = "C ABI v1 String ownership is invalid";
        return false;
    }
    if (type.kind == PiiTypeKind::Raw || type.kind == PiiTypeKind::RawConst) {
        if (ownership == PiiOwnership::RawUnmanaged)
            return true;
        reason = "C ABI v1 raw pointers require raw_unmanaged ownership";
        return false;
    }
    reason = "type is not supported by C ABI v1";
    return false;
}

std::string renderPackageInterfaceJson(PackageInterface value) {
    std::sort(value.imports.begin(), value.imports.end(), [](const auto& a, const auto& b) {
        return std::tie(a.cSymbol, a.foundationName) < std::tie(b.cSymbol, b.foundationName);
    });
    std::sort(value.exports.begin(), value.exports.end(), [](const auto& a, const auto& b) {
        return std::tie(a.cSymbol, a.foundationName) < std::tie(b.cSymbol, b.foundationName);
    });
    std::sort(value.foreign.begin(), value.foreign.end(), [](const auto& a, const auto& b) {
        return std::tie(a.ecosystem, a.identifier, a.version, a.kind, a.resolver, a.digest,
                        a.target) < std::tie(b.ecosystem, b.identifier, b.version, b.kind,
                                             b.resolver, b.digest, b.target);
    });
    std::ostringstream out;
    out << "{\"format\":" << value.format << ",\"abi_major\":" << value.abiMajor
        << ",\"abi_minor\":" << value.abiMinor << ",\"package\":" << quote(value.package)
        << ",\"version\":" << quote(value.version.string())
        << ",\"sdk\":" << quote(value.sdk.string()) << ",\"library\":" << quote(value.library)
        << ",\"soversion\":" << value.soVersion
        << ",\"target\":" << quote(targetPlatformName(value.target)) << ",\"imports\":[";
    for (std::size_t index{}; index < value.imports.size(); ++index) {
        if (index)
            out << ',';
        functionJson(out, value.imports[index]);
    }
    out << "],\"exports\":[";
    for (std::size_t index{}; index < value.exports.size(); ++index) {
        if (index)
            out << ',';
        functionJson(out, value.exports[index]);
    }
    out << "],\"foreign\":[";
    for (std::size_t index{}; index < value.foreign.size(); ++index) {
        if (index)
            out << ',';
        foreignJson(out, value.foreign[index]);
    }
    out << "]}";
    const auto canonical = out.str();
    return canonical.substr(0, canonical.size() - 1) +
           ",\"canonical_sha256\":" + quote("sha256:" + sha256Hex(canonical)) + "}";
}

std::optional<PackageInterface> buildPackageInterface(const FirProgram& source,
                                                      const PackageManifest& manifest,
                                                      const PackageLock& lock,
                                                      Diagnostics& diagnostics) {
    const auto program = specializePackageInterface(source, manifest.name);
    PackageInterface result;
    result.package = manifest.name;
    result.version = manifest.version;
    result.sdk = manifest.sdk;
    result.library = manifest.nativeName.value_or(manifest.name);
    result.soVersion = manifest.nativeSOVersion.value_or(0);
    result.target = lock.target;
    for (const auto& function : program.functions) {
        if (!function.cSymbol.has_value())
            continue;
        auto lowered = piiFunction(program, function);
        const auto sourcePath = std::filesystem::path(lowered.source->path);
        const auto parent = std::find(sourcePath.begin(), sourcePath.end(), "..");
        if (sourcePath.has_root_path() || parent != sourcePath.end()) {
            diagnostics.error("FDN2121", "package interface source paths must be relative",
                              function.sourceSpan);
            continue;
        }
        lowered.source->path = sourcePath.lexically_normal().generic_string();
        for (std::size_t index = 0; index < lowered.parameters.size(); ++index) {
            std::string reason;
            if (!validateCAbiV1(lowered.parameters[index].type, lowered.parameters[index].ownership,
                                false, reason)) {
                diagnostics.error("FDN2120",
                                  "C ABI v1 cannot lower parameter " +
                                      lowered.parameters[index].name + " of " +
                                      lowered.foundationName + ": " + reason,
                                  function.sourceSpan);
            }
        }
        std::string reason;
        if (!validateCAbiV1(lowered.result, lowered.resultOwnership, true, reason)) {
            diagnostics.error("FDN2120",
                              "C ABI v1 cannot lower result of " + lowered.foundationName + ": " +
                                  reason,
                              function.sourceSpan);
        }
        if (function.hasBody && function.packageName == manifest.name) {
            result.exports.push_back(std::move(lowered));
        } else if (!function.hasBody) {
            result.imports.push_back(std::move(lowered));
        }
    }
    for (const auto& foreign : lock.foreign) {
        result.foreign.push_back({ecosystem(foreign.ecosystem), foreign.identifier, foreign.version,
                                  foreign.kind, foreign.resolver, foreign.digest, lock.target,
                                  PiiAbi::C11});
    }
    if (diagnostics.hasErrors())
        return std::nullopt;
    const auto bySymbol = [](const auto& left, const auto& right) {
        return std::tie(left.cSymbol, left.foundationName) <
               std::tie(right.cSymbol, right.foundationName);
    };
    std::sort(result.imports.begin(), result.imports.end(), bySymbol);
    std::sort(result.exports.begin(), result.exports.end(), bySymbol);
    std::sort(result.foreign.begin(), result.foreign.end(),
              [](const auto& left, const auto& right) {
                  return std::tie(left.ecosystem, left.identifier, left.version, left.kind,
                                  left.resolver, left.digest) <
                         std::tie(right.ecosystem, right.identifier, right.version, right.kind,
                                  right.resolver, right.digest);
              });
    return result;
}
} // namespace foundation
