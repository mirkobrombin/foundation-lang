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
    std::filesystem::path source;
    std::filesystem::path cache;

    Fixture() {
        root = std::filesystem::temp_directory_path() / "foundation-package-cache-test";
        source = root / "source";
        cache = root / "cache";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(source / "src");
        std::ofstream manifest(source / "foundation.package", std::ios::binary);
        manifest << "format foundation.package/v1\n"
                 << "name sample.lib\n"
                 << "version 1.0.0\n"
                 << "sdk ^0.1.0\n"
                 << "source src\n";
        std::ofstream code(source / "src" / "main.fn", std::ios::binary);
        code << "package sample.lib\n";
    }

    ~Fixture() { std::filesystem::remove_all(root); }
};

foundation::PackageCandidate candidate(const Fixture &fixture) {
    const auto manifest = foundation::readPackageManifest(fixture.source / "foundation.package");
    const auto snapshot = foundation::inspectPackageSource(fixture.source, *manifest.value);
    return {*manifest.value, snapshot.value->digest, foundation::PackageLocationKind::Registry,
            "default", fixture.source};
}

void cacheInstallationIsVerifiedAndIdempotent() {
    Fixture fixture;
    const auto value = candidate(fixture);
    const auto first = foundation::installPackageInCache(fixture.cache, value);
    const auto second = foundation::installPackageInCache(fixture.cache, value);
    expect(first.value.has_value() && second.value.has_value() &&
               *first.value == *second.value,
           "verified cache install is idempotent");
    const foundation::LockedPackage locked{value.manifest.name, value.manifest.version,
                                            value.digest, value.kind, value.location};
    const auto verified = foundation::verifyPackageInCache(fixture.cache, locked);
    expect(verified.value.has_value(), "locked cache entry verifies by identity and digest");
}

void cacheCorruptionIsRejected() {
    Fixture fixture;
    const auto value = candidate(fixture);
    const auto installed = foundation::installPackageInCache(fixture.cache, value);
    expect(installed.value.has_value(), "cache fixture installs");
    if (!installed.value.has_value()) {
        return;
    }
    std::ofstream changed(*installed.value / "src" / "main.fn", std::ios::binary);
    changed << "package sample.changed\n";
    changed.close();
    const foundation::LockedPackage locked{value.manifest.name, value.manifest.version,
                                            value.digest, value.kind, value.location};
    const auto verified = foundation::verifyPackageInCache(fixture.cache, locked);
    expect(hasCode(verified.errors, "FDN4072"), "cache content corruption is rejected");
    const auto repeated = foundation::installPackageInCache(fixture.cache, value);
    expect(!repeated.value.has_value(), "cache installation never replaces corrupted content");
}

void cacheRootSymlinkIsRejected() {
    Fixture fixture;
    const auto realCache = fixture.root / "real-cache";
    std::filesystem::create_directories(realCache);
    std::error_code error;
    std::filesystem::create_directory_symlink(realCache, fixture.cache, error);
    if (error) {
        return;
    }
    const auto installed = foundation::installPackageInCache(fixture.cache, candidate(fixture));
    expect(hasCode(installed.errors, "FDN4075"), "cache root symlink is rejected");
}

void cacheAlgorithmSymlinkIsRejected() {
    Fixture fixture;
    const auto outside = fixture.root / "outside-cache";
    std::filesystem::create_directories(fixture.cache);
    std::filesystem::create_directories(outside);
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, fixture.cache / "sha256", error);
    if (error) {
        return;
    }
    const auto value = candidate(fixture);
    const auto installed = foundation::installPackageInCache(fixture.cache, value);
    expect(hasCode(installed.errors, "FDN4075"),
           "cache algorithm root symlink is rejected during installation");
    const foundation::LockedPackage locked{value.manifest.name, value.manifest.version,
                                            value.digest, value.kind, value.location};
    const auto verified = foundation::verifyPackageInCache(fixture.cache, locked);
    expect(hasCode(verified.errors, "FDN4075"),
           "cache algorithm root symlink is rejected during verification");
}

