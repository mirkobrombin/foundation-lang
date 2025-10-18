#include "foundation/package.hpp"

#include <algorithm>
#include <system_error>

namespace foundation {

namespace {

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

bool realDirectory(const std::filesystem::path &path, std::error_code &error) {
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_directory(status) &&
           !std::filesystem::is_symlink(status);
}

} // namespace

PackageParseResult<std::vector<PackageCandidate>>
readLocalPackageRegistry(const std::filesystem::path &registryRoot,
                         std::string_view identity, std::string_view packageName) {
    PackageParseResult<std::vector<PackageCandidate>> result;
    std::error_code error;
    if (!isValidPackageName(identity) || !isValidPackageName(packageName) ||
        !realDirectory(registryRoot, error)) {
        addError(result.errors, registryRoot, "FDN4060",
                 "local registry must have a valid identity, package name, and real root");
        return result;
    }
    const auto packageRoot = registryRoot / std::string(packageName);
    const auto packageStatus = std::filesystem::symlink_status(packageRoot, error);
    if (error == std::errc::no_such_file_or_directory ||
        packageStatus.type() == std::filesystem::file_type::not_found) {
        result.value = std::vector<PackageCandidate>{};
        return result;
    }
    if (error || !std::filesystem::is_directory(packageStatus) ||
        std::filesystem::is_symlink(packageStatus)) {
        addError(result.errors, packageRoot, "FDN4061",
                 "registry package root must be a real directory");
        return result;
    }

    std::vector<PackageCandidate> candidates;
    std::filesystem::directory_iterator iterator(packageRoot, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto root = iterator->path();
        const auto status = iterator->symlink_status(error);
        if (error) {
            break;
        }
        const auto version = parsePackageVersion(root.filename().string());
        if (!version.has_value() || !std::filesystem::is_directory(status) ||
            std::filesystem::is_symlink(status)) {
            addError(result.errors, root, "FDN4062",
                     "registry entries must be real directories named by semantic version");
            return result;
        }
        const auto manifestStatus =
            std::filesystem::symlink_status(root / "foundation.package", error);
        if (error || !std::filesystem::is_regular_file(manifestStatus) ||
            std::filesystem::is_symlink(manifestStatus)) {
            addError(result.errors, root / "foundation.package", "FDN4063",
                     "registry manifest must be a regular file");
            return result;
        }
        const auto parsed = readPackageManifest(root / "foundation.package");
        if (!parsed.value.has_value()) {
            result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
            return result;
        }
        if (parsed.value->name != packageName || parsed.value->version != *version) {
            addError(result.errors, root / "foundation.package", "FDN4063",
                     "registry path does not match package manifest identity");
            return result;
        }
        const auto snapshot = inspectPackageSource(root, *parsed.value);
        if (!snapshot.value.has_value()) {
            result.errors.insert(result.errors.end(), snapshot.errors.begin(),
                                 snapshot.errors.end());
            return result;
        }
        candidates.push_back({*parsed.value, snapshot.value->digest,
                              PackageLocationKind::Registry, std::string(identity), root});
        iterator.increment(error);
    }
    if (error) {
        addError(result.errors, packageRoot, "FDN4064", "cannot enumerate local registry");
        return result;
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.manifest.version < right.manifest.version;
    });
    result.value = std::move(candidates);
    return result;
}

} // namespace foundation
