#include "foundation/package.hpp"
#include "foundation/package_interface.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures{};

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool hasCode(const std::vector<foundation::PackageError> &errors, std::string_view code) {
    for (const auto &error : errors) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

void versionsFollowSemanticOrdering() {
    const auto stable = foundation::parsePackageVersion("1.2.3");
    const auto prerelease = foundation::parsePackageVersion("1.2.3-rc.1+linux");
    const auto next = foundation::parsePackageVersion("1.3.0");
    expect(stable.has_value() && prerelease.has_value() && next.has_value(),
           "semantic versions parse");
    if (!stable.has_value() || !prerelease.has_value() || !next.has_value()) {
        return;
    }
    expect(*prerelease < *stable && *stable < *next,
           "prerelease and core versions have semantic order");
    expect(prerelease->string() == "1.2.3-rc.1+linux",
           "semantic version rendering preserves identifiers");
    expect(!foundation::parsePackageVersion("01.2.3").has_value() &&
               !foundation::parsePackageVersion("1.2").has_value() &&
               !foundation::parsePackageVersion("1.2.3-01").has_value(),
           "non-canonical semantic versions are rejected");
}

void requirementsUseBoundedRanges() {
    const auto caret = foundation::parsePackageRequirement("^1.2.3");
    const auto zeroCaret = foundation::parsePackageRequirement("^0.2.3");
    const auto tilde = foundation::parsePackageRequirement("~1.2.3");
    expect(caret.has_value() && zeroCaret.has_value() && tilde.has_value(),
           "bounded requirements parse");
    if (!caret.has_value() || !zeroCaret.has_value() || !tilde.has_value()) {
        return;
    }
    expect(caret->accepts(*foundation::parsePackageVersion("1.9.0")) &&
               !caret->accepts(*foundation::parsePackageVersion("2.0.0")),
           "caret requirement stops at the next major version");
    expect(zeroCaret->accepts(*foundation::parsePackageVersion("0.2.9")) &&
               !zeroCaret->accepts(*foundation::parsePackageVersion("0.3.0")),
           "zero-major caret requirement stops at the next minor version");
    expect(tilde->accepts(*foundation::parsePackageVersion("1.2.9")) &&
               !tilde->accepts(*foundation::parsePackageVersion("1.3.0")),
           "tilde requirement stops at the next minor version");
}

void manifestsRoundTripCanonically() {
    constexpr std::string_view source = R"(format foundation.package/v1
name sample.app
version 1.0.0
language 1
fcs strict
fcs_rule FCS1001 error
fcs_rule FCS3001 off
source src
test_source tests
dependency sample.local 2.0.0 path "../local package"
dependency sample.platform ~3.1.0 registry default scope test target linux
)";
    const auto parsed = foundation::parsePackageManifest("foundation.package", source);
    expect(parsed.value.has_value() && parsed.errors.empty(), "valid package manifest parses");
    if (!parsed.value.has_value()) {
        return;
    }
    const auto rendered = foundation::renderPackageManifest(*parsed.value);
    const auto repeated = foundation::parsePackageManifest("foundation.package", rendered);
    expect(repeated.value.has_value() &&
               foundation::renderPackageManifest(*repeated.value) == rendered,
           "package manifest serialization is canonical");
    expect(rendered.find("sample.local 2.0.0 path \"../local package\"") !=
               std::string::npos,
           "quoted dependency locations round trip");
    expect(rendered.find("test_source tests") != std::string::npos &&
               rendered.find("target linux scope test") != std::string::npos,
           "test sources and dependency scopes render canonically");
    expect(rendered.find("fcs strict") != std::string::npos &&
               parsed.value->codeStandard == foundation::CodeStandardProfile::Strict,
           "FCS profile renders canonically");
    expect(rendered.find("fcs_rule FCS1001 error") != std::string::npos &&
               rendered.find("fcs_rule FCS3001 off") != std::string::npos &&
               parsed.value->codeStandardRules.size() == 2,
           "FCS rule severities render canonically");
    expect(parsed.value->languageExplicit && parsed.value->language == 1 &&
               parsed.value->sdk.accepts(*foundation::parsePackageVersion("99.0.0")),
           "language 1 does not bind a package to one toolchain release");

    constexpr std::string_view legacy = R"(format foundation.package/v1
name sample.legacy
version 1.0.0
sdk ^0.1.0
source src
)";
    const auto legacyParsed = foundation::parsePackageManifest("foundation.package", legacy);
    expect(legacyParsed.value.has_value() &&
               foundation::renderPackageManifest(*legacyParsed.value) == legacy,
           "implicit Standard profile preserves legacy manifest bytes");
}

