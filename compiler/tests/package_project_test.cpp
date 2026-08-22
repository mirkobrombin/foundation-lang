#include "foundation/package.hpp"
#include "foundation/project.hpp"

#include <algorithm>
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
    std::filesystem::path app;
    std::filesystem::path dependency;
    std::filesystem::path cache;

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-project-test";
        app = root / "app";
        dependency = root / "dependency";
        cache = root / "cache";
        std::filesystem::remove_all(root);
        package(app, "sample.app", "1.0.0",
                "dependency sample.lib 1.0.0 path ../dependency\n");
        package(dependency, "sample.lib", "1.0.0");
    }

    ~Fixture() { std::filesystem::remove_all(root); }

    static void package(const std::filesystem::path &directory, std::string_view name,
                        std::string_view version, std::string_view dependencies = {}) {
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

    foundation::PackageResolution resolve() const {
        const auto manifest =
            foundation::readPackageManifest(app / "foundation.package");
        return *foundation::resolveProjectPackages(
                    app / "foundation.package", *manifest.value,
                    *foundation::parsePackageVersion("0.1.0"),
                    foundation::TargetPlatform::Linux, {})
                    .value;
    }
};

void lockedProjectsLoadVerifiedSources() {
    Fixture fixture;
    const auto resolution = fixture.resolve();
    const auto written = foundation::writePackageLockAtomically(
        fixture.app / "foundation.lock", resolution.lock);
    expect(written.errors.empty(), "locked project fixture writes");
    const auto discovered =
        foundation::discoverPackageManifest(fixture.app / "src" / "main.fn");
    expect(discovered.has_value() && discovered->filename() == "foundation.package",
           "manifest discovery walks from a source file");
    const auto loaded = foundation::loadLockedPackageProject(
        *discovered, *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, fixture.cache);
    expect(loaded.value.has_value() && loaded.value->sources.size() == 2,
           "locked project loads root and path dependency sources");
}

void lockedProjectsRejectChangedPathsAndTargets() {
    Fixture fixture;
    const auto resolution = fixture.resolve();
    const auto written = foundation::writePackageLockAtomically(
        fixture.app / "foundation.lock", resolution.lock);
    expect(written.errors.empty(), "changed path fixture lock writes");
    std::ofstream(fixture.dependency / "src" / "main.fn", std::ios::binary)
        << "package sample.changed\n";
    const auto changed = foundation::loadLockedPackageProject(
        fixture.app / "foundation.package", *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, fixture.cache);
    expect(hasCode(changed.errors, "FDN4112"),
           "changed path dependency is rejected before compilation");

    const auto target = foundation::loadLockedPackageProject(
        fixture.app / "foundation.package", *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Windows, fixture.cache);
    expect(hasCode(target.errors, "FDN4111"),
           "lock target mismatch is rejected before compilation");
}

void lockedProjectsRejectLockSymlinks() {
    Fixture fixture;
    const auto resolution = fixture.resolve();
    const auto target = fixture.root / "target.lock";
    const auto written = foundation::writePackageLockAtomically(target, resolution.lock);
    expect(written.errors.empty(), "lock symlink target fixture writes");
    std::error_code error;
    std::filesystem::create_symlink(target, fixture.app / "foundation.lock", error);
    if (error) {
        return;
    }
    const auto loaded = foundation::loadLockedPackageProject(
        fixture.app / "foundation.package", *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, fixture.cache);
    expect(hasCode(loaded.errors, "FDN4111"),
           "locked project loader does not follow a lock symlink");
}

