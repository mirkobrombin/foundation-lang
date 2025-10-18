#include "foundation/package.hpp"
#include "foundation/sha256.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

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
    return std::any_of(errors.begin(), errors.end(), [&](const auto &error) {
        return error.code == code;
    });
}

foundation::PackageRequirement requirement(std::string_view value) {
    return *foundation::parsePackageRequirement(value);
}

foundation::PackageManifest manifest(std::string name, std::string_view version) {
    foundation::PackageManifest result;
    result.name = std::move(name);
    result.version = *foundation::parsePackageVersion(version);
    result.sdk = requirement("^0.1.0");
    result.source = "src";
    return result;
}

foundation::PackageDependency dependency(std::string name, std::string_view version,
                                         std::optional<foundation::TargetPlatform> target = {}) {
    return {std::move(name), requirement(version), foundation::PackageLocationKind::Registry,
            "default", target};
}

foundation::PackageCandidate candidate(foundation::PackageManifest value,
                                       std::string digest) {
    digest = "sha256:" + foundation::sha256Hex(digest);
    return {std::move(value), std::move(digest),
            foundation::PackageLocationKind::Registry, "default", {}};
}

void resolutionBacktracksAndIsDeterministic() {
    auto root = manifest("sample.app", "1.0.0");
    root.dependencies.push_back(dependency("sample.a", "*"));
    root.dependencies.push_back(dependency("sample.b", "*"));
    auto a1 = manifest("sample.a", "1.0.0");
    a1.dependencies.push_back(dependency("sample.c", "^1.0.0"));
    auto a2 = manifest("sample.a", "2.0.0");
    a2.dependencies.push_back(dependency("sample.c", "^2.0.0"));
    auto b = manifest("sample.b", "1.0.0");
    b.dependencies.push_back(dependency("sample.c", "^1.0.0"));
    const auto c1 = manifest("sample.c", "1.5.0");
    const auto c2 = manifest("sample.c", "2.5.0");
    std::vector<foundation::PackageCandidate> catalog{
        candidate(c2, "sha256:c2"), candidate(a2, "sha256:a2"),
        candidate(b, "sha256:b"),   candidate(c1, "sha256:c1"),
        candidate(a1, "sha256:a1"),
    };
    const auto sdk = *foundation::parsePackageVersion("0.1.0");
    const auto first = foundation::resolvePackageGraph(
        "foundation.package", root, sdk, foundation::TargetPlatform::Linux, catalog);
    std::reverse(catalog.begin(), catalog.end());
    const auto second = foundation::resolvePackageGraph(
        "foundation.package", root, sdk, foundation::TargetPlatform::Linux, catalog);
    expect(first.value.has_value() && second.value.has_value(), "compatible graph resolves");
    if (!first.value.has_value() || !second.value.has_value()) {
        return;
    }
    expect(foundation::renderPackageLock(first.value->lock) ==
               foundation::renderPackageLock(second.value->lock),
           "catalog order does not change the lock");
    const auto lock = foundation::renderPackageLock(first.value->lock);
    expect(lock.find("package sample.a 1.0.0") != std::string::npos &&
               lock.find("package sample.c 1.5.0") != std::string::npos,
           "resolver backtracks from the incompatible highest version");
}