void manifestsSelectOneCompatibilityLevel() {
    constexpr std::string_view unsupported = R"(format foundation.package/v1
name sample.future
version 1.0.0
language 2
source src
)";
    constexpr std::string_view ambiguous = R"(format foundation.package/v1
name sample.ambiguous
version 1.0.0
language 1
sdk ^0.1.0
source src
)";
    constexpr std::string_view missing = R"(format foundation.package/v1
name sample.missing
version 1.0.0
source src
)";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", unsupported).errors,
                   "FDN4007"),
           "unsupported language levels are rejected");
    expect(hasCode(foundation::parsePackageManifest("foundation.package", ambiguous).errors,
                   "FDN4007"),
           "language and SDK compatibility cannot both be selected");
    expect(hasCode(foundation::parsePackageManifest("foundation.package", missing).errors,
                   "FDN4007"),
           "package manifests require one compatibility selection");
}

void manifestsRequireSeparatedTestSources() {
    constexpr std::string_view missing = R"(format foundation.package/v1
name sample.app
version 1.0.0
sdk ^0.1.0
source src
dependency sample.test 1.0.0 registry default scope test
)";
    constexpr std::string_view overlapping = R"(format foundation.package/v1
name sample.app
version 1.0.0
sdk ^0.1.0
source src
test_source src/tests
)";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", missing).errors,
                   "FDN4013"),
           "test dependencies require test_source");
    expect(hasCode(foundation::parsePackageManifest("foundation.package", overlapping).errors,
                   "FDN4013"),
           "production and test source roots cannot overlap");
}

void manifestsRejectInvalidCodeStandardRules() {
    constexpr std::string_view duplicate = R"(format foundation.package/v1
name sample.fcs
version 1.0.0
sdk ^0.1.0
fcs_rule FCS1001 warning
fcs_rule FCS1001 error
source src
)";
    constexpr std::string_view invalid = R"(format foundation.package/v1
name sample.fcs
version 1.0.0
sdk ^0.1.0
fcs_rule FDN1001 warning
source src
)";
    constexpr std::string_view unknown = R"(format foundation.package/v1
name sample.fcs
version 1.0.0
sdk ^0.1.0
fcs_rule FCS9999 warning
source src
)";
    constexpr std::string_view internal = R"(format foundation.package/v1
name sample.fcs
version 1.0.0
sdk ^0.1.0
fcs_rule FCS9001 off
source src
)";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", duplicate).errors,
                   "FDN4014") &&
               hasCode(foundation::parsePackageManifest("foundation.package", invalid).errors,
                   "FDN4014") &&
               hasCode(foundation::parsePackageManifest("foundation.package", unknown).errors,
                       "FDN4014") &&
               hasCode(foundation::parsePackageManifest("foundation.package", internal).errors,
                       "FDN4014"),
           "FCS rule settings reject duplicate, unknown, and internal codes");
}

void nativeManifestsRoundTripAndValidatePaths() {
    constexpr std::string_view source = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
native_soversion 2
native_link dl target linux
native_link dl target macos
native_link fuse
foreign c libfuse 2.9.9 path native/libfuse abi c/v1
)";
    const auto parsed = foundation::parsePackageManifest("foundation.package", source);
    expect(parsed.value.has_value() &&
               foundation::renderPackageManifest(*parsed.value) == source,
           "native manifest metadata round trips canonically");
    const auto absolute = std::string(source) +
                          "foreign c absolute 1.0.0 path /tmp/native abi c/v1\n";
    const auto parent = std::string(source) +
                        "foreign c parent 1.0.0 path ../native abi c/v1\n";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", absolute).errors,
                   "FDN4015") &&
               hasCode(foundation::parsePackageManifest("foundation.package", parent).errors,
                       "FDN4015"),
           "foreign path resolvers cannot be absolute or escape the package");
    constexpr std::string_view reserved = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
