#include "foundation/package.hpp"

#include "foundation/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <span>

namespace foundation {

namespace {

constexpr std::size_t maxSourceFiles = 8192;
constexpr std::uintmax_t maxSourceFileBytes = 32U * 1024U * 1024U;
constexpr std::uintmax_t maxSourceBytes = 256U * 1024U * 1024U;
constexpr int maxSourceDepth = 64;

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

bool portablePart(std::string_view part) {
    if (part.empty() || part == "." || part == ".." || part.back() == ' ' ||
        part.back() == '.') {
        return false;
    }
    return std::all_of(part.begin(), part.end(), [](const auto byte) {
        const auto character = static_cast<unsigned char>(byte);
        return std::isalnum(character) || byte == '.' || byte == '_' || byte == '-';
    });
}

bool portablePath(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [](const auto &part) {
        return portablePart(part.generic_string());
    });
}

std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const auto byte) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
    });
    return value;
}

void updateSize(Sha256 &hash, std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[bytes.size() - index - 1] = static_cast<std::byte>(value >> (index * 8U));
    }
    hash.update(bytes);
}

void updateEntryHeader(Sha256 &hash, std::string_view path, std::uint64_t size) {
    updateSize(hash, path.size());
    hash.update(path);
    updateSize(hash, size);
}

bool hashFile(Sha256 &hash, const std::filesystem::path &path, std::uintmax_t expectedSize) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::array<char, 16384> buffer{};
    std::uintmax_t readBytes{};
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           input.gcount() != 0) {
        const auto count = static_cast<std::size_t>(input.gcount());
        hash.update(std::as_bytes(std::span{buffer.data(), count}));
        readBytes += count;
        if (readBytes > expectedSize) {
            return false;
        }
    }
    return !input.bad() && readBytes == expectedSize;
}

} // namespace

