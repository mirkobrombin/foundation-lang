#ifndef FOUNDATION_PACKAGE_INTERFACE_HPP
#define FOUNDATION_PACKAGE_INTERFACE_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"
#include "foundation/package.hpp"

#include <optional>
#include <string>
#include <vector>

namespace foundation {

enum class PiiDirection { Export, Import };
enum class PiiEcosystem { Foundation, C, Zig, Rust, Go };
enum class PiiAbi { C11 };
enum class PiiTypeKind {
    Void,
    I8,
    I16,
    I32,
    I64,
    ISize,
    U8,
    U16,
    U32,
    U64,
    USize,
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
    Struct,
    Enum,
    Contract,
    Function,
    Task,
    Channel,
    Sender,
    Receiver,
};
enum class PiiOwnership {
    Value,
    Borrowed,
    ExclusiveBorrow,
    CallerOwnedResult,
    RawUnmanaged,
    OpaqueBorrow,
    OpaqueTransfer,
};
enum class PiiErrorConvention { Infallible, StatusOut, TaggedResult, OptionTag, ForeignStatus };
enum class PiiCallbackLifetime { CallScoped, Retained, Once };
enum class PiiCallbackProtocol { Direct, FoundationReactorV1 };

struct PiiType {
    PiiTypeKind kind{PiiTypeKind::Void};
    std::string name;
    std::string abi;
    bool nullable{};
    std::vector<PiiType> arguments;
};

struct PiiParameter {
    std::string name;
    PiiType type;
    PiiOwnership ownership{PiiOwnership::Value};
};

struct PiiLayoutField {
    std::string foundationName;
    std::string cName;
    PiiType type;
};

struct PiiStructLayout {
    std::string foundationName;
    std::string cName;
    std::vector<PiiLayoutField> fields;
};

struct PiiSourceSpan {
    std::string path;
    std::size_t offset{};
    std::size_t length{};
    std::size_t line{1};
    std::size_t column{1};
};

struct PiiHandle {
    std::string identity;
    std::string name;
    PiiType type;
    PiiOwnership ownership{PiiOwnership::OpaqueBorrow};
    std::optional<std::string> releaseSymbol;
    bool threadAffine{};
};

struct PiiCallback {
    std::string name;
    std::vector<PiiParameter> parameters;
    PiiType result;
    PiiErrorConvention errors{PiiErrorConvention::Infallible};
    PiiCallbackLifetime lifetime{PiiCallbackLifetime::CallScoped};
    PiiCallbackProtocol protocol{PiiCallbackProtocol::Direct};
    std::optional<std::string> contextHandle;
    std::optional<std::string> cancelSymbol;
};

struct PiiFunction {
    std::string foundationName;
    std::string cSymbol;
    PiiDirection direction{PiiDirection::Export};
    PiiAbi abi{PiiAbi::C11};
    std::vector<PiiParameter> parameters;
    PiiType result;
    PiiOwnership resultOwnership{PiiOwnership::Value};
    PiiErrorConvention errors{PiiErrorConvention::Infallible};
    std::optional<PiiHandle> handle;
    std::optional<PiiCallback> callback;
    std::optional<PiiSourceSpan> source;
};

struct ForeignProvenance {
    PiiEcosystem ecosystem{PiiEcosystem::C};
    std::string identifier;
    std::string version;
    std::string kind;
    std::string resolver;
    std::string digest;
    TargetPlatform target{TargetPlatform::Linux};
    PiiAbi abi{PiiAbi::C11};
};

struct PiiLinkLibrary {
    std::string name;
    std::optional<TargetPlatform> target;
};

struct PackageInterface {
    unsigned int format{1};
    unsigned int abiMajor{1};
    unsigned int abiMinor{2};
    std::string package;
    PackageVersion version;
    PackageRequirement sdk;
    std::string library;
    unsigned int soVersion{};
    TargetPlatform target{TargetPlatform::Linux};
    std::vector<PiiLinkLibrary> links;
    std::vector<PiiStructLayout> layouts;
    std::vector<PiiFunction> imports;
    std::vector<PiiFunction> exports;
    std::vector<ForeignProvenance> foreign;
    std::string canonicalSha256;
};

[[nodiscard]] bool validateCAbiV1(const PiiType& type, PiiOwnership ownership, bool result,
                                  std::string& reason);
[[nodiscard]] std::string renderPackageInterfaceJson(PackageInterface value);
[[nodiscard]] std::optional<PackageInterface> buildPackageInterface(const FirProgram& program,
                                                                    const PackageManifest& manifest,
                                                                    const PackageLock& lock,
                                                                    Diagnostics& diagnostics);

} // namespace foundation

#endif