foreign c registry-lib 1.0.0 registry default abi c/v1
foreign c system-lib 1.0.0 system local abi c/v1
)";
    expect(foundation::parsePackageManifest("foundation.package", reserved).value.has_value(),
           "reserved registry and system provenance remains parseable");

    constexpr std::string_view duplicateLink = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
native_link m target linux
native_link m target linux
)";
    constexpr std::string_view overlappingLink = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
native_link m
native_link m target linux
)";
    constexpr std::string_view invalidLink = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
native_link -lm
)";
    constexpr std::string_view orphanLink = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_link m
)";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", duplicateLink).errors,
                   "FDN4015") &&
               hasCode(foundation::parsePackageManifest("foundation.package",
                                                        overlappingLink)
                           .errors,
                       "FDN4015") &&
               hasCode(foundation::parsePackageManifest("foundation.package", invalidLink).errors,
                       "FDN4015") &&
               hasCode(foundation::parsePackageManifest("foundation.package", orphanLink).errors,
                       "FDN4015"),
           "native links reject overlapping duplicates, raw linker flags, and non-native packages");
}

void manifestsRejectAmbiguousInput() {
    constexpr std::string_view source = R"(format foundation.package/v1
name sample.app
version 1.0.0
sdk ^0.1.0
source ../src
dependency sample.lib 1.0.0 registry default
dependency sample.lib 1.1.0 registry default
unknown value
)";
    const auto parsed = foundation::parsePackageManifest("foundation.package", source);
    expect(!parsed.value.has_value(), "invalid package manifest has no value");
    expect(hasCode(parsed.errors, "FDN4008") && hasCode(parsed.errors, "FDN4011") &&
               hasCode(parsed.errors, "FDN4012"),
           "manifest errors identify traversal, duplicates, and unknown directives");

    constexpr std::string_view profile = R"(format foundation.package/v1
name sample.app
version 1.0.0
sdk ^0.1.0
fcs custom
fcs strict
source src
)";
    expect(hasCode(foundation::parsePackageManifest("foundation.package", profile).errors,
                   "FDN4014"),
           "manifest rejects invalid or duplicate FCS profiles");
}

void locksRoundTripCanonically() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    foundation::PackageLock lock;
    lock.rootName = "sample.app";
    lock.rootVersion = *foundation::parsePackageVersion("1.0.0");
    lock.target = foundation::TargetPlatform::Linux;
    lock.nativeLibrary = foundation::LockedNativeLibrary{
        "sample_native", 2, std::string(digest)};
    lock.foreign.push_back({"c", "libfuse", "2.9.9", "path", "native/libfuse",
                            std::string(digest)});
    lock.packages.push_back({"sample.lib", *foundation::parsePackageVersion("2.0.0"),
                             std::string(digest), foundation::PackageLocationKind::Registry,
                             "default"});
    lock.edges.push_back(
        {"sample.app", "sample.lib", foundation::PackageDependencyScope::Test});
    const auto rendered = foundation::renderPackageLock(lock);
    const auto parsed = foundation::parsePackageLock("foundation.lock", rendered);
    expect(parsed.value.has_value() && parsed.errors.empty(), "valid package lock parses");
    expect(parsed.value.has_value() && foundation::renderPackageLock(*parsed.value) == rendered,
           "package lock serialization is canonical");
    expect(rendered.find("edge sample.app sample.lib scope test") != std::string::npos,
           "test dependency scope is retained in the lock");
    expect(rendered.find("native c sample_native 2 sha256:") != std::string::npos &&
               rendered.find("foreign c libfuse 2.9.9 path native/libfuse abi c/v1 sha256:") !=
                   std::string::npos,
           "native and foreign lock metadata is retained");

    foundation::PackageLock legacy;
    legacy.rootName = "sample.legacy";
    legacy.rootVersion = *foundation::parsePackageVersion("1.0.0");
    legacy.target = foundation::TargetPlatform::Linux;
    expect(foundation::renderPackageLock(legacy) ==
               "format foundation.lock/v1\nroot sample.legacy 1.0.0\ntarget linux\n",
           "legacy lock bytes remain unchanged without native metadata");
}

