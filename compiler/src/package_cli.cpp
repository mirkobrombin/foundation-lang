#include "foundation/package_cli.hpp"

#include "foundation/driver.hpp"
#include "foundation/package.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace foundation {

namespace {

struct PackageOptions {
    std::filesystem::path project;
    std::optional<std::filesystem::path> cache;
    std::optional<TargetPlatform> target;
    std::vector<PackageRegistryRoot> registries;
};

struct PackageExportOptions {
    std::filesystem::path project;
    std::filesystem::path output;
    std::optional<PackageExportFormat> format;
    std::vector<std::filesystem::path> nativeInputs;
    BackendKind backend{defaultBackendKind()};
};

void usage(std::ostream &output) {
    output << "usage:\n"
           << "  foundationc package init <project> <package-name>\n"
           << "  foundationc package resolve <project> [--target <platform>] "
              "[--registry <identity>=<path>]...\n"
           << "  foundationc package fetch <project> [--cache <path>] "
              "[--registry <identity>=<path>]...\n"
           << "  foundationc package verify <project> [--cache <path>]\n"
           << "  foundationc package inspect <project>\n"
           << "  foundationc package prune <project> [--cache <path>]\n"
           << "  foundationc package export <project> -o <directory>"
              " --format <zig|rust|go-cgo|go-dynamic|go-source>"
              " [--backend <llvm|c>] [--native <input>]...\n";
}

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::string code, std::string message) {
    errors.push_back({path, 1, 1, std::move(code), std::move(message)});
}

int printErrors(const std::vector<PackageError> &errors) {
    for (const auto &error : errors) {
        std::cerr << renderPackageError(error);
    }
    return 1;
}

void printChanges(const std::vector<std::filesystem::path> &changed) {
    for (const auto &path : changed) {
        std::cout << "changed " << path.generic_string() << '\n';
    }
}

int printChangesAndErrors(const std::vector<std::filesystem::path> &changed,
                          const std::vector<PackageError> &errors) {
    printChanges(changed);
    return printErrors(errors);
}

bool publishWithoutReplacement(const std::filesystem::path &source,
                               const std::filesystem::path &destination) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::link(source.c_str(), destination.c_str()) == 0;
#endif
}

bool parseOptions(int argc, char **argv, int start, PackageOptions &options) {
    for (auto index = start; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        const std::string_view value = argv[index + 1];
        if (option == "--cache") {
            if (options.cache.has_value() || value.empty()) {
                return false;
            }
            options.cache = std::filesystem::path{value};
        } else if (option == "--target") {
            if (options.target.has_value()) {
                return false;
            }
            options.target = parseTargetPlatform(value);
            if (!options.target.has_value()) {
                return false;
            }
        } else if (option == "--registry") {
            const auto separator = value.find('=');
            if (separator == std::string_view::npos || separator == 0 ||
                separator + 1 == value.size()) {
                return false;
            }
            options.registries.push_back(
                {std::string(value.substr(0, separator)),
                 std::filesystem::path{value.substr(separator + 1)}});
        } else {
            return false;
        }
    }
    return true;
}

bool parseExportOptions(int argc, char **argv, int start,
                        PackageExportOptions &options) {
    auto outputSeen = false;
    auto backendSeen = false;
    for (auto index = start; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        const std::string_view value = argv[index + 1];
        if (option == "-o" && !outputSeen) {
            options.output = value;
            outputSeen = true;
        } else if (option == "--format" && !options.format.has_value()) {
            options.format = parsePackageExportFormat(value);
            if (!options.format.has_value()) {
                return false;
            }
        } else if (option == "--native") {
            options.nativeInputs.emplace_back(value);
        } else if (option == "--backend" && !backendSeen) {
            const auto backend = parseBackendKind(value);
            if (!backend.has_value()) {
                return false;
            }
            options.backend = *backend;
            backendSeen = true;
        } else {
            return false;
        }
    }
    return outputSeen && options.format.has_value();
}

std::filesystem::path manifestPath(const std::filesystem::path &project) {
    return project.filename() == "foundation.package" ? project
                                                       : project / "foundation.package";
}

std::filesystem::path lockPath(const std::filesystem::path &project) {
    const auto manifest = manifestPath(project);
    return manifest.parent_path() / "foundation.lock";
}

