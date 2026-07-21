#include "foundation/package.hpp"

#include <filesystem>
#include <fstream>
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

struct Fixture {
    std::filesystem::path root;
    std::filesystem::path project;
    std::filesystem::path registry;

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-catalog-test";
        project = root / "project";
        registry = root / "registry";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(project / "src");
        std::filesystem::create_directories(registry);
    }

    ~Fixture() { std::filesystem::remove_all(root); }

    void package(const std::filesystem::path &directory, std::string_view name,
                 std::string_view version, std::string_view dependencies = {}) const {
        std::filesystem::create_directories(directory / "src");
        std::ofstream manifest(directory / "foundation.package", std::ios::binary);
        manifest << "format foundation.package/v1\n"
                 << "name " << name << '\n'
                 << "version " << version << '\n'
                 << "sdk ^0.1.0\n"
                 << "source src\n"
                 << dependencies;
        std::ofstream source(directory / "src" / "main.fn", std::ios::binary);
        source << "package " << name << '\n';
    }
};

void catalogsCombineRegistryAndPathDependencies() {
    Fixture fixture;
    fixture.package(fixture.project, "sample.app", "1.0.0",
                    "dependency sample.remote ^1.0.0 registry default\n"
                    "dependency sample.local 2.0.0 path ../local\n");
    fixture.package(fixture.registry / "sample.remote" / "1.0.0", "sample.remote",
                    "1.0.0",
                    "dependency sample.transitive 1.0.0 registry default\n");
    fixture.package(fixture.registry / "sample.remote" / "2.0.0", "sample.remote",
                    "2.0.0");
    fixture.package(fixture.registry / "sample.transitive" / "1.0.0",
                    "sample.transitive", "1.0.0");
    fixture.package(fixture.root / "local", "sample.local", "2.0.0");

    const auto manifest =
        foundation::readPackageManifest(fixture.project / "foundation.package");
    const std::vector<foundation::PackageRegistryRoot> registries{
        {"default", fixture.registry}};
    const auto resolved = foundation::resolveProjectPackages(
        fixture.project / "foundation.package", *manifest.value,
        *foundation::parsePackageVersion("0.1.0"), foundation::TargetPlatform::Linux,
        registries);
    expect(resolved.value.has_value(), "registry and path package graph resolves");
    if (!resolved.value.has_value()) {
        return;
    }
    const auto lock = foundation::renderPackageLock(resolved.value->lock);
    expect(lock.find("package sample.remote 1.0.0") != std::string::npos &&
               lock.find("package sample.transitive 1.0.0") != std::string::npos &&
               lock.find("package sample.local 2.0.0") != std::string::npos,
           "resolved lock contains compatible registry, transitive, and path packages");
    expect(lock.find("path ../local") != std::string::npos,
           "path dependency identity is normalized relative to the root project");
}

void catalogsRejectMissingAdaptersAndPathIdentityMismatch() {
    Fixture fixture;
    fixture.package(fixture.project, "sample.app", "1.0.0",
                    "dependency sample.remote 1.0.0 registry missing\n");
    auto manifest = foundation::readPackageManifest(fixture.project / "foundation.package");
    const auto missing = foundation::resolveProjectPackages(
        fixture.project / "foundation.package", *manifest.value,
        *foundation::parsePackageVersion("0.1.0"), foundation::TargetPlatform::Linux, {});
    expect(hasCode(missing.errors, "FDN4052"),
           "registry dependency without an adapter reports a resolution conflict");

    fixture.package(fixture.project, "sample.app", "1.0.0",
                    "dependency sample.expected 1.0.0 path ../local\n");
    fixture.package(fixture.root / "local", "sample.actual", "1.0.0");
    manifest = foundation::readPackageManifest(fixture.project / "foundation.package");
    const auto mismatch = foundation::resolveProjectPackages(
        fixture.project / "foundation.package", *manifest.value,
        *foundation::parsePackageVersion("0.1.0"), foundation::TargetPlatform::Linux, {});
    expect(hasCode(mismatch.errors, "FDN4052"),
           "path dependency identity mismatch reports a resolution conflict");
}

void catalogsIgnoreIncompatibleAndInactiveDependencies() {
    Fixture fixture;
    fixture.package(fixture.project, "sample.app", "1.0.0",
                    "dependency sample.remote ^1.0.0 registry default\n"
                    "dependency sample.remote * registry default target linux\n"
                    "dependency sample.windows 1.0.0 registry missing target windows\n");
    fixture.package(fixture.registry / "sample.remote" / "1.0.0", "sample.remote",
                    "1.0.0");
    fixture.package(fixture.registry / "sample.remote" / "2.0.0", "sample.remote",
                    "2.0.0", "dependency sample.unavailable 1.0.0 registry missing\n");
    const auto manifest =
        foundation::readPackageManifest(fixture.project / "foundation.package");
    const std::vector<foundation::PackageRegistryRoot> registries{
        {"default", fixture.registry}};
    const auto resolved = foundation::resolveProjectPackages(
        fixture.project / "foundation.package", *manifest.value,
        *foundation::parsePackageVersion("0.1.0"), foundation::TargetPlatform::Linux,
        registries);
    expect(resolved.value.has_value() && resolved.value->lock.packages.size() == 1,
           "inactive targets and incompatible versions do not expand the catalog");
}

} // namespace

int runPackageCatalogTests() {
    catalogsCombineRegistryAndPathDependencies();
    catalogsRejectMissingAdaptersAndPathIdentityMismatch();
    catalogsIgnoreIncompatibleAndInactiveDependencies();
    return failures;
}
