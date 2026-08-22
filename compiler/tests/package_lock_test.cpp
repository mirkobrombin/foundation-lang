#include "foundation/package.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
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
    for (const auto &error : errors) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

struct Fixture {
    std::filesystem::path root;
    std::filesystem::path lockPath;

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-lock-test";
        lockPath = root / "foundation.lock";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~Fixture() { std::filesystem::remove_all(root); }
};

foundation::PackageLock packageLock(std::string_view version = "1.0.0") {
    foundation::PackageLock lock;
    lock.rootName = "sample.app";
    lock.rootVersion = *foundation::parsePackageVersion(version);
    lock.target = foundation::TargetPlatform::Linux;
    return lock;
}

void lockWritesAreAtomicAndIdempotent() {
    Fixture fixture;
    const auto lock = packageLock();
    const auto first = foundation::writePackageLockAtomically(fixture.lockPath, lock);
    const auto second = foundation::writePackageLockAtomically(fixture.lockPath, lock);
    expect(first.errors.empty() && first.changed.size() == 1,
           "first lock write reports the changed path");
    expect(second.errors.empty() && second.changed.empty(),
           "identical lock write does not replace the file");
    const auto parsed = foundation::readPackageLock(fixture.lockPath);
    expect(parsed.value.has_value() && foundation::renderPackageLock(*parsed.value) ==
                                           foundation::renderPackageLock(lock),
           "published lockfile is canonical and readable");
}

void concurrentLockWritesRemainComplete() {
    Fixture fixture;
    std::vector<foundation::PackageMutationResult> results(8);
    std::vector<std::thread> threads;
    threads.reserve(results.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        threads.emplace_back([&, index]() {
            results[index] = foundation::writePackageLockAtomically(
                fixture.lockPath, packageLock(index % 2 == 0 ? "1.0.0" : "2.0.0"));
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    for (const auto &result : results) {
        expect(result.errors.empty(), "concurrent lock writer publishes without error");
    }
    const auto parsed = foundation::readPackageLock(fixture.lockPath);
    expect(parsed.value.has_value() &&
               (parsed.value->rootVersion.string() == "1.0.0" ||
                parsed.value->rootVersion.string() == "2.0.0"),
           "concurrent lock writes leave one complete valid generation");
    for (const auto &entry : std::filesystem::directory_iterator(fixture.root)) {
        expect(!entry.path().filename().string().starts_with(".foundation.lock.tmp-"),
               "successful lock writes remove staging directories");
    }
}

void lockWritesRejectSymlinkDestinations() {
    Fixture fixture;
    const auto target = fixture.root / "target.lock";
    std::ofstream(target) << foundation::renderPackageLock(packageLock());
    std::error_code error;
    std::filesystem::create_symlink(target, fixture.lockPath, error);
    if (error) {
        return;
    }
    const auto written =
        foundation::writePackageLockAtomically(fixture.lockPath, packageLock("2.0.0"));
    expect(hasCode(written.errors, "FDN4082"), "lock writer rejects symlink destinations");
    const auto targetLock = foundation::readPackageLock(target);
    expect(targetLock.value.has_value() && targetLock.value->rootVersion.string() == "1.0.0",
           "rejected symlink write leaves its target unchanged");
}

} // namespace

int runPackageLockTests() {
    lockWritesAreAtomicAndIdempotent();
    concurrentLockWritesRemainComplete();
    lockWritesRejectSymlinkDestinations();
    return failures;
}