void targetDependenciesAndConflictsAreChecked() {
    auto root = manifest("sample.app", "1.0.0");
    root.dependencies.push_back(
        dependency("sample.linux", "1.0.0", foundation::TargetPlatform::Linux));
    root.dependencies.push_back(
        dependency("sample.windows", "1.0.0", foundation::TargetPlatform::Windows));
    std::vector<foundation::PackageCandidate> catalog{
        candidate(manifest("sample.linux", "1.0.0"), "sha256:linux")};
    const auto sdk = *foundation::parsePackageVersion("0.1.0");
    const auto linux = foundation::resolvePackageGraph(
        "foundation.package", root, sdk, foundation::TargetPlatform::Linux, catalog);
    expect(linux.value.has_value() && linux.value->lock.packages.size() == 1,
           "inactive target dependency is not resolved");

    auto conflictRoot = manifest("sample.conflict", "1.0.0");
    conflictRoot.dependencies.push_back(dependency("sample.missing", "^2.0.0"));
    const auto conflict = foundation::resolvePackageGraph(
        "foundation.package", conflictRoot, sdk, foundation::TargetPlatform::Linux, catalog);
    expect(hasCode(conflict.errors, "FDN4052") &&
               conflict.errors.front().message.find("sample.conflict -> sample.missing") !=
                   std::string::npos,
           "conflict reports the requirement path");
}

void cyclesAndSdkMismatchAreRejected() {
    auto root = manifest("sample.app", "1.0.0");
    root.dependencies.push_back(dependency("sample.a", "*"));
    auto a = manifest("sample.a", "1.0.0");
    a.dependencies.push_back(dependency("sample.b", "*"));
    auto b = manifest("sample.b", "1.0.0");
    b.dependencies.push_back(dependency("sample.a", "*"));
    std::vector<foundation::PackageCandidate> catalog{
        candidate(a, "sha256:a"), candidate(b, "sha256:b")};
    const auto sdk = *foundation::parsePackageVersion("0.1.0");
    const auto cycle = foundation::resolvePackageGraph(
        "foundation.package", root, sdk, foundation::TargetPlatform::Linux, catalog);
    expect(hasCode(cycle.errors, "FDN4054"), "dependency cycle has a stable diagnostic");

    root.sdk = requirement("^2.0.0");
    const auto mismatch = foundation::resolvePackageGraph(
        "foundation.package", root, sdk, foundation::TargetPlatform::Linux, catalog);
    expect(hasCode(mismatch.errors, "FDN4050"), "SDK mismatch has a stable diagnostic");
}

void cyclesAcrossPreviouslySelectedPackagesAreRejected() {
    auto root = manifest("sample.app", "1.0.0");
    root.dependencies.push_back(dependency("sample.a", "*"));
    root.dependencies.push_back(dependency("sample.b", "*"));
    auto a = manifest("sample.a", "1.0.0");
    a.dependencies.push_back(dependency("sample.c", "*"));
    auto b = manifest("sample.b", "1.0.0");
    b.dependencies.push_back(dependency("sample.a", "*"));
    auto c = manifest("sample.c", "1.0.0");
    c.dependencies.push_back(dependency("sample.b", "*"));
    std::vector<foundation::PackageCandidate> catalog{
        candidate(a, "sha256:a"), candidate(b, "sha256:b"),
        candidate(c, "sha256:c")};
    const auto resolved = foundation::resolvePackageGraph(
        "foundation.package", root, *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, catalog);
    expect(hasCode(resolved.errors, "FDN4054"),
           "cycle among previously selected packages is rejected");
}

void malformedCatalogCandidateIsRejected() {
    auto root = manifest("sample.app", "1.0.0");
    root.dependencies.push_back(dependency("sample.lib", "*"));
    std::vector<foundation::PackageCandidate> catalog{{
        manifest("sample.lib", "1.0.0"), "sha256:broken",
        foundation::PackageLocationKind::Registry, "default", {}}};
    const auto resolved = foundation::resolvePackageGraph(
        "foundation.package", root, *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, catalog);
    expect(hasCode(resolved.errors, "FDN4051"),
           "malformed catalog digest has a stable diagnostic");
}

} // namespace

int runPackageResolverTests() {
    resolutionBacktracksAndIsDeterministic();
    targetDependenciesAndConflictsAreChecked();
    cyclesAndSdkMismatchAreRejected();
    cyclesAcrossPreviouslySelectedPackagesAreRejected();
    malformedCatalogCandidateIsRejected();
    return failures;
}
