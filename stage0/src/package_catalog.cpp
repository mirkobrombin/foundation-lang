#include "foundation/package.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <system_error>
#include <tuple>

namespace foundation {

namespace {

constexpr std::size_t maxCatalogWork = 65536;

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

bool realFile(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
           !std::filesystem::is_symlink(status);
}

std::optional<std::filesystem::path> canonicalDirectory(
    const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_directory(status) ||
        std::filesystem::is_symlink(status)) {
        return std::nullopt;
    }
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) {
        return std::nullopt;
    }
    return canonical;
}

std::optional<PackageManifest> normalizeManifest(
    const std::filesystem::path &path, PackageManifest manifest,
    const std::filesystem::path &manifestRoot, const std::filesystem::path &projectRoot,
    TargetPlatform target, std::vector<PackageError> &errors) {
    for (auto &dependency : manifest.dependencies) {
        if (dependency.kind != PackageLocationKind::Path ||
            (dependency.target.has_value() && *dependency.target != target)) {
            continue;
        }
        std::error_code error;
        const auto dependencyRoot =
            std::filesystem::absolute(manifestRoot / dependency.location, error)
                .lexically_normal();
        if (error) {
            addError(errors, path, "FDN4092",
                     "path dependency cannot be represented relative to the project");
            return std::nullopt;
        }
        const auto relative = dependencyRoot.lexically_relative(projectRoot);
        if (relative.empty() || relative.is_absolute()) {
            addError(errors, path, "FDN4092",
                     "path dependency cannot be represented relative to the project");
            return std::nullopt;
        }
        dependency.location = relative.generic_string();
    }
    return manifest;
}

std::string catalogKey(const PackageDependency &dependency) {
    const auto kind = dependency.kind == PackageLocationKind::Path ? "path" : "registry";
    return std::string(kind) + '\n' + dependency.location + '\n' + dependency.name + '\n' +
           dependency.requirement.string();
}

bool active(const PackageDependency &dependency, TargetPlatform target) {
    return !dependency.target.has_value() || *dependency.target == target;
}

} // namespace

PackageParseResult<PackageCatalog>
collectPackageCatalog(const std::filesystem::path &rootManifestPath,
                      const PackageManifest &root, const PackageVersion &sdk,
                      TargetPlatform target,
                      std::span<const PackageRegistryRoot> registries) {
    PackageParseResult<PackageCatalog> result;
    const auto projectRoot = canonicalDirectory(rootManifestPath.parent_path());
    if (!projectRoot.has_value() || !realFile(rootManifestPath)) {
        addError(result.errors, rootManifestPath, "FDN4090",
                 "root manifest must be a regular file in a real directory");
        return result;
    }

    std::map<std::string, std::filesystem::path> registryRoots;
    for (const auto &registry : registries) {
        if (!isValidPackageName(registry.identity) ||
            !registryRoots.emplace(registry.identity, registry.root).second) {
            addError(result.errors, rootManifestPath, "FDN4090",
                     "registry identities must be valid and unique");
            return result;
        }
    }

    auto normalizedRoot = normalizeManifest(rootManifestPath, root, *projectRoot,
                                            *projectRoot, target, result.errors);
    if (!normalizedRoot.has_value()) {
        return result;
    }
    std::deque<PackageDependency> pending(normalizedRoot->dependencies.begin(),
                                          normalizedRoot->dependencies.end());
    std::set<std::string> visited;
    std::set<std::tuple<std::string, PackageVersion, PackageLocationKind, std::string,
                        std::string>> seenCandidates;
    std::vector<PackageCandidate> candidates;
    while (!pending.empty()) {
        if (visited.size() + pending.size() > maxCatalogWork) {
            addError(result.errors, rootManifestPath, "FDN4093",
                     "package catalog exceeds work limit");
            return result;
        }
        auto dependency = std::move(pending.front());
        pending.pop_front();
        if (!active(dependency, target)) {
            continue;
        }
        if (!visited.insert(catalogKey(dependency)).second) {
            continue;
        }

        std::vector<PackageCandidate> discovered;
        if (dependency.kind == PackageLocationKind::Registry) {
            const auto registry = registryRoots.find(dependency.location);
            if (registry == registryRoots.end()) {
                continue;
            }
            auto loaded = readLocalPackageRegistry(registry->second, dependency.location,
                                                   dependency.name);
            if (!loaded.value.has_value()) {
                result.errors = std::move(loaded.errors);
                return result;
            }
            discovered = std::move(*loaded.value);
        } else {
            const auto packageRoot = canonicalDirectory(*projectRoot / dependency.location);
            if (!packageRoot.has_value() ||
                !realFile(*packageRoot / "foundation.package")) {
                continue;
            }
            const auto parsed = readPackageManifest(*packageRoot / "foundation.package");
            if (!parsed.value.has_value()) {
                result.errors = parsed.errors;
                return result;
            }
            if (parsed.value->name != dependency.name) {
                continue;
            }
            const auto snapshot = inspectPackageSource(*packageRoot, *parsed.value);
            if (!snapshot.value.has_value()) {
                result.errors = snapshot.errors;
                return result;
            }
            discovered.push_back({*parsed.value, snapshot.value->digest,
                                  PackageLocationKind::Path, dependency.location,
                                  *packageRoot});
        }

        for (auto &candidate : discovered) {
            if (!dependency.requirement.accepts(candidate.manifest.version) ||
                !candidate.manifest.sdk.accepts(sdk)) {
                continue;
            }
            auto manifest = normalizeManifest(candidate.root / "foundation.package",
                                              std::move(candidate.manifest), candidate.root,
                                              *projectRoot, target, result.errors);
            if (!manifest.has_value()) {
                return result;
            }
            candidate.manifest = std::move(*manifest);
            const auto key = std::make_tuple(candidate.manifest.name,
                                             candidate.manifest.version, candidate.kind,
                                             candidate.location, candidate.digest);
            if (!seenCandidates.insert(key).second) {
                continue;
            }
            for (const auto &child : candidate.manifest.dependencies) {
                if (active(child, target) &&
                    child.scope == PackageDependencyScope::Runtime) {
                    pending.push_back(child);
                }
            }
            candidates.push_back(std::move(candidate));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return std::tie(left.manifest.name, left.manifest.version, left.kind, left.location,
                        left.digest) <
               std::tie(right.manifest.name, right.manifest.version, right.kind,
                        right.location, right.digest);
    });
    result.value = PackageCatalog{std::move(*normalizedRoot), std::move(candidates)};
    return result;
}

PackageParseResult<PackageResolution>
resolveProjectPackages(const std::filesystem::path &rootManifestPath,
                       const PackageManifest &root, const PackageVersion &sdk,
                       TargetPlatform target,
                       std::span<const PackageRegistryRoot> registries) {
    auto catalog = collectPackageCatalog(rootManifestPath, root, sdk, target, registries);
    if (!catalog.value.has_value()) {
        return {std::nullopt, std::move(catalog.errors)};
    }
    return resolvePackageGraph(rootManifestPath, catalog.value->root, sdk, target,
                               catalog.value->candidates);
}

} // namespace foundation