std::optional<std::filesystem::path> cachePath(const PackageOptions &options,
                                               std::vector<PackageError> &errors) {
    if (options.cache.has_value()) {
        return options.cache;
    }
    const auto cache = defaultPackageCachePath();
    if (!cache.has_value()) {
        addError(errors, options.project, "FDN4100",
                 "package cache path is unavailable; pass --cache");
    }
    return cache;
}

PackageParseResult<PackageResolution> resolve(const PackageOptions &options,
                                              TargetPlatform target) {
    const auto path = manifestPath(options.project);
    const auto manifest = readPackageManifest(path);
    if (!manifest.value.has_value()) {
        return {std::nullopt, manifest.errors};
    }
    const auto sdk = *parsePackageVersion("0.1.0");
    return resolveProjectPackages(path, *manifest.value, sdk, target, options.registries);
}

int initialize(const std::filesystem::path &project, std::string_view name) {
    std::vector<PackageError> errors;
    if (!isValidPackageName(name)) {
        addError(errors, project, "FDN4101", "package name is invalid");
        return printErrors(errors);
    }
    std::error_code error;
    std::vector<std::filesystem::path> changed;
    const auto status = std::filesystem::symlink_status(project, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        error.clear();
        std::filesystem::create_directories(project, error);
        if (error) {
            addError(errors, project, "FDN4102", "cannot create package project directory");
            return printChangesAndErrors(changed, errors);
        }
        changed.push_back(project);
    } else if (error || !std::filesystem::is_directory(status) ||
               std::filesystem::is_symlink(status)) {
        addError(errors, project, "FDN4102", "package project must be a real directory");
        return printErrors(errors);
    }

    const auto manifest = project / "foundation.package";
    const auto manifestStatus = std::filesystem::symlink_status(manifest, error);
    if (!error && manifestStatus.type() != std::filesystem::file_type::not_found) {
        addError(errors, manifest, "FDN4103", "package manifest already exists");
        return printChangesAndErrors(changed, errors);
    }
    if (error != std::errc::no_such_file_or_directory && error) {
        addError(errors, manifest, "FDN4103", "cannot inspect package manifest path");
        return printChangesAndErrors(changed, errors);
    }
    error.clear();

    const auto source = project / "src";
    const auto sourceStatus = std::filesystem::symlink_status(source, error);
    if (error == std::errc::no_such_file_or_directory ||
        sourceStatus.type() == std::filesystem::file_type::not_found) {
        error.clear();
        std::filesystem::create_directory(source, error);
        if (error) {
            addError(errors, source, "FDN4102", "cannot create package source directory");
            return printChangesAndErrors(changed, errors);
        }
        changed.push_back(source);
    } else if (error || !std::filesystem::is_directory(sourceStatus) ||
               std::filesystem::is_symlink(sourceStatus)) {
        addError(errors, source, "FDN4102", "package source must be a real directory");
        return printChangesAndErrors(changed, errors);
    }
    error.clear();
    PackageManifest value;
    value.name = std::string(name);
    value.version = *parsePackageVersion("0.1.0");
    value.language = 1;
    value.languageExplicit = true;
    value.sdk = *parsePackageRequirement("*");
    value.codeStandardExplicit = true;
    value.source = "src";

    static std::atomic<unsigned long> sequence{};
#ifdef _WIN32
    const auto pid = static_cast<long>(_getpid());
#else
    const auto pid = static_cast<long>(getpid());
#endif
    std::filesystem::path staging;
    auto staged = false;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        staging = project / (".foundation.package.tmp-" + std::to_string(pid) + '-' +
                             std::to_string(sequence.fetch_add(1)));
        if (std::filesystem::create_directory(staging, error)) {
            staged = true;
            break;
        }
        if (!error || error == std::errc::file_exists) {
            error.clear();
            continue;
        }
        break;
    }
    if (!staged) {
        addError(errors, staging, "FDN4104", "cannot create manifest staging directory");
        return printChangesAndErrors(changed, errors);
    }
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const auto temporary = staging / "foundation.package";
    {
        std::ofstream output(temporary, std::ios::binary);
        output << renderPackageManifest(value);
        output.flush();
        if (!output) {
            cleanup();
            addError(errors, temporary, "FDN4104", "cannot write package manifest");
            return printChangesAndErrors(changed, errors);
        }
    }
    const auto verified = readPackageManifest(temporary);
    if (!verified.value.has_value()) {
        cleanup();
        return printChangesAndErrors(changed, verified.errors);
    }
    if (!publishWithoutReplacement(temporary, manifest)) {
        cleanup();
        addError(errors, manifest, "FDN4104", "cannot publish package manifest");
        return printChangesAndErrors(changed, errors);
    }
    cleanup();
    changed.push_back(manifest);
    printChanges(changed);
    return 0;
}

