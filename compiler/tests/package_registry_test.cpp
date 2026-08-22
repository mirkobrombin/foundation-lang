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

struct Fixture {
    std::filesystem::path root;

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-registry-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~Fixture() { std::filesystem::remove_all(root); }

    void package(std::string_view name, std::string_view version) const {
        const auto directory = root / name / version;
        std::filesystem::create_directories(directory / "src");
        std::ofstream manifest(directory / "foundation.package", std::ios::binary);
        manifest << "format foundation.package/v1\n"
                 << "name " << name << '\n'
                 << "version " << version << '\n'
                 << "sdk ^0.1.0\n"
                 << "source src\n";
        std::ofstream source(directory / "src" / "main.fn", std::ios::binary);
        source << "package " << name << '\n';
    }
};

void registryProducesDeterministicCandidates() {
    Fixture fixture;
    fixture.package("sample.lib", "2.0.0");
    fixture.package("sample.lib", "1.0.0");
    const auto first =
        foundation::readLocalPackageRegistry(fixture.root, "default", "sample.lib");
    const auto second =
        foundation::readLocalPackageRegistry(fixture.root, "default", "sample.lib");
    expect(first.value.has_value() && second.value.has_value(), "local registry is read");
    if (!first.value.has_value() || !second.value.has_value()) {
        return;
    }
    expect(first.value->size() == 2 && first.value->front().manifest.version.string() == "1.0.0",
           "registry candidates use semantic version order");
    expect(first.value->front().digest == second.value->front().digest &&
               first.value->front().location == "default" &&
               !first.value->front().root.empty(),
           "registry candidates preserve stable source identity and digest");
}

void registryRejectsUntrustedEntryShapes() {
    Fixture fixture;
    fixture.package("sample.lib", "1.0.0");
    std::filesystem::create_directories(fixture.root / "sample.lib" / "latest");
    const auto malformed =
        foundation::readLocalPackageRegistry(fixture.root, "default", "sample.lib");
    expect(!malformed.value.has_value() && !malformed.errors.empty() &&
               malformed.errors.front().code == "FDN4062",
           "non-version registry entry is rejected");
}

void registryRejectsTraversalAndSymlinks() {
    Fixture fixture;
    fixture.package("sample.lib", "1.0.0");
    const auto traversal =
        foundation::readLocalPackageRegistry(fixture.root, "default", "../sample.lib");
    expect(!traversal.value.has_value() && !traversal.errors.empty() &&
               traversal.errors.front().code == "FDN4060",
           "package names cannot escape the registry root");

    const auto outside = fixture.root.parent_path() / "foundation-package-registry-outside";
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, fixture.root / "sample.link", error);
    if (!error) {
        const auto linked =
            foundation::readLocalPackageRegistry(fixture.root, "default", "sample.link");
        expect(!linked.value.has_value() && !linked.errors.empty() &&
                   linked.errors.front().code == "FDN4061",
               "registry package roots cannot be symlinks");
    }
    std::filesystem::remove_all(outside);
}

void registryRejectsManifestSymlinks() {
    Fixture fixture;
    fixture.package("sample.lib", "1.0.0");
    const auto packageRoot = fixture.root / "sample.lib" / "1.0.0";
    const auto outside = fixture.root / "outside.package";
    std::filesystem::rename(packageRoot / "foundation.package", outside);
    std::error_code error;
    std::filesystem::create_symlink(outside, packageRoot / "foundation.package", error);
    if (error) {
        return;
    }
    const auto linked =
        foundation::readLocalPackageRegistry(fixture.root, "default", "sample.lib");
    expect(!linked.value.has_value() && !linked.errors.empty() &&
               linked.errors.front().code == "FDN4063",
           "registry reader does not follow a manifest symlink");
}

} // namespace

int runPackageRegistryTests() {
    registryProducesDeterministicCandidates();
    registryRejectsUntrustedEntryShapes();
    registryRejectsTraversalAndSymlinks();
    registryRejectsManifestSymlinks();
    return failures;
}
