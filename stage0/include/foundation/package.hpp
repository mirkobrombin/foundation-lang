#ifndef FOUNDATION_PACKAGE_HPP
#define FOUNDATION_PACKAGE_HPP

#include "foundation/target.hpp"

#include <compare>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

struct PackageError {
    std::filesystem::path path;
    std::size_t line{1};
    std::size_t column{1};
    std::string code;
    std::string message;
};

struct PackageVersion {
    std::size_t major{};
    std::size_t minor{};
    std::size_t patch{};
    std::vector<std::string> prerelease;
    std::vector<std::string> build;

    [[nodiscard]] std::string string() const;
    [[nodiscard]] std::strong_ordering operator<=>(const PackageVersion &other) const;
    [[nodiscard]] bool operator==(const PackageVersion &other) const;
};

enum class PackageRequirementKind {
    Any,
    Exact,
    Caret,
    Tilde,
};

struct PackageRequirement {
    PackageRequirementKind kind{PackageRequirementKind::Exact};
    PackageVersion version;

    [[nodiscard]] std::string string() const;
    [[nodiscard]] bool accepts(const PackageVersion &candidate) const;
};

enum class PackageLocationKind {
    Path,
    Registry,
};

enum class PackageDependencyScope {
    Runtime,
    Test,
};

struct PackageDependency {
    std::string name;
    PackageRequirement requirement;
    PackageLocationKind kind{PackageLocationKind::Registry};
    std::string location;
    std::optional<TargetPlatform> target;
    PackageDependencyScope scope{PackageDependencyScope::Runtime};
};

struct PackageManifest {
    std::string name;
    PackageVersion version;
    PackageRequirement sdk;
    std::filesystem::path source;
    std::optional<std::filesystem::path> testSource;
    std::vector<PackageDependency> dependencies;
};

struct LockedPackage {
    std::string name;
    PackageVersion version;
    std::string digest;
    PackageLocationKind kind{PackageLocationKind::Registry};
    std::string location;
};

struct PackageEdge {
    std::string parent;
    std::string dependency;
    PackageDependencyScope scope{PackageDependencyScope::Runtime};
};

struct PackageLock {
    std::string rootName;
    PackageVersion rootVersion;
    TargetPlatform target{TargetPlatform::Linux};
    std::vector<LockedPackage> packages;
    std::vector<PackageEdge> edges;
};

struct PackageSourceFile {
    std::filesystem::path path;
    std::uintmax_t size{};
};

struct PackageSourceSnapshot {
    std::string digest;
    std::uintmax_t totalBytes{};
    std::vector<PackageSourceFile> files;
};

struct PackageCandidate {
    PackageManifest manifest;
    std::string digest;
    PackageLocationKind kind{PackageLocationKind::Registry};
    std::string location;
    std::filesystem::path root;
};

struct PackageResolution {
    PackageLock lock;
    std::vector<PackageCandidate> packages;
};

struct PackageRegistryRoot {
    std::string identity;
    std::filesystem::path root;
};

struct PackageCatalog {
    PackageManifest root;
    std::vector<PackageCandidate> candidates;
};

struct LockedPackageSource {
    std::string name;
    std::filesystem::path packageRoot;
    std::filesystem::path sourceRoot;
    PackageManifest manifest;
    PackageDependencyScope scope{PackageDependencyScope::Runtime};
};

struct LockedPackageProject {
    std::filesystem::path manifestPath;
    std::filesystem::path projectRoot;
    PackageManifest manifest;
    PackageLock lock;
    std::vector<LockedPackageSource> sources;
};

struct PackageMutationResult {
    std::vector<std::filesystem::path> changed;
    std::vector<PackageError> errors;
};

template <typename T> struct PackageParseResult {
    std::optional<T> value;
    std::vector<PackageError> errors;
};

[[nodiscard]] std::optional<PackageVersion> parsePackageVersion(std::string_view text);
[[nodiscard]] bool isValidPackageName(std::string_view value);
[[nodiscard]] std::optional<PackageRequirement>
parsePackageRequirement(std::string_view text);
[[nodiscard]] PackageParseResult<PackageManifest>
parsePackageManifest(const std::filesystem::path &path, std::string_view source);
[[nodiscard]] PackageParseResult<PackageManifest>
readPackageManifest(const std::filesystem::path &path);
[[nodiscard]] std::string renderPackageManifest(const PackageManifest &manifest);
[[nodiscard]] PackageParseResult<PackageLock>
parsePackageLock(const std::filesystem::path &path, std::string_view source);
[[nodiscard]] PackageParseResult<PackageLock>
readPackageLock(const std::filesystem::path &path);
[[nodiscard]] std::string renderPackageLock(const PackageLock &lock);
[[nodiscard]] PackageMutationResult
writePackageLockAtomically(const std::filesystem::path &path, const PackageLock &lock);
[[nodiscard]] PackageParseResult<PackageSourceSnapshot>
inspectPackageSource(const std::filesystem::path &packageDirectory,
                     const PackageManifest &manifest);
[[nodiscard]] PackageParseResult<PackageResolution>
resolvePackageGraph(const std::filesystem::path &rootManifestPath,
                    const PackageManifest &root, const PackageVersion &sdk,
                    TargetPlatform target, std::span<const PackageCandidate> catalog);
[[nodiscard]] PackageParseResult<PackageCatalog>
collectPackageCatalog(const std::filesystem::path &rootManifestPath,
                      const PackageManifest &root, const PackageVersion &sdk,
                      TargetPlatform target,
                      std::span<const PackageRegistryRoot> registries);
[[nodiscard]] PackageParseResult<PackageResolution>
resolveProjectPackages(const std::filesystem::path &rootManifestPath,
                       const PackageManifest &root, const PackageVersion &sdk,
                       TargetPlatform target,
                       std::span<const PackageRegistryRoot> registries);
[[nodiscard]] std::optional<std::filesystem::path>
discoverPackageManifest(const std::filesystem::path &input);
[[nodiscard]] PackageParseResult<LockedPackageProject>
loadLockedPackageProject(const std::filesystem::path &manifestPath,
                         const PackageVersion &sdk, TargetPlatform target,
                         const std::optional<std::filesystem::path> &cacheRoot,
                         bool includeTestDependencies = true);
[[nodiscard]] PackageParseResult<std::vector<PackageCandidate>>
readLocalPackageRegistry(const std::filesystem::path &registryRoot,
                         std::string_view identity, std::string_view packageName);
[[nodiscard]] PackageParseResult<std::filesystem::path>
installPackageInCache(const std::filesystem::path &cacheRoot,
                      const PackageCandidate &candidate);
[[nodiscard]] PackageParseResult<std::filesystem::path>
verifyPackageInCache(const std::filesystem::path &cacheRoot,
                     const LockedPackage &package);
[[nodiscard]] PackageMutationResult
prunePackageCache(const std::filesystem::path &cacheRoot,
                  std::span<const std::string> keepDigests);
[[nodiscard]] std::optional<std::filesystem::path> defaultPackageCachePath();
[[nodiscard]] std::string renderPackageError(const PackageError &error);

} // namespace foundation

#endif