void lockedProjectsRejectChangedForeignContent() {
    Fixture fixture;
    std::filesystem::create_directories(fixture.app / "native" / "libfuse");
    std::ofstream(fixture.app / "native" / "libfuse" / "fuse.h", std::ios::binary)
        << "int fuse_main(void);\n";
    std::ofstream(fixture.app / "foundation.package", std::ios::binary)
        << "format foundation.package/v1\n"
        << "name sample.app\n"
        << "version 1.0.0\n"
        << "sdk ^0.1.0\n"
        << "source src\n"
        << "native_library c\n"
        << "native_name sample_app\n"
        << "foreign c libfuse 2.9.9 path native/libfuse abi c/v1\n"
        << "dependency sample.lib 1.0.0 path ../dependency\n";
    const auto resolution = fixture.resolve();
    const auto written = foundation::writePackageLockAtomically(
        fixture.app / "foundation.lock", resolution.lock);
    expect(written.errors.empty(), "foreign content fixture lock writes");
    std::ofstream(fixture.app / "native" / "libfuse" / "fuse.h", std::ios::binary)
        << "int fuse_changed(void);\n";
    const auto loaded = foundation::loadLockedPackageProject(
        fixture.app / "foundation.package", *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, fixture.cache);
    expect(hasCode(loaded.errors, "FDN4111"),
           "changed foreign path content makes the lock stale");
}

void testSourcesStayOutOfProductionProjects() {
    Fixture fixture;
    std::filesystem::create_directories(fixture.app / "tests");
    std::ofstream(fixture.app / "foundation.package", std::ios::binary)
        << "format foundation.package/v1\n"
        << "name sample.app\n"
        << "version 1.0.0\n"
        << "sdk ^0.1.0\n"
        << "source src\n"
        << "test_source tests\n"
        << "dependency sample.lib 1.0.0 path ../dependency scope test\n";
    std::ofstream(fixture.app / "tests" / "check.fn", std::ios::binary)
        << "package sample.app\n";
    const auto resolution = fixture.resolve();
    const auto written = foundation::writePackageLockAtomically(
        fixture.app / "foundation.lock", resolution.lock);
    expect(written.errors.empty(), "test scope fixture lock writes");
    const auto locked = foundation::loadLockedPackageProject(
        fixture.app / "foundation.package", *foundation::parsePackageVersion("0.1.0"),
        foundation::TargetPlatform::Linux, fixture.cache);
    expect(locked.value.has_value() && locked.value->sources.size() == 2 &&
               locked.value->sources[1].scope == foundation::PackageDependencyScope::Test,
           "locked project classifies test-only packages");

    foundation::Diagnostics productionDiagnostics;
    const auto production = foundation::loadProject(
        fixture.app, productionDiagnostics, {}, foundation::ProjectMode::Production);
    foundation::Diagnostics testDiagnostics;
    const auto test = foundation::loadProject(fixture.app, testDiagnostics, {},
                                              foundation::ProjectMode::Test);
    const auto contains = [](const auto &project, std::string_view suffix) {
        return project.has_value() &&
               std::any_of(project->sources.begin(), project->sources.end(),
                           [&](const auto &source) { return source.path.ends_with(suffix); });
    };
    expect(production.has_value() && !contains(production, "tests/check.fn") &&
               !contains(production, "packages/sample.lib/src/main.fn"),
           "production analysis excludes test sources and test dependencies");
    expect(test.has_value() && contains(test, "tests/check.fn") &&
               contains(test, "packages/sample.lib/src/main.fn"),
           "test analysis includes root tests and test dependencies");

    std::filesystem::remove_all(fixture.dependency);
    foundation::Diagnostics isolatedDiagnostics;
    const auto isolated = foundation::loadProject(
        fixture.app, isolatedDiagnostics, {}, foundation::ProjectMode::Production);
    expect(isolated.has_value() && !isolatedDiagnostics.hasErrors(),
           "production analysis does not require test-only package content");
}

} // namespace

int runPackageProjectTests() {
    lockedProjectsLoadVerifiedSources();
    lockedProjectsRejectChangedPathsAndTargets();
    lockedProjectsRejectLockSymlinks();
    lockedProjectsRejectChangedForeignContent();
    testSourcesStayOutOfProductionProjects();
    return failures;
}
