#include "foundation/package.hpp"

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
sdk ^0.1.0
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
}

void locksRoundTripCanonically() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    foundation::PackageLock lock;
    lock.rootName = "sample.app";
    lock.rootVersion = *foundation::parsePackageVersion("1.0.0");
    lock.target = foundation::TargetPlatform::Linux;
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
    manifestsRejectAmbiguousInput();
    manifestsRequireSeparatedTestSources();
    locksRoundTripCanonically();
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
