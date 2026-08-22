#include "foundation/package.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace foundation {

namespace {

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

bool digestHex(std::string_view digest) {
    if (digest.size() != 71 || !digest.starts_with("sha256:")) {
        return false;
    }
    return std::all_of(digest.begin() + 7, digest.end(), [](const auto byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}

bool missing(const std::error_code &error) {
    return error == std::errc::no_such_file_or_directory;
}

std::filesystem::path cachePath(const std::filesystem::path &root, std::string_view digest) {
    return root / "sha256" / std::string(digest.substr(7));
}

bool realFile(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
           !std::filesystem::is_symlink(status);
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

PackageParseResult<std::filesystem::path>
verifyAt(const std::filesystem::path &path, const LockedPackage &package) {
    PackageParseResult<std::filesystem::path> result;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_directory(status) ||
        std::filesystem::is_symlink(status)) {
        addError(result.errors, path, "FDN4070", "locked package is missing from cache");
        return result;
    }
    if (!realFile(path / "foundation.package")) {
        addError(result.errors, path / "foundation.package", "FDN4071",
                 "cached package manifest must be a regular file");
        return result;
    }
    const auto manifest = readPackageManifest(path / "foundation.package");
    if (!manifest.value.has_value()) {
        result.errors = manifest.errors;
        return result;
    }
    if (manifest.value->name != package.name || manifest.value->version != package.version) {
        addError(result.errors, path / "foundation.package", "FDN4071",
                 "cached package identity does not match lock");
        return result;
    }
    const auto snapshot = inspectPackageSource(path, *manifest.value);
    if (!snapshot.value.has_value()) {
        result.errors = snapshot.errors;
        return result;
    }
    if (snapshot.value->digest != package.digest) {
        addError(result.errors, path, "FDN4072", "cached package digest does not match lock");
        return result;
    }
    result.value = path;
    return result;
}

bool validateExistingCacheRoot(const std::filesystem::path &cacheRoot,
                               std::vector<PackageError> &errors) {
    std::error_code error;
    const auto cacheStatus = std::filesystem::symlink_status(cacheRoot, error);
    if (missing(error) || cacheStatus.type() == std::filesystem::file_type::not_found) {
        return true;
    }
    if (error || !std::filesystem::is_directory(cacheStatus) ||
        std::filesystem::is_symlink(cacheStatus)) {
        addError(errors, cacheRoot, "FDN4075", "cache root must be a real directory");
        return false;
    }
    const auto algorithmRoot = cacheRoot / "sha256";
    const auto algorithmStatus = std::filesystem::symlink_status(algorithmRoot, error);
    if (missing(error) || algorithmStatus.type() == std::filesystem::file_type::not_found) {
        return true;
    }
    if (error || !std::filesystem::is_directory(algorithmStatus) ||
        std::filesystem::is_symlink(algorithmStatus)) {
        addError(errors, algorithmRoot, "FDN4075",
                 "cache algorithm root must be a real directory");
        return false;
    }
    return true;
}

} // namespace

PackageParseResult<std::filesystem::path>
verifyPackageInCache(const std::filesystem::path &cacheRoot, const LockedPackage &package) {
    PackageParseResult<std::filesystem::path> result;
    if (!digestHex(package.digest)) {
        addError(result.errors, cacheRoot, "FDN4073", "locked package has an invalid digest");
        return result;
    }
    if (!validateExistingCacheRoot(cacheRoot, result.errors)) {
        return result;
    }
    return verifyAt(cachePath(cacheRoot, package.digest), package);
}

PackageParseResult<std::filesystem::path>
installPackageInCache(const std::filesystem::path &cacheRoot,
                      const PackageCandidate &candidate) {
    PackageParseResult<std::filesystem::path> result;
    if (!digestHex(candidate.digest)) {
        addError(result.errors, candidate.root, "FDN4073",
                 "package candidate has an invalid digest");
        return result;
    }
    const auto snapshot = inspectPackageSource(candidate.root, candidate.manifest);
    if (!snapshot.value.has_value()) {
        result.errors = snapshot.errors;
        return result;
    }
    if (snapshot.value->digest != candidate.digest) {
        addError(result.errors, candidate.root, "FDN4074",
                 "package candidate changed before cache installation");
        return result;
    }

    std::error_code error;
    if (cacheRoot.empty()) {
        addError(result.errors, cacheRoot, "FDN4075", "cache root cannot be empty");
        return result;
    }
    std::filesystem::create_directories(cacheRoot, error);
    if (error) {
        addError(result.errors, cacheRoot, "FDN4075", "cannot create cache root");
        return result;
    }
    const auto cacheStatus = std::filesystem::symlink_status(cacheRoot, error);
    if (error || !std::filesystem::is_directory(cacheStatus) ||
        std::filesystem::is_symlink(cacheStatus)) {
        addError(result.errors, cacheRoot, "FDN4075",
                 "cache root must be a real directory");
        return result;
    }
    const auto algorithmRoot = cacheRoot / "sha256";
    std::filesystem::create_directories(algorithmRoot, error);
    if (error) {
        addError(result.errors, algorithmRoot, "FDN4075",
                 "cannot create cache algorithm root");
        return result;
    }
    const auto rootStatus = std::filesystem::symlink_status(algorithmRoot, error);
    if (error || !std::filesystem::is_directory(rootStatus) ||
        std::filesystem::is_symlink(rootStatus)) {
        addError(result.errors, algorithmRoot, "FDN4075",
                 "cache root must be a real directory");
        return result;
    }
    const auto destination = cachePath(cacheRoot, candidate.digest);
    const auto destinationStatus = std::filesystem::symlink_status(destination, error);
    if (error && !missing(error)) {
        addError(result.errors, destination, "FDN4078",
                 "cannot inspect package cache destination");
        return result;
    }
    if (!error && destinationStatus.type() != std::filesystem::file_type::not_found) {
        return verifyAt(destination, {candidate.manifest.name, candidate.manifest.version,
                                      candidate.digest, candidate.kind, candidate.location});
    }
    error.clear();
    static std::atomic<unsigned long> sequence{};
    std::filesystem::path temporary;
    auto created = false;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        temporary = algorithmRoot /
                    (".tmp-" + std::to_string(processId()) + '-' +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (std::filesystem::create_directory(temporary, error)) {
            created = true;
            break;
        }
        if (!error || error == std::errc::file_exists) {
            error.clear();
            continue;
        }
        break;
    }
    if (error || !created) {
        addError(result.errors, temporary, "FDN4076", "cannot create cache staging directory");
        return result;
    }
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    {
        std::ofstream manifest(temporary / "foundation.package", std::ios::binary);
        manifest << renderPackageManifest(candidate.manifest);
        if (!manifest) {
            cleanup();
            addError(result.errors, temporary, "FDN4077", "cannot write cache manifest");
            return result;
        }
    }
    for (const auto &file : snapshot.value->files) {
        const auto output = temporary / file.path;
        std::filesystem::create_directories(output.parent_path(), error);
        if (error || !std::filesystem::copy_file(candidate.root / file.path, output,
                                                 std::filesystem::copy_options::none, error)) {
            cleanup();
            addError(result.errors, output, "FDN4077", "cannot copy package into cache");
            return result;
        }
    }
    const LockedPackage locked{candidate.manifest.name, candidate.manifest.version,
                               candidate.digest, candidate.kind, candidate.location};
    const auto staged = verifyAt(temporary, locked);
    if (!staged.value.has_value()) {
        cleanup();
        result.errors = staged.errors;
        return result;
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        cleanup();
        const auto concurrent = verifyAt(destination, locked);
        if (concurrent.value.has_value()) {
            return concurrent;
        }
        addError(result.errors, destination, "FDN4078",
                 "cannot publish verified package cache entry");
        return result;
    }
    result.value = destination;
    return result;
}

PackageMutationResult prunePackageCache(const std::filesystem::path &cacheRoot,
                                        std::span<const std::string> keepDigests) {
    PackageMutationResult result;
    for (const auto &digest : keepDigests) {
        if (!digestHex(digest)) {
            addError(result.errors, cacheRoot, "FDN4073",
                     "cache keep set contains an invalid digest");
            return result;
        }
    }
    if (!validateExistingCacheRoot(cacheRoot, result.errors)) {
        return result;
    }

    std::error_code error;
    const auto algorithmRoot = cacheRoot / "sha256";
    const auto rootStatus = std::filesystem::symlink_status(algorithmRoot, error);
    if (missing(error) || rootStatus.type() == std::filesystem::file_type::not_found) {
        return result;
    }
    if (error) {
        addError(result.errors, algorithmRoot, "FDN4079", "cannot inspect package cache");
        return result;
    }

    std::set<std::string> keep;
    for (const auto &digest : keepDigests) {
        keep.insert(digest.substr(7));
    }
    std::vector<std::filesystem::path> remove;
    std::filesystem::directory_iterator iterator(algorithmRoot, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto path = iterator->path();
        const auto name = path.filename().string();
        const auto status = iterator->symlink_status(error);
        if (error) {
            break;
        }
        if (name.starts_with(".tmp-")) {
            remove.push_back(path);
        } else if (name.size() != 64 ||
                   !std::all_of(name.begin(), name.end(), [](const auto byte) {
                       return (byte >= '0' && byte <= '9') ||
                              (byte >= 'a' && byte <= 'f');
                   }) ||
                   !std::filesystem::is_directory(status) ||
                   std::filesystem::is_symlink(status)) {
            addError(result.errors, path, "FDN4079", "cache contains an unexpected entry");
            return result;
        } else if (!keep.contains(name)) {
            remove.push_back(path);
        }
        iterator.increment(error);
    }
    if (error) {
        addError(result.errors, algorithmRoot, "FDN4079", "cannot enumerate package cache");
        return result;
    }
    std::sort(remove.begin(), remove.end());
    for (const auto &path : remove) {
        std::filesystem::remove_all(path, error);
        if (error) {
            addError(result.errors, path, "FDN4079", "cannot remove package cache entry");
            return result;
        }
        result.changed.push_back(path);
    }
    return result;
}

} // namespace foundation