void concurrentCacheInstallationPublishesOneEntry() {
    Fixture fixture;
    const auto value = candidate(fixture);
    std::vector<foundation::PackageParseResult<std::filesystem::path>> results(8);
    std::vector<std::thread> threads;
    threads.reserve(results.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        threads.emplace_back([&, index]() {
            results[index] = foundation::installPackageInCache(fixture.cache, value);
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    const auto expected = results.front().value;
    expect(expected.has_value(), "one concurrent cache installation succeeds");
    for (const auto &result : results) {
        expect(result.value == expected && result.errors.empty(),
               "all concurrent cache installations observe the same verified entry");
    }
    std::size_t entries{};
    for (const auto &entry : std::filesystem::directory_iterator(fixture.cache / "sha256")) {
        if (!entry.path().filename().string().starts_with(".tmp-")) {
            ++entries;
        }
    }
    expect(entries == 1, "concurrent installation publishes one content entry");
}

void cachePruningRemovesOnlyUnreferencedAndInterruptedEntries() {
    Fixture fixture;
    const auto kept = candidate(fixture);
    const auto keptInstall = foundation::installPackageInCache(fixture.cache, kept);
    expect(keptInstall.value.has_value(), "kept cache fixture installs");

    std::ofstream(fixture.source / "src" / "main.fn", std::ios::binary)
        << "package sample.changed\n";
    const auto discarded = candidate(fixture);
    const auto discardedInstall = foundation::installPackageInCache(fixture.cache, discarded);
    expect(discardedInstall.value.has_value(), "discarded cache fixture installs");
    const auto interrupted = fixture.cache / "sha256" / ".tmp-interrupted";
    std::filesystem::create_directories(interrupted);
    std::ofstream(interrupted / "partial", std::ios::binary) << "partial";

    const std::vector<std::string> keep{kept.digest};
    const auto pruned = foundation::prunePackageCache(fixture.cache, keep);
    expect(pruned.errors.empty() && pruned.changed.size() == 2,
           "cache prune reports unreferenced and interrupted paths");
    expect(keptInstall.value.has_value() && std::filesystem::exists(*keptInstall.value),
           "cache prune preserves referenced content");
    expect(discardedInstall.value.has_value() &&
               !std::filesystem::exists(*discardedInstall.value) &&
               !std::filesystem::exists(interrupted),
           "cache prune removes only selected entries");
}

void cacheManifestSymlinkIsRejected() {
    Fixture fixture;
    const auto value = candidate(fixture);
    const auto installed = foundation::installPackageInCache(fixture.cache, value);
    expect(installed.value.has_value(), "manifest symlink cache fixture installs");
    if (!installed.value.has_value()) {
        return;
    }
    std::filesystem::remove(*installed.value / "foundation.package");
    std::error_code error;
    std::filesystem::create_symlink(fixture.source / "foundation.package",
                                    *installed.value / "foundation.package", error);
    if (error) {
        return;
    }
    const foundation::LockedPackage locked{value.manifest.name, value.manifest.version,
                                            value.digest, value.kind, value.location};
    const auto verified = foundation::verifyPackageInCache(fixture.cache, locked);
    expect(hasCode(verified.errors, "FDN4071"),
           "cache verification does not follow a manifest symlink");
}

} // namespace

int runPackageCacheTests() {
    cacheInstallationIsVerifiedAndIdempotent();
    cacheCorruptionIsRejected();
    cacheRootSymlinkIsRejected();
    cacheAlgorithmSymlinkIsRejected();
    concurrentCacheInstallationPublishesOneEntry();
    cachePruningRemovesOnlyUnreferencedAndInterruptedEntries();
    cacheManifestSymlinkIsRejected();
    return failures;
}