PackageParseResult<PackageSourceSnapshot>
inspectPackageSource(const std::filesystem::path &packageDirectory,
                     const PackageManifest &manifest) {
    PackageParseResult<PackageSourceSnapshot> result;
    PackageSourceSnapshot snapshot;
    std::error_code error;
    const auto packageStatus = std::filesystem::symlink_status(packageDirectory, error);
    if (error || !std::filesystem::is_directory(packageStatus) ||
        std::filesystem::is_symlink(packageStatus)) {
        addError(result.errors, packageDirectory, "FDN4030",
                 "package root must be a real directory");
        return result;
    }

    std::vector<std::filesystem::path> sourceDirectories{packageDirectory / manifest.source};
    if (manifest.testSource.has_value()) {
        sourceDirectories.push_back(packageDirectory / *manifest.testSource);
    }
    std::set<std::string> foldedPaths;
    for (const auto &sourceDirectory : sourceDirectories) {
        error.clear();
        const auto sourceStatus = std::filesystem::symlink_status(sourceDirectory, error);
        if (error || !std::filesystem::is_directory(sourceStatus) ||
            std::filesystem::is_symlink(sourceStatus)) {
            addError(result.errors, sourceDirectory, "FDN4031",
                     "package source must be a real directory");
            return result;
        }

        std::filesystem::recursive_directory_iterator iterator(sourceDirectory, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            const auto status = iterator->symlink_status(error);
            if (error) {
                break;
            }
            if (iterator.depth() >= maxSourceDepth) {
                addError(result.errors, path, "FDN4032",
                         "package source exceeds depth limit");
                return result;
            }
            if (std::filesystem::is_symlink(status)) {
                addError(result.errors, path, "FDN4033",
                         "package source cannot contain symlinks");
                return result;
            }
            if (std::filesystem::is_directory(status)) {
                iterator.increment(error);
                continue;
            }
            if (!std::filesystem::is_regular_file(status)) {
                addError(result.errors, path, "FDN4034",
                         "package source can contain only regular files");
                return result;
            }
            const auto relative = path.lexically_relative(packageDirectory).lexically_normal();
            if (!portablePath(relative)) {
                addError(result.errors, path, "FDN4035",
                         "package source path is not portable");
                return result;
            }
            if (!foldedPaths.insert(folded(relative.generic_string())).second) {
                addError(result.errors, path, "FDN4036",
                         "package source paths collide without case sensitivity");
                return result;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > maxSourceFileBytes ||
                snapshot.totalBytes > maxSourceBytes - size) {
                addError(result.errors, path, "FDN4037",
                         "package source exceeds size limit");
                return result;
            }
            if (snapshot.files.size() == maxSourceFiles) {
                addError(result.errors, path, "FDN4038",
                         "package source exceeds file limit");
                return result;
            }
            snapshot.files.push_back({relative, size});
            snapshot.totalBytes += size;
            iterator.increment(error);
        }
        if (error) {
            addError(result.errors, sourceDirectory, "FDN4039",
                     "cannot inspect package source");
            return result;
        }
    }
    std::sort(snapshot.files.begin(), snapshot.files.end(), [](const auto &left,
                                                               const auto &right) {
        return left.path.generic_string() < right.path.generic_string();
    });

    for (const auto &foreign : manifest.foreign) {
        if (foreign.kind != "path") {
            continue;
        }
        const auto resolver = std::filesystem::path(foreign.resolver).lexically_normal();
        const auto ownsNativeSource = std::any_of(
            manifest.nativeSources.begin(), manifest.nativeSources.end(),
            [&](const auto &source) {
                const auto relative = source.path.lexically_relative(resolver);
                return !relative.empty() && !relative.is_absolute() &&
                       *relative.begin() != "..";
            });
        if (!ownsNativeSource) {
            continue;
        }
        auto current = packageDirectory;
        for (const auto &part : resolver) {
            current /= part;
            error.clear();
            const auto status = std::filesystem::symlink_status(current, error);
            if (error || std::filesystem::is_symlink(status)) {
                addError(result.errors, current, "FDN4057",
                         "foreign path source cannot use symlinked path components");
                return result;
            }
        }
        const auto foreignSnapshot = inspectForeignSource(packageDirectory / resolver);
        if (!foreignSnapshot.value.has_value()) {
            result.errors.insert(result.errors.end(), foreignSnapshot.errors.begin(),
                                 foreignSnapshot.errors.end());
            return result;
        }
        for (const auto &file : foreignSnapshot.value->files) {
            const auto relative = (resolver / file.path).lexically_normal();
            if (!portablePath(relative)) {
                addError(result.errors, packageDirectory / relative, "FDN4035",
                         "package source path is not portable");
                return result;
            }
            if (!foldedPaths.insert(folded(relative.generic_string())).second) {
                addError(result.errors, packageDirectory / relative, "FDN4036",
                         "package source paths collide without case sensitivity");
                return result;
            }
            if (snapshot.totalBytes > maxSourceBytes - file.size) {
                addError(result.errors, packageDirectory / relative, "FDN4037",
                         "package source exceeds size limit");
                return result;
            }
            if (snapshot.files.size() == maxSourceFiles) {
                addError(result.errors, packageDirectory / relative, "FDN4038",
                         "package source exceeds file limit");
                return result;
            }
            snapshot.files.push_back({relative, file.size});
            snapshot.totalBytes += file.size;
        }
    }
    std::sort(snapshot.files.begin(), snapshot.files.end(), [](const auto &left,
                                                               const auto &right) {
        return left.path.generic_string() < right.path.generic_string();
    });

    Sha256 hash;
    hash.update("foundation.package.digest/v1");
    const auto canonicalManifest = renderPackageManifest(manifest);
    updateEntryHeader(hash, "foundation.package", canonicalManifest.size());
    hash.update(canonicalManifest);
    for (const auto &file : snapshot.files) {
        const auto relative = file.path.generic_string();
        updateEntryHeader(hash, relative, file.size);
        if (!hashFile(hash, packageDirectory / file.path, file.size)) {
            addError(result.errors, packageDirectory / file.path, "FDN4040",
                     "package source changed while it was read");
            return result;
        }
    }
    const auto digestBytes = hash.finish();
    static constexpr char digits[] = "0123456789abcdef";
    snapshot.digest = "sha256:";
    snapshot.digest.reserve(7 + digestBytes.size() * 2);
    for (const auto byte : digestBytes) {
        const auto value = std::to_integer<unsigned int>(byte);
        snapshot.digest.push_back(digits[value >> 4U]);
        snapshot.digest.push_back(digits[value & 0x0fU]);
    }
    result.value = std::move(snapshot);
    return result;
}