int resolveCommand(const PackageOptions &options) {
    const auto target = options.target.value_or(hostTargetPlatform());
    const auto resolution = resolve(options, target);
    if (!resolution.value.has_value()) {
        return printErrors(resolution.errors);
    }
    const auto path = lockPath(options.project);
    const auto written = writePackageLockAtomically(path, resolution.value->lock);
    if (!written.errors.empty()) {
        return printErrors(written.errors);
    }
    if (written.changed.empty()) {
        std::cout << "unchanged " << path.generic_string() << '\n';
    } else {
        for (const auto &changed : written.changed) {
            std::cout << "changed " << changed.generic_string() << '\n';
        }
    }
    std::cout << "resolved " << resolution.value->lock.packages.size() << " packages\n";
    return 0;
}

bool sameLock(const PackageLock &left, const PackageLock &right) {
    return renderPackageLock(left) == renderPackageLock(right);
}

int fetchCommand(const PackageOptions &options) {
    const auto existing = readPackageLock(lockPath(options.project));
    if (!existing.value.has_value()) {
        return printErrors(existing.errors);
    }
    if (options.target.has_value() && *options.target != existing.value->target) {
        std::vector<PackageError> errors;
        addError(errors, lockPath(options.project), "FDN4105",
                 "requested target does not match the lockfile");
        return printErrors(errors);
    }
    const auto resolution = resolve(options, existing.value->target);
    if (!resolution.value.has_value()) {
        return printErrors(resolution.errors);
    }
    if (!sameLock(*existing.value, resolution.value->lock)) {
        std::vector<PackageError> errors;
        addError(errors, lockPath(options.project), "FDN4105",
                 "package lock is stale; run package resolve");
        return printErrors(errors);
    }
    std::vector<PackageError> errors;
    const auto cache = cachePath(options, errors);
    if (!cache.has_value()) {
        return printErrors(errors);
    }
    for (const auto &candidate : resolution.value->packages) {
        if (candidate.kind != PackageLocationKind::Registry) {
            continue;
        }
        const LockedPackage locked{candidate.manifest.name, candidate.manifest.version,
                                   candidate.digest, candidate.kind, candidate.location};
        const auto cached = verifyPackageInCache(*cache, locked);
        if (cached.value.has_value()) {
            std::cout << "verified " << cached.value->generic_string() << '\n';
            continue;
        }
        const auto absent = std::all_of(cached.errors.begin(), cached.errors.end(),
                                        [](const auto &error) {
                                            return error.code == "FDN4070";
                                        });
        if (!absent) {
            return printErrors(cached.errors);
        }
        const auto installed = installPackageInCache(*cache, candidate);
        if (!installed.value.has_value()) {
            return printErrors(installed.errors);
        }
        std::cout << "changed " << installed.value->generic_string() << '\n';
    }
    return 0;
}

