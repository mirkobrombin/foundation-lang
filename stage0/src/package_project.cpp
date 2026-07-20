#include "foundation/package.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>
#include <tuple>

namespace foundation {

namespace {

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

bool active(const PackageDependency &dependency, TargetPlatform target) {
    return !dependency.target.has_value() || *dependency.target == target;
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
    return error ? std::nullopt : std::optional<std::filesystem::path>{canonical};
}

bool realFile(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
           !std::filesystem::is_symlink(status);
}

std::optional<std::string> lockedLocation(const PackageDependency &dependency,
                                          const std::filesystem::path &packageRoot,
                                          const std::filesystem::path &projectRoot) {
    if (dependency.kind == PackageLocationKind::Registry) {
        return dependency.location;
    }
    const auto target = canonicalDirectory(packageRoot / dependency.location);
    if (!target.has_value()) {
        return std::nullopt;
    }
    const auto relative = target->lexically_relative(projectRoot);
    if (relative.empty() || relative.is_absolute()) {
        return std::nullopt;
    }
    return relative.generic_string();
}

bool validateLockedGraph(const LockedPackageProject &project, bool includeTestDependencies,
                         std::vector<PackageError> &errors) {
    std::map<std::string, const LockedPackage *> packages;
    for (const auto &package : project.lock.packages) {
        packages.emplace(package.name, &package);
    }
    std::set<std::tuple<std::string, std::string, PackageDependencyScope>> expectedEdges;
    for (const auto &source : project.sources) {
        for (const auto &dependency : source.manifest.dependencies) {
            if (!active(dependency, project.lock.target)) {
                continue;
            }
            if (!includeTestDependencies &&
                dependency.scope == PackageDependencyScope::Test) {
                continue;
            }
            if (dependency.scope == PackageDependencyScope::Test &&
                source.name != project.manifest.name) {
                continue;
            }
            const auto locked = packages.find(dependency.name);
            const auto location =
                lockedLocation(dependency, source.packageRoot, project.projectRoot);
            if (locked == packages.end() || !location.has_value() ||
                !dependency.requirement.accepts(locked->second->version) ||
                dependency.kind != locked->second->kind ||
                *location != locked->second->location) {
                addError(errors, project.manifestPath, "FDN4113",
                         "package lock does not satisfy " + source.name + " dependency " +
                             dependency.name);
                return false;
            }
            expectedEdges.emplace(source.name, dependency.name, dependency.scope);
        }
    }
    std::set<std::tuple<std::string, std::string, PackageDependencyScope>> actualEdges;
    for (const auto &edge : project.lock.edges) {
        if (!includeTestDependencies &&
            (edge.scope == PackageDependencyScope::Test ||
             (edge.parent != project.manifest.name &&
              std::none_of(project.sources.begin(), project.sources.end(),
                           [&](const auto &source) { return source.name == edge.parent; })))) {
            continue;
        }
        actualEdges.emplace(edge.parent, edge.dependency, edge.scope);
    }
    if (actualEdges != expectedEdges) {
        addError(errors, project.manifestPath, "FDN4113",
                 "package lock edges do not match selected manifests");
        return false;
    }
    return true;
}

} // namespace

std::optional<std::filesystem::path>
discoverPackageManifest(const std::filesystem::path &input) {
    std::error_code error;
    auto current = std::filesystem::absolute(input, error);
    if (error) {
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(current, error) || error) {
        error.clear();
        current = current.parent_path();
    }
    while (!current.empty()) {
        const auto candidate = current / "foundation.package";
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (!error && status.type() != std::filesystem::file_type::not_found) {
            return candidate;
        }
        error.clear();
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::nullopt;
}

PackageParseResult<LockedPackageProject>
loadLockedPackageProject(const std::filesystem::path &manifestPath,
                         const PackageVersion &sdk, TargetPlatform target,
                         const std::optional<std::filesystem::path> &cacheRoot,
                         bool includeTestDependencies) {
    PackageParseResult<LockedPackageProject> result;
    const auto projectRoot = canonicalDirectory(manifestPath.parent_path());
    if (!projectRoot.has_value()) {
        addError(result.errors, manifestPath, "FDN4110",
                 "package project root must be a real directory");
        return result;
    }
    if (!realFile(manifestPath)) {
        addError(result.errors, manifestPath, "FDN4110",
                 "package manifest must be a regular file");
        return result;
    }
    const auto manifest = readPackageManifest(manifestPath);
    if (!manifest.value.has_value()) {
        result.errors = manifest.errors;
        return result;
    }
    if (!manifest.value->sdk.accepts(sdk)) {
        addError(result.errors, manifestPath, "FDN4110",
                 "package manifest does not support SDK " + sdk.string());
        return result;
    }
    const auto lockPath = *projectRoot / "foundation.lock";
    if (!realFile(lockPath)) {
        addError(result.errors, lockPath, "FDN4111", "package lock must be a regular file");
        return result;
    }
    const auto lock = readPackageLock(lockPath);
    if (!lock.value.has_value()) {
        result.errors = lock.errors;
        return result;
    }
    if (lock.value->rootName != manifest.value->name ||
        lock.value->rootVersion != manifest.value->version) {
        addError(result.errors, lockPath, "FDN4111",
                 "package lock root does not match the manifest");
        return result;
    }
    if (lock.value->target != target) {
        addError(result.errors, lockPath, "FDN4111",
                 "package lock target " + std::string(targetPlatformName(lock.value->target)) +
                     " does not match build target " +
                     std::string(targetPlatformName(target)));
        return result;
    }

    LockedPackageProject project;
    project.manifestPath = manifestPath;
    project.projectRoot = *projectRoot;
    project.manifest = *manifest.value;
    project.lock = *lock.value;
    auto inspectedRoot = project.manifest;
    if (!includeTestDependencies) {
        inspectedRoot.testSource.reset();
    }
    const auto rootSnapshot = inspectPackageSource(*projectRoot, inspectedRoot);
    if (!rootSnapshot.value.has_value()) {
        result.errors = rootSnapshot.errors;
        return result;
    }
    project.sources.push_back({project.manifest.name, *projectRoot,
                               *projectRoot / project.manifest.source, project.manifest,
                               PackageDependencyScope::Runtime});

    std::map<std::string, std::vector<std::string>> runtimeGraph;
    for (const auto &edge : project.lock.edges) {
        if (edge.scope == PackageDependencyScope::Runtime) {
            runtimeGraph[edge.parent].push_back(edge.dependency);
        }
    }
    std::set<std::string> runtimePackages;
    std::vector<std::string> pending{project.manifest.name};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        for (const auto &dependency : runtimeGraph[current]) {
            if (runtimePackages.insert(dependency).second) {
                pending.push_back(dependency);
            }
        }
    }

    for (const auto &package : project.lock.packages) {
        if (!includeTestDependencies && !runtimePackages.contains(package.name)) {
            continue;
        }
        std::filesystem::path packageRoot;
        if (package.kind == PackageLocationKind::Registry) {
            if (!cacheRoot.has_value()) {
                addError(result.errors, lockPath, "FDN4112",
                         "package cache path is unavailable for locked registry content");
                return result;
            }
            const auto verified = verifyPackageInCache(*cacheRoot, package);
            if (!verified.value.has_value()) {
                result.errors = verified.errors;
                return result;
            }
            packageRoot = *verified.value;
        } else {
            const auto canonical = canonicalDirectory(*projectRoot / package.location);
            if (!canonical.has_value()) {
                addError(result.errors, *projectRoot / package.location, "FDN4112",
                         "locked path dependency root is unavailable");
                return result;
            }
            packageRoot = *canonical;
        }
        const auto dependencyManifest =
            readPackageManifest(packageRoot / "foundation.package");
        if (!dependencyManifest.value.has_value()) {
            result.errors = dependencyManifest.errors;
            return result;
        }
        if (dependencyManifest.value->name != package.name ||
            dependencyManifest.value->version != package.version ||
            !dependencyManifest.value->sdk.accepts(sdk)) {
            addError(result.errors, packageRoot / "foundation.package", "FDN4112",
                     "locked package identity or SDK requirement does not match");
            return result;
        }
        if (package.kind == PackageLocationKind::Path) {
            const auto snapshot = inspectPackageSource(packageRoot, *dependencyManifest.value);
            if (!snapshot.value.has_value()) {
                result.errors = snapshot.errors;
                return result;
            }
            if (snapshot.value->digest != package.digest) {
                addError(result.errors, packageRoot, "FDN4112",
                         "locked path dependency digest does not match");
                return result;
            }
        }
        project.sources.push_back({package.name, packageRoot,
                                   packageRoot / dependencyManifest.value->source,
                                   *dependencyManifest.value,
                                   runtimePackages.contains(package.name)
                                       ? PackageDependencyScope::Runtime
                                       : PackageDependencyScope::Test});
    }
    if (!validateLockedGraph(project, includeTestDependencies, result.errors)) {
        return result;
    }
    result.value = std::move(project);
    return result;
}

} // namespace foundation