PackageParseResult<PackageSourceSnapshot>
inspectForeignSource(const std::filesystem::path &sourceDirectory) {
    PackageParseResult<PackageSourceSnapshot> result;
    PackageSourceSnapshot snapshot;
    std::error_code error;
    const auto sourceStatus = std::filesystem::symlink_status(sourceDirectory, error);
    if (error || !std::filesystem::is_directory(sourceStatus) ||
        std::filesystem::is_symlink(sourceStatus)) {
        addError(result.errors, sourceDirectory, "FDN4057",
                 "foreign path source must be a real directory");
        return result;
    }

    std::set<std::string> foldedPaths;
    std::filesystem::recursive_directory_iterator iterator(sourceDirectory, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto path = iterator->path();
        const auto status = iterator->symlink_status(error);
        if (error) {
            break;
        }
        if (iterator.depth() >= maxSourceDepth) {
            addError(result.errors, path, "FDN4057", "foreign path source exceeds depth limit");
            return result;
        }
        if (std::filesystem::is_symlink(status)) {
            addError(result.errors, path, "FDN4057", "foreign path source cannot contain symlinks");
            return result;
        }
        if (std::filesystem::is_directory(status)) {
            iterator.increment(error);
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            addError(result.errors, path, "FDN4057",
                     "foreign path source can contain only regular files");
            return result;
        }
        const auto relative = path.lexically_relative(sourceDirectory).lexically_normal();
        if (!portablePath(relative)) {
            addError(result.errors, path, "FDN4057", "foreign path source is not portable");
            return result;
        }
        if (!foldedPaths.insert(folded(relative.generic_string())).second) {
            addError(result.errors, path, "FDN4057",
                     "foreign path source paths collide without case sensitivity");
            return result;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > maxSourceFileBytes || snapshot.totalBytes > maxSourceBytes - size) {
            addError(result.errors, path, "FDN4057", "foreign path source exceeds size limit");
            return result;
        }
        if (snapshot.files.size() == maxSourceFiles) {
            addError(result.errors, path, "FDN4057", "foreign path source exceeds file limit");
            return result;
        }
        snapshot.files.push_back({relative, size});
        snapshot.totalBytes += size;
        iterator.increment(error);
    }
    if (error) {
        addError(result.errors, sourceDirectory, "FDN4057", "cannot inspect foreign path source");
        return result;
    }
    if (snapshot.files.empty()) {
        addError(result.errors, sourceDirectory, "FDN4057", "foreign path source is empty");
        return result;
    }
    std::sort(snapshot.files.begin(), snapshot.files.end(), [](const auto &left,
                                                               const auto &right) {
        return left.path.generic_string() < right.path.generic_string();
    });

    Sha256 hash;
    hash.update("foundation.foreign.path.digest/v1");
    for (const auto &file : snapshot.files) {
        const auto relative = file.path.generic_string();
        updateEntryHeader(hash, relative, file.size);
        if (!hashFile(hash, sourceDirectory / file.path, file.size)) {
            addError(result.errors, sourceDirectory / file.path, "FDN4057",
                     "foreign path source changed while it was read");
            return result;
        }
    }
    const auto digestBytes = hash.finish();
    static constexpr char digits[] = "0123456789abcdef";
    snapshot.digest = "sha256:";
    snapshot.digest.reserve(7 + digestBytes.size() * 2);
    for (const auto byte : digestBytes) {
        const auto value = std::to_integer<unsigned int>(byte);
        snapshot.digest.push_back(digits[value >> 4U]);
        snapshot.digest.push_back(digits[value & 0x0fU]);
    }
    result.value = std::move(snapshot);
    return result;
}

} // namespace foundation