int verifyCommand(const PackageOptions &options) {
    const auto manifest = readPackageManifest(manifestPath(options.project));
    if (!manifest.value.has_value()) {
        return printErrors(manifest.errors);
    }
    const auto lock = readPackageLock(lockPath(options.project));
    if (!lock.value.has_value()) {
        return printErrors(lock.errors);
    }
    if (manifest.value->name != lock.value->rootName ||
        manifest.value->version != lock.value->rootVersion) {
        std::vector<PackageError> errors;
        addError(errors, lockPath(options.project), "FDN4105",
                 "package lock root does not match the manifest");
        return printErrors(errors);
    }
    std::vector<PackageError> errors;
    const auto cache = cachePath(options, errors);
    if (!cache.has_value()) {
        return printErrors(errors);
    }
    const auto root = manifestPath(options.project).parent_path();
    for (const auto &package : lock.value->packages) {
        if (package.kind == PackageLocationKind::Registry) {
            const auto verified = verifyPackageInCache(*cache, package);
            if (!verified.value.has_value()) {
                return printErrors(verified.errors);
            }
            std::cout << "verified " << verified.value->generic_string() << '\n';
            continue;
        }
        const auto packageRoot = root / package.location;
        const auto dependencyManifest =
            readPackageManifest(packageRoot / "foundation.package");
        if (!dependencyManifest.value.has_value()) {
            return printErrors(dependencyManifest.errors);
        }
        if (dependencyManifest.value->name != package.name ||
            dependencyManifest.value->version != package.version) {
            addError(errors, packageRoot, "FDN4106",
                     "path dependency identity does not match the lock");
            return printErrors(errors);
        }
        const auto snapshot = inspectPackageSource(packageRoot, *dependencyManifest.value);
        if (!snapshot.value.has_value()) {
            return printErrors(snapshot.errors);
        }
        if (snapshot.value->digest != package.digest) {
            addError(errors, packageRoot, "FDN4106",
                     "path dependency digest does not match the lock");
            return printErrors(errors);
        }
        std::cout << "verified " << packageRoot.generic_string() << '\n';
    }
    return 0;
}

int inspectCommand(const PackageOptions &options) {
    const auto manifest = readPackageManifest(manifestPath(options.project));
    if (!manifest.value.has_value()) {
        return printErrors(manifest.errors);
    }
    std::cout << renderPackageManifest(*manifest.value);
    std::error_code error;
    if (!std::filesystem::exists(lockPath(options.project), error) || error) {
        std::cout << "lock absent\n";
        return 0;
    }
    const auto lock = readPackageLock(lockPath(options.project));
    if (!lock.value.has_value()) {
        return printErrors(lock.errors);
    }
    std::cout << '\n' << renderPackageLock(*lock.value);
    return 0;
}

int pruneCommand(const PackageOptions &options) {
    const auto lock = readPackageLock(lockPath(options.project));
    if (!lock.value.has_value()) {
        return printErrors(lock.errors);
    }
    std::vector<PackageError> errors;
    const auto cache = cachePath(options, errors);
    if (!cache.has_value()) {
        return printErrors(errors);
    }
    std::vector<std::string> keep;
    for (const auto &package : lock.value->packages) {
        if (package.kind == PackageLocationKind::Registry) {
            keep.push_back(package.digest);
        }
    }
    const auto pruned = prunePackageCache(*cache, keep);
    if (!pruned.errors.empty()) {
        return printErrors(pruned.errors);
    }
    if (pruned.changed.empty()) {
        std::cout << "unchanged " << cache->generic_string() << '\n';
    } else {
        for (const auto &changed : pruned.changed) {
            std::cout << "changed " << changed.generic_string() << '\n';
        }
    }
    return 0;
}

} // namespace

int runPackageCommand(int argc, char **argv) {
    if (argc == 5 && std::string_view(argv[2]) == "init") {
        return initialize(std::filesystem::path{argv[3]}, argv[4]);
    }
    if (argc < 4) {
        usage(std::cerr);
        return 2;
    }
    if (std::string_view(argv[2]) == "export") {
        PackageExportOptions options;
        options.project = std::filesystem::path{argv[3]};
        if (!parseExportOptions(argc, argv, 4, options)) {
            usage(std::cerr);
            return 2;
        }
        return exportPackage(options.project, options.output, *options.format,
                             options.nativeInputs, options.backend);
    }
    PackageOptions options;
    options.project = std::filesystem::path{argv[3]};
    if (!parseOptions(argc, argv, 4, options)) {
        usage(std::cerr);
        return 2;
    }
    const std::string_view command = argv[2];
    if (command == "resolve") {
        return resolveCommand(options);
    }
    if (command == "fetch") {
        return fetchCommand(options);
    }
    if (command == "verify") {
        return verifyCommand(options);
    }
    if (command == "inspect") {
        return inspectCommand(options);
    }
    if (command == "prune") {
        return pruneCommand(options);
    }
    usage(std::cerr);
    return 2;
}

} // namespace foundation
