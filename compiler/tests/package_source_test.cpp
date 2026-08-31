#include "foundation/package.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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
    fixture.write("src/z.fn", "module sample.z\n");
    fixture.write("src/nested/a.fn", "module sample.a\n");
    const auto first = foundation::inspectPackageSource(fixture.root, manifest());
    const auto second = foundation::inspectPackageSource(fixture.root, manifest());
    expect(first.value.has_value() && second.value.has_value(), "package source is inspected");
    expect(first.value.has_value() && second.value.has_value() &&
               first.value->digest == second.value->digest,
           "package source digest is deterministic");
    expect(first.value.has_value() && first.value->files.size() == 2 &&
               first.value->files[0].path == "src/nested/a.fn",
           "package source inventory is sorted");
    fixture.write("src/z.fn", "module sample.changed\n");
    const auto changed = foundation::inspectPackageSource(fixture.root, manifest());
    expect(first.value.has_value() && changed.value.has_value() &&
               first.value->digest != changed.value->digest,
           "package source digest changes with content");
}

void snapshotsRejectSymlinksAndCaseCollisions() {
    Fixture fixture;
    fixture.write("src/name.fn", "module sample.name\n");
    fixture.write("src/Name.fn", "module sample.other\n");
    const auto collision = foundation::inspectPackageSource(fixture.root, manifest());
    std::error_code equivalentError;
    const auto sameFile = std::filesystem::equivalent(fixture.root / "src" / "name.fn",
                                                      fixture.root / "src" / "Name.fn",
                                                      equivalentError);
    if (!equivalentError && !sameFile) {
        expect(hasCode(collision.errors, "FDN4036"),
               "case-folded source collisions are rejected");
    }

    std::filesystem::remove(fixture.root / "src" / "Name.fn");
    fixture.write("src/name.fn", "module sample.name\n");
    std::error_code error;
    std::filesystem::create_symlink(fixture.root / "src" / "name.fn",
                                    fixture.root / "src" / "alias.fn", error);
    if (!error) {
        const auto symlink = foundation::inspectPackageSource(fixture.root, manifest());
        expect(hasCode(symlink.errors, "FDN4033"), "source symlinks are rejected");
    }
}

void snapshotsIncludeTestSources() {
    Fixture fixture;
    std::filesystem::create_directories(fixture.root / "tests");
    fixture.write("src/main.fn", "package sample.package\n");
    auto withTests = manifest();
    withTests.testSource = "tests";
    fixture.write("tests/main.fn", "package sample.package\n");
    const auto first = foundation::inspectPackageSource(fixture.root, withTests);
    fixture.write("tests/main.fn", "package sample.changed\n");
    const auto changed = foundation::inspectPackageSource(fixture.root, withTests);
    expect(first.value.has_value() && first.value->files.size() == 2,
           "package snapshot inventories production and test sources");
    expect(first.value.has_value() && changed.value.has_value() &&
               first.value->digest != changed.value->digest,
           "package digest changes with test source content");
}

void foreignSnapshotsPinContentAndRejectUnsafeTrees() {
    Fixture fixture;
    const auto foreign = fixture.root / "foreign";
    std::filesystem::create_directories(foreign / "nested");
    fixture.write("foreign/nested/api.h", "int sample(void);\n");
    const auto first = foundation::inspectForeignSource(foreign);
    const auto repeated = foundation::inspectForeignSource(foreign);
    expect(first.value.has_value() && repeated.value.has_value() &&
               first.value->digest == repeated.value->digest,
           "foreign path digest is deterministic");
    fixture.write("foreign/nested/api.h", "int changed(void);\n");
    const auto changed = foundation::inspectForeignSource(foreign);
    expect(first.value.has_value() && changed.value.has_value() &&
               first.value->digest != changed.value->digest,
           "foreign path digest pins file content");

    std::filesystem::create_directories(fixture.root / "empty");
    expect(hasCode(foundation::inspectForeignSource(fixture.root / "empty").errors,
                   "FDN4057"),
           "empty foreign path sources are rejected");

    std::error_code error;
    std::filesystem::create_directory_symlink(foreign, fixture.root / "foreign-link", error);
    if (!error) {
        expect(hasCode(foundation::inspectForeignSource(fixture.root / "foreign-link").errors,
                       "FDN4057"),
               "foreign source roots cannot be symlinks");
    }
    error.clear();
    std::filesystem::create_symlink(foreign / "nested" / "api.h",
                                    foreign / "nested" / "alias.h", error);
    if (!error) {
        expect(hasCode(foundation::inspectForeignSource(foreign).errors, "FDN4057"),
               "foreign source trees cannot contain symlinks");
        std::filesystem::remove(foreign / "nested" / "alias.h");
    }

    fixture.write("foreign/Name.h", "one\n");
    fixture.write("foreign/name.h", "two\n");
    error.clear();
    const auto sameFile = std::filesystem::equivalent(
        foreign / "Name.h", foreign / "name.h", error);
    if (!error && !sameFile) {
        expect(hasCode(foundation::inspectForeignSource(foreign).errors, "FDN4057"),
               "foreign source paths cannot collide without case sensitivity");
    }
    std::filesystem::remove(foreign / "Name.h");
    std::filesystem::remove(foreign / "name.h");

#ifndef _WIN32
    const auto fifo = foreign / "input.pipe";
    if (::mkfifo(fifo.c_str(), 0600) == 0) {
        expect(hasCode(foundation::inspectForeignSource(foreign).errors, "FDN4057"),
               "foreign source trees can contain only regular files");
    }
#endif
}

} // namespace

int runPackageSourceTests() {
    snapshotsAreDeterministicAndSensitive();
    snapshotsRejectSymlinksAndCaseCollisions();
    snapshotsIncludeTestSources();
    foreignSnapshotsPinContentAndRejectUnsafeTrees();
    return failures;
}
