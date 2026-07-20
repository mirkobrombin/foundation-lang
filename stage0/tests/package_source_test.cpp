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

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-source-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src" / "nested");
    }

    ~Fixture() { std::filesystem::remove_all(root); }

    void write(const std::filesystem::path &path, std::string_view source) const {
        std::ofstream output(root / path, std::ios::binary);
        output << source;
    }
};

foundation::PackageManifest manifest() {
    foundation::PackageManifest result;
    result.name = "sample.package";
    result.version = *foundation::parsePackageVersion("1.0.0");
    result.sdk = *foundation::parsePackageRequirement("^0.1.0");
    result.source = "src";
    return result;
}

void snapshotsAreDeterministicAndSensitive() {
    Fixture fixture;
    fixture.write("src/z.fdn", "module sample.z\n");
    fixture.write("src/nested/a.fdn", "module sample.a\n");
    const auto first = foundation::inspectPackageSource(fixture.root, manifest());
    const auto second = foundation::inspectPackageSource(fixture.root, manifest());
    expect(first.value.has_value() && second.value.has_value(), "package source is inspected");
    expect(first.value.has_value() && second.value.has_value() &&
               first.value->digest == second.value->digest,
           "package source digest is deterministic");
    expect(first.value.has_value() && first.value->files.size() == 2 &&
               first.value->files[0].path == "src/nested/a.fdn",
           "package source inventory is sorted");
    fixture.write("src/z.fdn", "module sample.changed\n");
    const auto changed = foundation::inspectPackageSource(fixture.root, manifest());
    expect(first.value.has_value() && changed.value.has_value() &&
               first.value->digest != changed.value->digest,
           "package source digest changes with content");
}

void snapshotsRejectSymlinksAndCaseCollisions() {
    Fixture fixture;
    fixture.write("src/name.fdn", "module sample.name\n");
    fixture.write("src/Name.fdn", "module sample.other\n");
    const auto collision = foundation::inspectPackageSource(fixture.root, manifest());
    expect(hasCode(collision.errors, "FDN4036"), "case-folded source collisions are rejected");

    std::filesystem::remove(fixture.root / "src" / "Name.fdn");
    std::error_code error;
    std::filesystem::create_symlink(fixture.root / "src" / "name.fdn",
                                    fixture.root / "src" / "alias.fdn", error);
    if (!error) {
        const auto symlink = foundation::inspectPackageSource(fixture.root, manifest());
        expect(hasCode(symlink.errors, "FDN4033"), "source symlinks are rejected");
    }
}

void snapshotsIncludeTestSources() {
    Fixture fixture;
    std::filesystem::create_directories(fixture.root / "tests");
    fixture.write("src/main.fdn", "package sample.package\n");
    auto withTests = manifest();
    withTests.testSource = "tests";
    fixture.write("tests/main.fdn", "package sample.package\n");
    const auto first = foundation::inspectPackageSource(fixture.root, withTests);
    fixture.write("tests/main.fdn", "package sample.changed\n");
    const auto changed = foundation::inspectPackageSource(fixture.root, withTests);
    expect(first.value.has_value() && first.value->files.size() == 2,
           "package snapshot inventories production and test sources");
    expect(first.value.has_value() && changed.value.has_value() &&
               first.value->digest != changed.value->digest,
           "package digest changes with test source content");
}

} // namespace

int runPackageSourceTests() {
    snapshotsAreDeterministicAndSensitive();
    snapshotsRejectSymlinksAndCaseCollisions();
    snapshotsIncludeTestSources();
    return failures;
}
