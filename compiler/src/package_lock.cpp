#include "foundation/package.hpp"

#include <atomic>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace foundation {

namespace {

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

std::optional<std::string> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return std::nullopt;
    }
    return contents.str();
}

bool replaceFile(const std::filesystem::path &source,
                 const std::filesystem::path &destination, std::error_code &error) {
#ifdef _WIN32
    DWORD lastError = ERROR_SUCCESS;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        if (MoveFileExW(source.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
            error.clear();
            return true;
        }
        lastError = GetLastError();
        if (lastError != ERROR_ACCESS_DENIED && lastError != ERROR_SHARING_VIOLATION &&
            lastError != ERROR_LOCK_VIOLATION) {
            break;
        }
        if (attempt + 1 < 100) {
            Sleep(1);
        }
    }
    error = std::error_code(static_cast<int>(lastError), std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

PackageMutationResult writePackageLockAtomically(const std::filesystem::path &path,
                                                 const PackageLock &lock) {
    PackageMutationResult result;
    if (path.empty() || path.filename().empty()) {
        addError(result.errors, path, "FDN4080", "lock path must name a file");
        return result;
    }
    const auto content = renderPackageLock(lock);
    const auto parsed = parsePackageLock(path, content);
    if (!parsed.value.has_value()) {
        result.errors = parsed.errors;
        return result;
    }

    const auto parent = path.parent_path().empty() ? std::filesystem::path{"."}
                                                   : path.parent_path();
    std::error_code error;
    const auto parentStatus = std::filesystem::symlink_status(parent, error);
    if (error || !std::filesystem::is_directory(parentStatus) ||
        std::filesystem::is_symlink(parentStatus)) {
        addError(result.errors, parent, "FDN4081",
                 "lock parent must be an existing real directory");
        return result;
    }

    const auto destinationStatus = std::filesystem::symlink_status(path, error);
    if (!error && destinationStatus.type() != std::filesystem::file_type::not_found) {
        if (!std::filesystem::is_regular_file(destinationStatus) ||
            std::filesystem::is_symlink(destinationStatus)) {
            addError(result.errors, path, "FDN4082",
                     "lock destination must be a regular file or absent");
            return result;
        }
        const auto current = readFile(path);
        if (!current.has_value()) {
            addError(result.errors, path, "FDN4083", "cannot read existing lockfile");
            return result;
        }
        if (*current == content) {
            return result;
        }
    } else if (error && error != std::errc::no_such_file_or_directory) {
        addError(result.errors, path, "FDN4083", "cannot inspect lock destination");
        return result;
    }
    error.clear();

    static std::atomic<unsigned long> sequence{};
    std::filesystem::path staging;
    auto created = false;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        staging = parent / ('.' + path.filename().string() + ".tmp-" +
                            std::to_string(processId()) + '-' +
                            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (std::filesystem::create_directory(staging, error)) {
            created = true;
            break;
        }
        if (!error || error == std::errc::file_exists) {
            error.clear();
            continue;
        }
        break;
    }
    if (!created) {
        addError(result.errors, staging, "FDN4084", "cannot create lock staging directory");
        return result;
    }
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const auto temporary = staging / path.filename();
    {
        std::ofstream output(temporary, std::ios::binary);
        output << content;
        output.flush();
        if (!output) {
            cleanup();
            addError(result.errors, temporary, "FDN4085", "cannot write staged lockfile");
            return result;
        }
    }
    const auto verified = readPackageLock(temporary);
    if (!verified.value.has_value() || renderPackageLock(*verified.value) != content) {
        cleanup();
        addError(result.errors, temporary, "FDN4086", "staged lockfile failed verification");
        return result;
    }
    if (!replaceFile(temporary, path, error)) {
        cleanup();
        addError(result.errors, path, "FDN4087", "cannot publish verified lockfile");
        return result;
    }
    cleanup();
    result.changed.push_back(path);
    return result;
}

} // namespace foundation