void packageInterfacesRenderCanonically() {
    foundation::PackageInterface packageInterface;
    packageInterface.package = "sample.native";
    packageInterface.version = *foundation::parsePackageVersion("1.0.0");
    packageInterface.sdk = *foundation::parsePackageRequirement("^0.1.0");
    packageInterface.library = "sample_native";
    packageInterface.target = foundation::TargetPlatform::Linux;
    packageInterface.links.push_back({"m", foundation::TargetPlatform::Linux});
    packageInterface.foreign.push_back(
        {foundation::PiiEcosystem::C, "libfuse", "2.9.9", "path", "native/libfuse",
         "sha256:test", foundation::TargetPlatform::Linux, foundation::PiiAbi::C11});
    const auto first = foundation::renderPackageInterfaceJson(packageInterface);
    const auto second = foundation::renderPackageInterfaceJson(packageInterface);
    expect(first == second && first.find("\"abi_minor\":3") != std::string::npos &&
               first.find("\"language\":1") != std::string::npos &&
               first.find("\"links\":[{\"name\":\"m\",\"target\":\"linux\"}]") !=
                   std::string::npos &&
               first.find("\"kind\":\"path\"") != std::string::npos &&
               first.find("\"canonical_sha256\":\"sha256:") != std::string::npos,
           "package interface JSON is canonical and preserves resolver provenance");

    foundation::PiiType invalidFunction;
    invalidFunction.kind = foundation::PiiTypeKind::Function;
    invalidFunction.abi = "c11";
    foundation::PiiType functionResult;
    functionResult.kind = foundation::PiiTypeKind::I32;
    foundation::PiiType functionParameter;
    functionParameter.kind = foundation::PiiTypeKind::Struct;
    invalidFunction.arguments = {functionResult, functionParameter};
    std::string reason;
    expect(!foundation::validateCAbiV1(invalidFunction, foundation::PiiOwnership::Value,
                                       false, reason),
           "C function pointer validation rejects unsupported nested parameters");
}

void locksRejectIncoherentGraphs() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const auto prefix = std::string{"format foundation.lock/v1\n"}
                        + "root sample.app 1.0.0\n"
                        + "target linux\n"
                        + "package sample.lib 1.0.0 " + std::string(digest)
                        + " registry default\n";
    const auto unreachable = foundation::parsePackageLock("foundation.lock", prefix);
    expect(hasCode(unreachable.errors, "FDN4027"),
           "locked packages must be reachable from the root");

    const auto duplicate = foundation::parsePackageLock(
        "foundation.lock", prefix + "edge sample.app sample.lib\n"
                               + "edge sample.app sample.lib\n");
    expect(hasCode(duplicate.errors, "FDN4027"),
           "duplicate locked package edges are rejected");

    const auto unknown = foundation::parsePackageLock(
        "foundation.lock", prefix + "edge sample.unknown sample.lib\n");
    expect(hasCode(unknown.errors, "FDN4027"),
           "locked edges cannot name unknown parents");

    const auto transitiveTest = foundation::parsePackageLock(
        "foundation.lock", prefix + "edge sample.lib sample.lib scope test\n");
    expect(hasCode(transitiveTest.errors, "FDN4027"),
           "test-scoped lock edges can originate only at the root");
}

} // namespace

int runPackageCacheTests();
int runPackageCatalogTests();
int runPackageLockTests();
int runPackageProjectTests();
int runPackageRegistryTests();
int runSha256Tests();
int runPackageResolverTests();
int runPackageSourceTests();

int main() {
    versionsFollowSemanticOrdering();
    requirementsUseBoundedRanges();
    manifestsRoundTripCanonically();
    manifestsSelectOneCompatibilityLevel();
    manifestsRejectAmbiguousInput();
    manifestsRequireSeparatedTestSources();
    manifestsRejectInvalidCodeStandardRules();
    nativeManifestsRoundTripAndValidatePaths();
    locksRoundTripCanonically();
    packageInterfacesRenderCanonically();
    locksRejectIncoherentGraphs();
    failures += runPackageCacheTests();
    failures += runPackageCatalogTests();
    failures += runPackageLockTests();
    failures += runPackageProjectTests();
    failures += runPackageRegistryTests();
    failures += runSha256Tests();
    failures += runPackageResolverTests();
    failures += runPackageSourceTests();
    if (failures != 0) {
        std::cerr << failures << " package assertions failed\n";
        return 1;
    }
    std::cout << "package tests passed\n";
    return 0;
}
