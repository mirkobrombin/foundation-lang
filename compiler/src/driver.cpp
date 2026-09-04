#include "foundation/driver.hpp"

#include "foundation/application.hpp"
#include "foundation/codegen.hpp"
#include "foundation/documentation.hpp"
#include "foundation/formatter.hpp"
#include "foundation/imports.hpp"
#include "foundation/lint.hpp"
#include "foundation/lower.hpp"
#include "foundation/llvm_codegen.hpp"
#include "foundation/metadata.hpp"
#include "foundation/package.hpp"
#include "foundation/package_interface.hpp"
#include "foundation/process.hpp"
#include "foundation/project.hpp"
#include "foundation/sdk.hpp"
#include "foundation/sema.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace foundation {

namespace {

class TempDirectory {
  public:
    explicit TempDirectory(std::filesystem::path path) : path_(std::move(path)) {}

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    TempDirectory(TempDirectory &&other) noexcept : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    TempDirectory &operator=(TempDirectory &&) = delete;

    ~TempDirectory() {
        if (path_.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

bool prepareParent(const std::filesystem::path &path) {
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (!error) {
        return true;
    }
    std::cerr << "foundationc: cannot create output directory " << parent.string() << ": "
              << error.message() << '\n';
    return false;
}

bool writeFile(const std::filesystem::path &path, std::string_view contents) {
    if (!prepareParent(path)) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "foundationc: cannot write " << path.string() << '\n';
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (output) {
        return true;
    }
    std::cerr << "foundationc: failed while writing " << path.string() << '\n';
    return false;
}

std::optional<std::string> readSourceFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "foundationc: cannot read " << path.string() << '\n';
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        std::cerr << "foundationc: failed while reading " << path.string() << '\n';
        return std::nullopt;
    }
    return contents.str();
}

std::vector<std::string> llvmSourcePaths(const std::vector<DiagnosticSource> &sources) {
    std::vector<std::string> paths;
    paths.reserve(sources.size());
    for (const auto &source : sources) {
        paths.push_back(source.identity.empty() ? source.path : source.identity);
    }
    return paths;
}

std::vector<std::string> llvmReproducibleSourcePaths(
    const std::vector<DiagnosticSource> &sources) {
    std::vector<std::string> paths;
    paths.reserve(sources.size());
    for (const auto &source : sources) {
        paths.push_back(source.path.empty() ? source.identity : source.path);
    }
    return paths;
}

std::filesystem::path sourceIdentity(const std::filesystem::path &path) {
    std::error_code error;
    auto result = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    const auto canonical = std::filesystem::weakly_canonical(result, error);
    return error ? result.lexically_normal() : canonical;
}

std::vector<std::size_t> rootProjectSources(const std::filesystem::path &source,
                                            const ProjectAnalysis &analysis) {
    std::vector<std::size_t> result;
    std::error_code error;
    const auto sourceStatus = std::filesystem::status(source, error);
    if (!error && std::filesystem::is_regular_file(sourceStatus)) {
        const auto identity = sourceIdentity(source).generic_string();
        for (std::size_t index = 0; index < analysis.sources.size(); ++index) {
            if (analysis.sources[index].identity == identity) {
                result.push_back(index);
                break;
            }
        }
        return result;
    }
    if (error || !std::filesystem::is_directory(sourceStatus)) {
        return result;
    }
    const auto root = sourceIdentity(source);
    for (std::size_t index = 0; index < analysis.sources.size(); ++index) {
        const auto &input = analysis.sources[index];
        if (input.path.starts_with("packages/")) {
            continue;
        }
        const auto relative = std::filesystem::path(input.identity).lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..") {
            result.push_back(index);
        }
    }
    return result;
}

CodeStandardProfile projectCodeStandard(const std::filesystem::path &source) {
    const auto manifestPath = discoverPackageManifest(source);
    if (!manifestPath.has_value()) {
        return CodeStandardProfile::Standard;
    }
    const auto manifest = readPackageManifest(*manifestPath);
    return manifest.value.has_value() ? manifest.value->codeStandard
                                      : CodeStandardProfile::Standard;
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

bool replaceableFile(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        std::cerr << "foundationc: cannot inspect " << path.string() << ": " << error.message()
                  << '\n';
        return false;
    }
    if (std::filesystem::is_symlink(status)) {
        std::cerr << "foundationc: refusing to replace symbolic link " << path.string() << '\n';
        return false;
    }
    if (!std::filesystem::is_regular_file(status)) {
        std::cerr << "foundationc: refusing to replace non-regular file " << path.string() << '\n';
        return false;
    }
    return true;
}

bool writeExclusiveFile(const std::filesystem::path &path, std::string_view contents) {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::cerr << "foundationc: cannot create temporary file " << path.string()
                  << ": win32 error " << GetLastError() << '\n';
        return false;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto limit = static_cast<std::size_t>(std::numeric_limits<DWORD>::max());
        const auto chunk = static_cast<DWORD>(remaining < limit ? remaining : limit);
        DWORD count{};
        if (WriteFile(handle, contents.data() + offset, chunk, &count, nullptr) == 0 ||
            count == 0) {
            written = false;
            break;
        }
        offset += count;
    }
    if (written && FlushFileBuffers(handle) == 0) {
        written = false;
    }
    if (CloseHandle(handle) == 0) {
        written = false;
    }
#else
    auto flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const auto descriptor = open(path.c_str(), flags, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        std::cerr << "foundationc: cannot create temporary file " << path.string() << '\n';
        return false;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < contents.size()) {
        const auto count = write(descriptor, contents.data() + offset, contents.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            written = false;
            break;
        }
        if (count == 0) {
            written = false;
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    if (written && fsync(descriptor) != 0) {
        written = false;
    }
    if (close(descriptor) != 0) {
        written = false;
    }
#endif
    if (written) {
        return true;
    }
    std::cerr << "foundationc: failed while writing temporary file " << path.string() << '\n';
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    return false;
}

bool replaceFile(const std::filesystem::path &path, std::string_view contents) {
    if (!replaceableFile(path)) {
        return false;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        std::cerr << "foundationc: cannot inspect " << path.string() << ": " << error.message()
                  << '\n';
        return false;
    }

    static std::atomic<unsigned long> sequence{};
    const auto suffix = ".foundation-format-" + std::to_string(processId()) + "-" +
                        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    auto temporary = path;
    temporary += suffix;
    if (!writeExclusiveFile(temporary, contents)) {
        return false;
    }
    std::filesystem::permissions(temporary, status.permissions(), error);
    if (error) {
        std::cerr << "foundationc: cannot preserve permissions for " << path.string() << ": "
                  << error.message() << '\n';
        std::filesystem::remove(temporary, error);
        return false;
    }
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    std::cerr << "foundationc: cannot replace " << path.string() << ": win32 error "
              << GetLastError() << '\n';
#else
    std::filesystem::rename(temporary, path, error);
    if (!error) {
        return true;
    }
    std::cerr << "foundationc: cannot replace " << path.string() << ": " << error.message()
              << '\n';
#endif
    std::filesystem::remove(temporary, error);
    return false;
}

std::optional<std::vector<std::filesystem::path>> formatterSources(
    const std::filesystem::path &input) {
    std::error_code error;
    const auto status = std::filesystem::status(input, error);
    if (error) {
        std::cerr << "foundationc: cannot inspect " << input.string() << ": " << error.message()
                  << '\n';
        return std::nullopt;
    }
    if (std::filesystem::is_regular_file(status)) {
        if (input.extension() != ".fn") {
            std::cerr << "foundationc: expected a .fn source file\n";
            return std::nullopt;
        }
        return std::vector<std::filesystem::path>{input};
    }
    if (!std::filesystem::is_directory(status)) {
        std::cerr << "foundationc: expected a Foundation source file or project directory\n";
        return std::nullopt;
    }

    std::vector<std::filesystem::path> sourceRoots{input};
    if (const auto manifest = discoverPackageManifest(input); manifest.has_value()) {
        const auto absoluteInput = std::filesystem::absolute(input, error);
        if (error) {
            std::cerr << "foundationc: cannot identify package project " << input.string()
                      << ": " << error.message() << '\n';
            return std::nullopt;
        }
        const auto sameProject =
            std::filesystem::equivalent(absoluteInput, manifest->parent_path(), error);
        if (error) {
            std::cerr << "foundationc: cannot identify package project " << input.string()
                      << ": " << error.message() << '\n';
            return std::nullopt;
        }
        if (sameProject) {
            const auto sdk = *parsePackageVersion("0.1.0");
            const auto project = loadLockedPackageProject(
                *manifest, sdk, hostTargetPlatform(), defaultPackageCachePath(), false);
            if (!project.value.has_value()) {
                for (const auto &packageError : project.errors) {
                    std::cerr << renderPackageError(packageError);
                }
                return std::nullopt;
            }
            sourceRoots = {project.value->sources.front().sourceRoot};
            if (project.value->manifest.testSource.has_value()) {
                sourceRoots.push_back(project.value->projectRoot /
                                      *project.value->manifest.testSource);
            }
        }
    }

    std::vector<std::filesystem::path> sources;
    for (const auto &sourceRoot : sourceRoots) {
        error.clear();
        std::filesystem::recursive_directory_iterator current(sourceRoot, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && current != end) {
            const auto &entry = *current;
            const auto name = entry.path().filename().string();
            if (entry.is_directory(error) &&
                (name == "build" || (!name.empty() && name[0] == '.'))) {
                current.disable_recursion_pending();
            } else if (!error && entry.is_regular_file(error) &&
                       entry.path().extension() == ".fn") {
                sources.push_back(entry.path());
            }
            current.increment(error);
        }
        if (error) {
            std::cerr << "foundationc: cannot walk " << sourceRoot.string() << ": "
                      << error.message() << '\n';
            return std::nullopt;
        }
    }
    std::sort(sources.begin(), sources.end(), [](const auto &left, const auto &right) {
        return left.generic_string() < right.generic_string();
    });
    if (sources.empty()) {
        std::cerr << "foundationc: no .fn source files found under " << input.string()
                  << '\n';
        return std::nullopt;
    }
    return sources;
}

std::optional<TempDirectory> createTempDirectory() {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) {
        std::cerr << "foundationc: cannot find temporary directory: " << error.message() << '\n';
        return std::nullopt;
    }

    static std::atomic<unsigned long> sequence{};
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        const auto suffix = std::to_string(processId()) + "-" +
                            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        auto candidate = root / ("foundationc-" + suffix);
        if (std::filesystem::create_directory(candidate, error)) {
            return TempDirectory(std::move(candidate));
        }
        if (error && error != std::errc::file_exists) {
            std::cerr << "foundationc: cannot create temporary directory: " << error.message()
                      << '\n';
            return std::nullopt;
        }
        error.clear();
    }

    std::cerr << "foundationc: cannot allocate a unique temporary directory\n";
    return std::nullopt;
}

std::string_view generatedProgramExtension(BackendKind backend) {
    if (backend == BackendKind::C) {
        return ".c";
    }
#ifdef _WIN32
    return ".obj";
#else
    return ".o";
#endif
}

std::filesystem::path generatedProgramPath(const std::filesystem::path &directory,
                                           BackendKind backend) {
    return directory / ("program" + std::string(generatedProgramExtension(backend)));
}

int report(const std::filesystem::path &path, const Compilation &compilation) {
    if (!compilation.diagnostics.hasErrors()) {
        return 0;
    }
    if (compilation.sources.empty()) {
        std::cerr << renderDiagnostics(path.string(), {}, compilation.diagnostics);
    } else {
        std::cerr << renderDiagnostics(compilation.sources, compilation.diagnostics);
    }
    return 1;
}

std::vector<std::string> compilerArguments(const std::filesystem::path &generated,
                                           const std::filesystem::path &output,
                                           const std::filesystem::path &nativeInclude,
                                           const std::vector<std::filesystem::path> &nativeInputs,
                                           const std::vector<std::string> &nativeLinks,
                                           bool verifyAllocations = false) {
    const auto runtimeInclude =
        sdkAsset("runtime/include", std::filesystem::path{FOUNDATION_RUNTIME_INCLUDE});
    const std::vector<std::filesystem::path> runtimeSources{
        sdkAsset("runtime/src/runtime.c", std::filesystem::path{FOUNDATION_RUNTIME_SOURCE}),
        sdkAsset("runtime/src/fs_host.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_FS_HOST_SOURCE}),
        sdkAsset("runtime/src/process.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PROCESS_SOURCE}),
        sdkAsset("runtime/src/pty_posix.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PTY_POSIX_SOURCE}),
        sdkAsset("runtime/src/pty_windows.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PTY_WINDOWS_SOURCE}),
        sdkAsset("runtime/src/terminal.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_TERMINAL_SOURCE}),
        sdkAsset("runtime/src/crypto.c", std::filesystem::path{FOUNDATION_RUNTIME_CRYPTO_SOURCE}),
        sdkAsset("runtime/src/parse.c", std::filesystem::path{FOUNDATION_RUNTIME_PARSE_SOURCE}),
        sdkAsset("runtime/src/task.c", std::filesystem::path{FOUNDATION_RUNTIME_TASK_SOURCE}),
        sdkAsset("runtime/src/cancellation.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_CANCELLATION_SOURCE}),
        sdkAsset("runtime/src/channel.c", std::filesystem::path{FOUNDATION_RUNTIME_CHANNEL_SOURCE}),
        sdkAsset("runtime/src/blocking.c", std::filesystem::path{FOUNDATION_RUNTIME_BLOCKING_SOURCE}),
        sdkAsset("runtime/src/pool.c", std::filesystem::path{FOUNDATION_RUNTIME_POOL_SOURCE}),
        sdkAsset("runtime/src/reactor.c", std::filesystem::path{FOUNDATION_RUNTIME_REACTOR_SOURCE}),
        sdkAsset("runtime/src/watch.c", std::filesystem::path{FOUNDATION_RUNTIME_WATCH_SOURCE}),
        sdkAsset("runtime/src/resiliency.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_RESILIENCY_SOURCE}),
        sdkAsset("runtime/src/net.c", std::filesystem::path{FOUNDATION_RUNTIME_NET_SOURCE}),
        sdkAsset("runtime/src/plugin.c", std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SOURCE}),
        sdkAsset("runtime/src/plugin_sandbox.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SANDBOX_SOURCE}),
    };
    std::vector<std::string> arguments{FOUNDATION_C_COMPILER};
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    if (compilerId == "MSVC") {
        auto objectDirectory = generated.parent_path().string();
        objectDirectory.push_back(std::filesystem::path::preferred_separator);
        arguments.insert(arguments.end(), {"/nologo", "/std:c11", "/W4", "/WX",
                                           "/Fo:" + objectDirectory,
                                           "/I" + runtimeInclude.string(),
                                           "/I" + nativeInclude.string(),
                                           "/Fe:" + output.string()});
        if (verifyAllocations) {
            arguments.push_back("/DFOUNDATION_VERIFY_ALLOCATIONS=1");
        }
        arguments.push_back(generated.string());
        for (const auto &source : runtimeSources) {
            arguments.push_back(source.string());
        }
        for (const auto &input : nativeInputs) {
            arguments.push_back(input.string());
        }
        for (const auto &link : nativeLinks) {
            arguments.push_back(link + ".lib");
        }
        arguments.push_back("bcrypt.lib");
        arguments.push_back("ws2_32.lib");
        arguments.insert(arguments.end(), {"/link", "/Brepro", "/STACK:8388608"});
        return arguments;
    }

    arguments.insert(arguments.end(), {"-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                                       generated.string()});
#if defined(FOUNDATION_GENERATED_ADDRESS_UNDEFINED_SANITIZER)
    arguments.push_back("-fsanitize=address,undefined");
#elif defined(FOUNDATION_GENERATED_THREAD_SANITIZER)
    arguments.push_back("-fsanitize=thread");
#endif
    for (const auto &source : runtimeSources) {
        arguments.push_back(source.string());
    }
    arguments.insert(arguments.end(), {"-I", runtimeInclude.string(), "-I",
                                       nativeInclude.string()});
    if (verifyAllocations) {
        arguments.push_back("-DFOUNDATION_VERIFY_ALLOCATIONS=1");
    }
#ifndef _WIN32
    arguments.push_back("-pthread");
#if defined(__APPLE__)
    arguments.push_back("-Wl,-no_uuid");
    arguments.push_back("-Wl,-no_adhoc_codesign");
    arguments.push_back("-Wl,-S");
#else
    arguments.push_back("-ldl");
#if defined(__linux__)
    arguments.push_back("-lutil");
#endif
#endif
#endif
    for (const auto &input : nativeInputs) {
        arguments.push_back(input.string());
    }
    for (const auto &link : nativeLinks) {
        arguments.push_back("-l" + link);
    }
#ifdef _WIN32
    arguments.push_back("-lbcrypt");
    arguments.push_back("-lws2_32");
#endif
    arguments.insert(arguments.end(), {"-o", output.string()});
    return arguments;
}

std::filesystem::path runtimeIncludeDirectory() {
    return sdkAsset("runtime/include", std::filesystem::path{FOUNDATION_RUNTIME_INCLUDE});
}

std::vector<std::filesystem::path> runtimeSourceFiles() {
    return {
        sdkAsset("runtime/src/runtime.c", std::filesystem::path{FOUNDATION_RUNTIME_SOURCE}),
        sdkAsset("runtime/src/fs_host.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_FS_HOST_SOURCE}),
        sdkAsset("runtime/src/process.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PROCESS_SOURCE}),
        sdkAsset("runtime/src/pty_posix.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PTY_POSIX_SOURCE}),
        sdkAsset("runtime/src/pty_windows.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PTY_WINDOWS_SOURCE}),
        sdkAsset("runtime/src/terminal.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_TERMINAL_SOURCE}),
        sdkAsset("runtime/src/crypto.c", std::filesystem::path{FOUNDATION_RUNTIME_CRYPTO_SOURCE}),
        sdkAsset("runtime/src/parse.c", std::filesystem::path{FOUNDATION_RUNTIME_PARSE_SOURCE}),
        sdkAsset("runtime/src/task.c", std::filesystem::path{FOUNDATION_RUNTIME_TASK_SOURCE}),
        sdkAsset("runtime/src/cancellation.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_CANCELLATION_SOURCE}),
        sdkAsset("runtime/src/channel.c", std::filesystem::path{FOUNDATION_RUNTIME_CHANNEL_SOURCE}),
        sdkAsset("runtime/src/blocking.c", std::filesystem::path{FOUNDATION_RUNTIME_BLOCKING_SOURCE}),
        sdkAsset("runtime/src/pool.c", std::filesystem::path{FOUNDATION_RUNTIME_POOL_SOURCE}),
        sdkAsset("runtime/src/reactor.c", std::filesystem::path{FOUNDATION_RUNTIME_REACTOR_SOURCE}),
        sdkAsset("runtime/src/watch.c", std::filesystem::path{FOUNDATION_RUNTIME_WATCH_SOURCE}),
        sdkAsset("runtime/src/resiliency.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_RESILIENCY_SOURCE}),
        sdkAsset("runtime/src/net.c", std::filesystem::path{FOUNDATION_RUNTIME_NET_SOURCE}),
        sdkAsset("runtime/src/plugin.c", std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SOURCE}),
        sdkAsset("runtime/src/plugin_sandbox.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SANDBOX_SOURCE}),
    };
}

bool compileLibraryObject(const std::filesystem::path &source,
                          const std::filesystem::path &output,
                          const std::filesystem::path &generatedInclude,
                          bool positionIndependent) {
    const auto runtimeInclude = runtimeIncludeDirectory();
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    std::vector<std::string> arguments{FOUNDATION_C_COMPILER};
    if (compilerId == "MSVC") {
        arguments.insert(arguments.end(), {"/nologo", "/std:c11", "/O2", "/W4", "/WX", "/c",
                                           source.string(), "/I" + runtimeInclude.string(),
                                           "/I" + generatedInclude.string(),
                                           "/Fo:" + output.string()});
    } else {
        arguments.insert(arguments.end(), {"-std=c11", "-O2", "-Wall", "-Wextra",
                                           "-Wpedantic", "-Werror"});
        if (positionIndependent) {
            arguments.push_back("-fPIC");
        }
        arguments.insert(arguments.end(), {"-I", runtimeInclude.string(), "-I",
                                           generatedInclude.string(), "-c", source.string(),
                                           "-o", output.string()});
    }
    return runProcess(arguments, ProcessOutput::StdoutToStderrOnFailure) == 0;
}

std::string sharedLibraryFilename(std::string_view name, unsigned int soVersion) {
#ifdef _WIN32
    static_cast<void>(soVersion);
    return std::string(name) + ".dll";
#elif defined(__APPLE__)
    return "lib" + std::string(name) +
           (soVersion == 0 ? std::string{} : "." + std::to_string(soVersion)) + ".dylib";
#else
    return "lib" + std::string(name) + ".so" +
           (soVersion == 0 ? std::string{} : "." + std::to_string(soVersion));
#endif
}

std::string sharedLibraryLinkFilename(std::string_view name) {
#ifdef _WIN32
    return std::string(name) + ".dll";
#elif defined(__APPLE__)
    return "lib" + std::string(name) + ".dylib";
#else
    return "lib" + std::string(name) + ".so";
#endif
}

std::string staticLibraryFilename(std::string_view name) {
#if defined(_WIN32) && defined(_MSC_VER)
    return std::string(name) + ".lib";
#else
    return "lib" + std::string(name) + ".a";
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__)
std::string renderElfVersionScript(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "FOUNDATION_" << packageInterface.abiMajor << " {\n    global:\n";
    for (const auto &function : packageInterface.exports) {
        output << "        " << function.cSymbol << ";\n";
    }
    output << "        fdn_alloc;\n"
              "        fdn_dealloc;\n"
              "        fdn_string_drop;\n"
              "        fdn_panic;\n"
              "        fdn_panic_cstr;\n"
              "    local:\n"
              "        *;\n"
              "};\n";
    return output.str();
}
#endif

#if defined(__APPLE__)
std::string renderExportList(const PackageInterface &packageInterface, bool leadingUnderscore) {
    std::ostringstream output;
    const auto prefix = leadingUnderscore ? "_" : "";
    for (const auto &function : packageInterface.exports) {
        output << prefix << function.cSymbol << '\n';
    }
    output << prefix << "fdn_alloc\n"
           << prefix << "fdn_dealloc\n"
           << prefix << "fdn_string_drop\n"
           << prefix << "fdn_panic\n"
           << prefix << "fdn_panic_cstr\n";
    return output.str();
}
#endif

#ifdef _WIN32
std::string renderWindowsDefinition(const PackageInterface &packageInterface) {
    std::ostringstream output;
    output << "LIBRARY " << packageInterface.library << "\nEXPORTS\n";
    for (const auto &function : packageInterface.exports) {
        output << "    " << function.cSymbol << '\n';
    }
    output << "    fdn_alloc\n"
              "    fdn_dealloc\n"
              "    fdn_string_drop\n"
              "    fdn_panic\n"
              "    fdn_panic_cstr\n";
    return output.str();
}
#endif

bool archiveLibrary(const std::filesystem::path &output,
                    const std::vector<std::filesystem::path> &objects) {
    std::vector<std::string> arguments;
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    if (compilerId == "MSVC") {
        arguments = {FOUNDATION_ARCHIVER, "/NOLOGO", "/Brepro", "/OUT:" + output.string()};
    } else {
        arguments = {FOUNDATION_ARCHIVER, "rcs", output.string()};
    }
    for (const auto &object : objects) {
        arguments.push_back(object.string());
    }
    return runProcess(arguments, ProcessOutput::StdoutToStderrOnFailure) == 0;
}

bool linkSharedLibrary(const std::filesystem::path &output,
                       const std::vector<std::filesystem::path> &objects,
                       const PackageInterface &packageInterface,
                       const std::filesystem::path &controlFile,
                       const std::filesystem::path &importLibrary) {
    static_cast<void>(packageInterface);
    static_cast<void>(importLibrary);
    std::vector<std::string> arguments{FOUNDATION_C_COMPILER};
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    if (compilerId == "MSVC") {
        arguments.insert(arguments.end(), {"/nologo", "/LD", "/Fe:" + output.string()});
        for (const auto &object : objects) {
            arguments.push_back(object.string());
        }
        for (const auto &link : packageInterface.links) {
            if (!link.target.has_value() || *link.target == packageInterface.target) {
                arguments.push_back(link.name + ".lib");
            }
        }
        arguments.insert(arguments.end(), {"bcrypt.lib", "ws2_32.lib", "/link",
                                           "/Brepro",
                                           "/DEF:" + controlFile.string(),
                                           "/IMPLIB:" + importLibrary.string()});
        return runProcess(arguments, ProcessOutput::StdoutToStderrOnFailure) == 0;
    }

    arguments.push_back("-shared");
    for (const auto &object : objects) {
        arguments.push_back(object.string());
    }
#ifdef _WIN32
    arguments.insert(arguments.end(), {"-Wl,--no-undefined", controlFile.string(),
                                       "-Wl,--out-implib," + importLibrary.string(),
                                       "-lbcrypt", "-lws2_32"});
#elif defined(__APPLE__)
    const auto installName = "@rpath/" + sharedLibraryFilename(packageInterface.library,
                                                                 packageInterface.soVersion);
    arguments.insert(arguments.end(), {"-Wl,-undefined,error", "-Wl,-install_name," + installName,
                                       "-Wl,-exported_symbols_list," + controlFile.string(),
                                       "-Wl,-S", "-pthread"});
#else
    arguments.insert(arguments.end(), {"-Wl,--no-undefined",
                                       "-Wl,-soname," + output.filename().string(),
                                       "-Wl,--version-script," + controlFile.string(),
                                       "-pthread", "-ldl"});
#endif
    for (const auto &link : packageInterface.links) {
        if (!link.target.has_value() || *link.target == packageInterface.target) {
            arguments.push_back("-l" + link.name);
        }
    }
    arguments.insert(arguments.end(), {"-o", output.string()});
    return runProcess(arguments, ProcessOutput::StdoutToStderrOnFailure) == 0;
}

int buildCompilation(const std::filesystem::path &source, const std::filesystem::path &output,
                     const std::filesystem::path &temporarySource,
                     const std::filesystem::path &temporaryHeader,
                     const std::vector<std::filesystem::path> &nativeInputs,
                     const std::vector<std::string> &nativeLinks,
                     BackendKind backend) {
    auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    if (!prepareParent(output) ||
        !writeFile(temporaryHeader, compilation.generatedCHeader)) {
        return 1;
    }
    if (backend == BackendKind::Llvm) {
        if (!compilation.fir.has_value() ||
            !emitLlvmObject(*compilation.fir, temporarySource,
                            source.generic_string(),
                            LlvmCodegenOptions{
                                .targetTriple = defaultLlvmTargetTriple(),
                                .optimize = true,
                                .verifyAllocations = false,
                                .sourcePaths = llvmSourcePaths(compilation.sources),
                                .entry = std::nullopt,
                                .libraryPackage = std::nullopt,
                            },
                            compilation.diagnostics)) {
            return report(source, compilation);
        }
    } else if (!writeFile(temporarySource, compilation.generatedC)) {
        return 1;
    }
    return runProcess(compilerArguments(temporarySource, output, temporaryHeader.parent_path(),
                                        nativeInputs, nativeLinks),
                      ProcessOutput::StdoutToStderrOnFailure);
}

} // namespace

std::vector<std::size_t> rootProjectSourceIds(const std::filesystem::path &path,
                                              const ProjectAnalysis &analysis) {
    return rootProjectSources(path, analysis);
}

namespace {

bool hasAttribute(const FirProgram &program, const std::vector<FirAttributeUse> &attributes,
                  std::string_view name) {
    return std::any_of(attributes.begin(), attributes.end(), [&](const auto &attribute) {
        return attribute.declaration < program.attributeDeclarations.size() &&
               program.attributeDeclarations[attribute.declaration].name == name;
    });
}

bool requiresApplicationHost(const FirProgram &program) {
    if (program.main >= program.functions.size() ||
        program.functions[program.main].name != "main") {
        return false;
    }
    return std::any_of(program.functions.begin(), program.functions.end(),
                       [&](const auto &function) {
                           if (hasAttribute(program, function.attributes,
                                            "foundation.web.Route")) {
                               return true;
                           }
                           if (!function.constructor) {
                               return false;
                           }
                           const auto separator = function.name.rfind('.');
                           if (separator == std::string::npos) {
                               return false;
                           }
                           const auto owner =
                               std::string_view(function.name).substr(0, separator);
                           return std::any_of(program.structs.begin(), program.structs.end(),
                                              [&](const auto &type) {
                                                  return type.service && type.name == owner;
                                              });
                       });
}

bool generatedSourceHasDeclarations(std::string_view source) {
    return source.find("\nmethods ") != std::string_view::npos ||
           source.find("\nstruct FoundationApplication") != std::string_view::npos;
}

bool generatedFoundationSource(std::string_view contents) {
    return contents.find("// foundation:generated package/v1") != std::string_view::npos ||
           contents.find("// foundation:generated application/v1") != std::string_view::npos;
}

std::optional<SourceOverlay> deriveProjectSource(const ProjectAnalysis &analysis,
                                                 Diagnostics &diagnostics) {
    if (analysis.sources.empty() || analysis.sources.front().packageName.empty()) {
        return std::nullopt;
    }
    if (std::any_of(analysis.sources.begin(), analysis.sources.end(), [](const auto &source) {
            return generatedFoundationSource(source.contents);
        })) {
        return std::nullopt;
    }

    auto declarations = analysis.program;
    for (auto &function : declarations.functions) {
        function.hasBody = false;
    }
    Diagnostics declarationDiagnostics;
    const auto semantic = analyze(
        declarations, declarationDiagnostics,
        AnalyzeOptions{.requireMain = false, .retainInvalidModel = true});
    if (!semantic.has_value()) {
        return std::nullopt;
    }

    const auto fir = lower(declarations, *semantic);
    const auto generated = requiresApplicationHost(fir)
                               ? emitApplicationHost(fir, diagnostics)
                               : emitPackageSource(fir, diagnostics,
                                                   analysis.sources.front().packageName);
    if (diagnostics.hasErrors() || !generatedSourceHasDeclarations(generated)) {
        return std::nullopt;
    }

    const auto sourcePath = std::filesystem::path(analysis.sources.front().identity)
                                .parent_path() / ".foundation.generated.fn";
    return SourceOverlay{sourcePath, generated};
}

} // namespace

ProjectAnalysis analyzeProject(const std::filesystem::path &path,
                               const std::vector<SourceOverlay> &overlays,
                               AnalyzeOptions options,
                               ProjectMode mode,
                               TargetPlatform target) {
    ProjectAnalysis analysis;
    auto loaded = loadProject(path, analysis.diagnostics, overlays, mode, target);
    if (!loaded.has_value()) {
        return analysis;
    }
    analysis.sources = std::move(loaded->sources);
    analysis.program = std::move(loaded->program);
    if (analysis.diagnostics.hasErrors()) {
        return analysis;
    }

    const auto generated = deriveProjectSource(analysis, analysis.diagnostics);
    if (analysis.diagnostics.hasErrors()) {
        return analysis;
    }
    if (generated.has_value()) {
        auto completeOverlays = overlays;
        completeOverlays.push_back(*generated);
        loaded = loadProject(path, analysis.diagnostics, completeOverlays, mode, target);
        if (!loaded.has_value()) {
            return analysis;
        }
        analysis.sources = std::move(loaded->sources);
        analysis.program = std::move(loaded->program);
        if (analysis.diagnostics.hasErrors()) {
            return analysis;
        }
    }
    analysis.semantic = analyze(analysis.program, analysis.diagnostics, options);
    return analysis;
}

Compilation compile(const std::filesystem::path &path,
                    const std::vector<SourceOverlay> &overlays,
                    TargetPlatform target) {
    Compilation compilation;
    auto analysis = analyzeProject(path, overlays, {}, ProjectMode::Production, target);
    compilation.sources = std::move(analysis.sources);
    compilation.diagnostics = std::move(analysis.diagnostics);
    if (!analysis.semantic.has_value()) {
        return compilation;
    }

    auto fir = lower(analysis.program, *analysis.semantic);
    compilation.generatedC = emitC(fir, path.generic_string());
    compilation.generatedCHeader = emitCHeader(fir);
    compilation.generatedMetadata = emitMetadata(fir);
    compilation.fir = std::move(fir);
    return compilation;
}

int checkFile(const std::filesystem::path &path, TargetPlatform target) {
    const auto compilation = compile(path, {}, target);
    return report(path, compilation);
}

int checkPackage(const std::filesystem::path &path, TargetPlatform target) {
    auto analysis = analyzeProject(path, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Production, target);
    Compilation compilation;
    compilation.sources = std::move(analysis.sources);
    compilation.diagnostics = std::move(analysis.diagnostics);
    if (analysis.semantic.has_value()) {
        compilation.fir = lower(analysis.program, *analysis.semantic);
    }
    return report(path, compilation);
}

int formatPath(const std::filesystem::path &path, FormatMode mode) {
    const auto sources = formatterSources(path);
    if (!sources.has_value()) {
        return 2;
    }
    if (mode == FormatMode::Stdout && sources->size() != 1) {
        std::cerr << "foundationc: stdout formatting requires one source file\n";
        return 2;
    }
    if (mode == FormatMode::Write) {
        for (const auto &source : *sources) {
            if (!replaceableFile(source)) {
                return 1;
            }
        }
    }

    struct FormattedFile {
        std::filesystem::path path;
        std::string contents;
    };
    std::vector<FormattedFile> changed;
    for (const auto &source : *sources) {
        const auto contents = readSourceFile(source);
        if (!contents.has_value()) {
            return 1;
        }
        auto formatted = formatSource(*contents);
        if (formatted.diagnostics.hasErrors()) {
            std::cerr << renderDiagnostics(source.string(), *contents, formatted.diagnostics);
            return 1;
        }
        if (mode == FormatMode::Stdout) {
            std::cout << formatted.contents;
            if (!std::cout) {
                std::cerr << "foundationc: failed while writing formatted source\n";
                return 1;
            }
        } else if (formatted.contents != *contents) {
            changed.push_back({source, std::move(formatted.contents)});
        }
    }
    if (mode == FormatMode::Check) {
        for (const auto &file : changed) {
            std::cout << file.path.generic_string() << '\n';
        }
        if (!std::cout) {
            std::cerr << "foundationc: failed while writing formatter check output\n";
            return 1;
        }
        return changed.empty() ? 0 : 1;
    }
    if (mode == FormatMode::Write) {
        for (const auto &file : changed) {
            if (!replaceFile(file.path, file.contents)) {
                return 1;
            }
        }
    }
    return 0;
}

int organizeImportsPath(const std::filesystem::path &path, FormatMode mode) {
    const auto sources = formatterSources(path);
    if (!sources.has_value()) {
        return 2;
    }
    if (mode == FormatMode::Stdout && sources->size() != 1) {
        std::cerr << "foundationc: stdout import organization requires one source file\n";
        return 2;
    }
    if (mode == FormatMode::Write) {
        for (const auto &source : *sources) {
            if (!replaceableFile(source)) {
                return 1;
            }
        }
    }

    const auto analysis = analyzeProject(
        path, {}, AnalyzeOptions{.requireMain = false, .retainInvalidModel = true},
        ProjectMode::Test);
    if (analysis.sources.empty()) {
        std::cerr << renderDiagnostics(analysis.sources, analysis.diagnostics);
        return 1;
    }

    struct OrganizedFile {
        std::filesystem::path path;
        std::string contents;
    };
    std::vector<OrganizedFile> changed;
    for (const auto &source : *sources) {
        const auto identity = sourceIdentity(source).generic_string();
        const auto found = std::find_if(
            analysis.sources.begin(), analysis.sources.end(), [&](const DiagnosticSource &input) {
                return input.identity == identity;
            });
        if (found == analysis.sources.end()) {
            std::cerr << "foundationc: source is outside the analyzed project: "
                      << source.generic_string() << '\n';
            return 1;
        }
        const auto sourceId = static_cast<std::size_t>(found - analysis.sources.begin());
        auto organized = organizeImports(analysis, sourceId);
        if (organized.diagnostics.hasErrors()) {
            std::cerr << renderDiagnostics(analysis.sources, organized.diagnostics);
            return 1;
        }
        if (mode == FormatMode::Stdout) {
            std::cout << organized.contents;
            if (!std::cout) {
                std::cerr << "foundationc: failed while writing organized source\n";
                return 1;
            }
        } else if (organized.contents != found->contents) {
            changed.push_back({source, std::move(organized.contents)});
        }
    }
    if (mode == FormatMode::Check) {
        for (const auto &file : changed) {
            std::cout << file.path.generic_string() << '\n';
        }
        if (!std::cout) {
            std::cerr << "foundationc: failed while writing import check output\n";
            return 1;
        }
        return changed.empty() ? 0 : 1;
    }
    if (mode == FormatMode::Write) {
        for (const auto &file : changed) {
            if (!replaceFile(file.path, file.contents)) {
                return 1;
            }
        }
    }
    return 0;
}

int emitCFile(const std::filesystem::path &source, const std::filesystem::path &output,
              TargetPlatform target) {
    const auto compilation = compile(source, {}, target);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedC) ? 0 : 1;
}

int emitCHeaderFile(const std::filesystem::path &source, const std::filesystem::path &output,
                    TargetPlatform target) {
    const auto compilation = compile(source, {}, target);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedCHeader) ? 0 : 1;
}

int emitLlvmIrFile(const std::filesystem::path &source,
                   const std::filesystem::path &output) {
    auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    if (!compilation.fir.has_value()) {
        return 1;
    }
    const auto generated = emitLlvmIr(
        *compilation.fir, source.generic_string(),
        LlvmCodegenOptions{
            .targetTriple = defaultLlvmTargetTriple(),
            .optimize = true,
            .verifyAllocations = false,
            .sourcePaths = llvmSourcePaths(compilation.sources),
            .entry = std::nullopt,
            .libraryPackage = std::nullopt,
        },
        compilation.diagnostics);
    if (!generated.has_value()) {
        return report(source, compilation);
    }
    return writeFile(output, *generated) ? 0 : 1;
}

int emitMetadataFile(const std::filesystem::path &source, const std::filesystem::path &output,
                     TargetPlatform target) {
    const auto compilation = compile(source, {}, target);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedMetadata) ? 0 : 1;
}

int emitPackageInterfaceFile(const std::filesystem::path &source,
                             const std::filesystem::path &output) {
    const auto manifestPath = discoverPackageManifest(source);
    if (!manifestPath.has_value()) {
        std::cerr << "foundationc: emit-pii requires a package project\n";
        return 2;
    }
    const auto manifest = readPackageManifest(*manifestPath);
    if (!manifest.value.has_value()) {
        for (const auto &error : manifest.errors)
            std::cerr << renderPackageError(error);
        return 1;
    }
    if (!manifest.value->nativeLibrary) {
        std::cerr << "foundationc: emit-pii requires native_library c\n";
        return 2;
    }
    const auto lock = readPackageLock(manifestPath->parent_path() / "foundation.lock");
    if (!lock.value.has_value()) {
        for (const auto &error : lock.errors)
            std::cerr << renderPackageError(error);
        return 1;
    }

    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false});
    std::optional<PackageInterface> packageInterface;
    if (analysis.semantic.has_value()) {
        const auto fir = lower(analysis.program, *analysis.semantic);
        packageInterface = buildPackageInterface(fir, *manifest.value, *lock.value,
                                                 analysis.diagnostics);
        if (packageInterface.has_value() && packageInterface->exports.empty()) {
            analysis.diagnostics.error("FDN2122",
                                       "native library exports no C ABI functions",
                                       {0, 0, 1, 1});
            packageInterface.reset();
        }
    }
    Compilation result;
    result.sources = std::move(analysis.sources);
    result.diagnostics = std::move(analysis.diagnostics);
    if (const auto status = report(source, result); status != 0)
        return status;
    return packageInterface.has_value() &&
                   writeFile(output, renderPackageInterfaceJson(std::move(*packageInterface)))
               ? 0
               : 1;
}

int emitStateMachineDiagramFile(const std::filesystem::path &source,
                                const std::filesystem::path &output,
                                const std::optional<std::string> &machine,
                                StateMachineDiagramFormat format) {
    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false});
    if (!analysis.semantic.has_value()) {
        Compilation result;
        result.sources = std::move(analysis.sources);
        result.diagnostics = std::move(analysis.diagnostics);
        return report(source, result);
    }
    const auto fir = lower(analysis.program, *analysis.semantic);
    const auto generated = emitStateMachineDiagram(fir, analysis.diagnostics, machine, format);
    Compilation result;
    result.sources = std::move(analysis.sources);
    result.diagnostics = std::move(analysis.diagnostics);
    if (const auto status = report(source, result); status != 0) {
        return status;
    }
    return writeFile(output, generated) ? 0 : 1;
}

int emitDocumentationFile(const std::filesystem::path &source,
                          const std::filesystem::path &output,
                          TargetPlatform target) {
    if (output.extension() != ".md") {
        std::cerr << "foundationc: documentation output must use the .md extension\n";
        return 2;
    }
    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Production, target);
    Compilation result;
    result.sources = analysis.sources;
    result.diagnostics = analysis.diagnostics;
    if (const auto status = report(source, result); status != 0) {
        return status;
    }
    return writeFile(output, emitDocumentation(analysis, rootProjectSourceIds(source, analysis)))
               ? 0
               : 1;
}

int lintFile(const std::filesystem::path &source,
             std::optional<CodeStandardProfile> profile,
             const std::vector<CodeStandardRuleSetting> &settings) {
    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Test);
    Compilation result;
    result.sources = analysis.sources;
    result.diagnostics = analysis.diagnostics;
    if (const auto status = report(source, result); status != 0) {
        return status;
    }
    auto selected = profile.value_or(projectCodeStandard(source));
    std::vector<CodeStandardRuleSetting> effectiveSettings;
    if (const auto manifestPath = discoverPackageManifest(source); manifestPath.has_value()) {
        const auto manifest = readPackageManifest(*manifestPath);
        if (manifest.value.has_value()) {
            if (!profile.has_value()) {
                selected = manifest.value->codeStandard;
            }
            effectiveSettings = manifest.value->codeStandardRules;
        }
    }
    effectiveSettings.insert(effectiveSettings.end(), settings.begin(), settings.end());
    auto findings = lintProject(analysis, selected, rootProjectSourceIds(source, analysis),
                                effectiveSettings);
    if (findings.empty()) {
        return 0;
    }
    std::cerr << renderDiagnostics(analysis.sources, findings);
    return 1;
}

int emitApplicationPlanFile(const std::filesystem::path &source,
                            const std::filesystem::path &output) {
    auto analysis = analyzeProject(source);
    if (analysis.semantic.has_value()) {
        const auto fir = lower(analysis.program, *analysis.semantic);
        const auto generated = emitApplicationPlan(fir, analysis.diagnostics);
        Compilation result;
        result.sources = std::move(analysis.sources);
        result.diagnostics = std::move(analysis.diagnostics);
        if (const auto status = report(source, result); status != 0) {
            return status;
        }
        return writeFile(output, generated) ? 0 : 1;
    }
    Compilation result;
    result.sources = std::move(analysis.sources);
    result.diagnostics = std::move(analysis.diagnostics);
    return report(source, result);
}

int emitOpenAPIFile(const std::filesystem::path &source,
                    const std::filesystem::path &output,
                    const std::optional<std::string> &title,
                    const std::optional<std::string> &version) {
    if (output.extension() != ".json") {
        std::cerr << "foundationc: OpenAPI output must use the .json extension\n";
        return 2;
    }
    auto analysis = analyzeProject(source);
    if (analysis.semantic.has_value()) {
        auto documentTitle = title.value_or("");
        auto documentVersion = version.value_or("");
        if (const auto manifestPath = discoverPackageManifest(source);
            manifestPath.has_value()) {
            const auto manifest = readPackageManifest(*manifestPath);
            if (manifest.value.has_value()) {
                if (documentTitle.empty()) {
                    documentTitle = manifest.value->name;
                }
                if (documentVersion.empty()) {
                    documentVersion = manifest.value->version.string();
                }
            }
        }
        if (documentTitle.empty()) {
            documentTitle = "Foundation API";
        }
        if (documentVersion.empty()) {
            documentVersion = "0.0.0";
        }
        const auto fir = lower(analysis.program, *analysis.semantic);
        const auto generated = emitOpenAPI(fir, analysis.diagnostics, documentTitle,
                                           documentVersion);
        Compilation result;
        result.sources = std::move(analysis.sources);
        result.diagnostics = std::move(analysis.diagnostics);
        if (const auto status = report(source, result); status != 0) {
            return status;
        }
        return writeFile(output, generated) ? 0 : 1;
    }
    Compilation result;
    result.sources = std::move(analysis.sources);
    result.diagnostics = std::move(analysis.diagnostics);
    return report(source, result);
}

namespace {

int emitApplicationHostSourceFile(const std::filesystem::path &source,
                                  const std::filesystem::path &output) {
    if (output.extension() != ".fn") {
        std::cerr << "foundationc: generated source output must use the .fn extension\n";
        return 2;
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(output, error);
    const auto exists = !error && status.type() != std::filesystem::file_type::not_found;
    if (error && error != std::errc::no_such_file_or_directory) {
        std::cerr << "foundationc: cannot inspect " << output.string() << ": "
                  << error.message() << '\n';
        return 1;
    }
    if (exists) {
        if (!replaceableFile(output)) {
            return 1;
        }
        const auto contents = readSourceFile(output);
        if (!contents.has_value()) {
            return 1;
        }
        if (!generatedFoundationSource(*contents)) {
            std::cerr << "foundationc: refusing to replace non-generated source "
                      << output.string() << '\n';
            return 1;
        }
    }

    ProjectAnalysis analysis;
    auto loaded = loadProject(source, analysis.diagnostics);
    if (!loaded.has_value()) {
        Compilation result;
        result.diagnostics = std::move(analysis.diagnostics);
        return report(source, result);
    }
    analysis.sources = std::move(loaded->sources);
    analysis.program = std::move(loaded->program);
    std::string generatedSourcePath;
    if (exists) {
        const auto identity = sourceIdentity(output).generic_string();
        const auto found = std::find_if(
            analysis.sources.begin(), analysis.sources.end(), [&](const auto &candidate) {
                if (candidate.identity == identity) {
                    return true;
                }
                return std::filesystem::path(candidate.path).filename() == output.filename() &&
                       generatedFoundationSource(candidate.contents);
            });
        if (found != analysis.sources.end()) {
            generatedSourcePath = found->path;
            for (auto &function : analysis.program.functions) {
                if (function.sourcePath == generatedSourcePath) {
                    function.hasBody = false;
                }
            }
        }
    }
    for (auto &function : analysis.program.functions) {
        function.hasBody = false;
    }
    if (!analysis.diagnostics.hasErrors()) {
        Diagnostics declarationDiagnostics;
        analysis.semantic = analyze(
            analysis.program, declarationDiagnostics,
            AnalyzeOptions{.requireMain = false, .retainInvalidModel = true});
    }
    if (analysis.semantic.has_value()) {
        const auto fir = lower(analysis.program, *analysis.semantic);
        const auto generated =
            emitApplicationHost(fir, analysis.diagnostics, generatedSourcePath);
        Compilation result;
        result.sources = std::move(analysis.sources);
        result.diagnostics = std::move(analysis.diagnostics);
        if (const auto reportStatus = report(source, result); reportStatus != 0) {
            return reportStatus;
        }
        return (exists ? replaceFile(output, generated) : writeFile(output, generated)) ? 0 : 1;
    }
    Compilation result;
    result.sources = std::move(analysis.sources);
    result.diagnostics = std::move(analysis.diagnostics);
    return report(source, result);
}

} // namespace

int emitApplicationHostFile(const std::filesystem::path &source,
                            const std::filesystem::path &output) {
    return emitApplicationHostSourceFile(source, output);
}

int buildFile(const std::filesystem::path &source, const std::filesystem::path &output,
              const std::vector<std::filesystem::path> &nativeInputs,
              const std::vector<std::string> &nativeLinks,
              BackendKind backend) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
    const auto generated = generatedProgramPath(temporary->path(), backend);
    return buildCompilation(source, output, generated,
                            temporary->path() / "foundation_abi.h", nativeInputs, nativeLinks,
                            backend);
}

int buildLibrary(const std::filesystem::path &source,
                 const std::filesystem::path &outputDirectory,
                 LibraryKind kind,
                 const std::vector<std::filesystem::path> &nativeInputs,
                 BackendKind backend,
                 bool positionIndependent) {
    const auto manifestPath = discoverPackageManifest(source);
    if (!manifestPath.has_value()) {
        std::cerr << "foundationc: build-library requires a package project\n";
        return 2;
    }
    const auto manifest = readPackageManifest(*manifestPath);
    if (!manifest.value.has_value()) {
        for (const auto &error : manifest.errors) {
            std::cerr << renderPackageError(error);
        }
        return 1;
    }
    if (!manifest.value->nativeLibrary || !manifest.value->nativeName.has_value()) {
        std::cerr << "foundationc: build-library requires native_library c and native_name\n";
        return 2;
    }
    const auto lockPath = manifestPath->parent_path() / "foundation.lock";
    const auto lock = readPackageLock(lockPath);
    if (!lock.value.has_value()) {
        for (const auto &error : lock.errors) {
            std::cerr << renderPackageError(error);
        }
        return 1;
    }
    if (lock.value->target != hostTargetPlatform()) {
        std::cerr << "foundationc: build-library requires a lock for the host target\n";
        return 2;
    }

    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Production, lock.value->target);
    std::optional<FirProgram> fir;
    std::optional<PackageInterface> packageInterface;
    if (analysis.semantic.has_value()) {
        fir = lower(analysis.program, *analysis.semantic);
        packageInterface = buildPackageInterface(*fir, *manifest.value, *lock.value,
                                                 analysis.diagnostics);
        if (packageInterface.has_value() && packageInterface->exports.empty()) {
            analysis.diagnostics.error("FDN2122", "native library exports no C ABI functions",
                                       {0, 0, 1, 1});
            packageInterface.reset();
        }
    }
    Compilation result;
    result.sources = analysis.sources;
    result.diagnostics = analysis.diagnostics;
    if (const auto status = report(source, result); status != 0) {
        return status;
    }
    if (!fir.has_value() || !packageInterface.has_value()) {
        return 1;
    }
    for (const auto &input : nativeInputs) {
        const auto extension = input.extension();
        if (extension != ".c" && extension != ".o" && extension != ".obj") {
            std::cerr << "foundationc: native library inputs must be C sources or objects\n";
            return 2;
        }
    }

    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
    const auto header = temporary->path() / "foundation_abi.h";
    const auto headerContents = emitPackageCHeader(*fir, manifest.value->name,
                                                   packageInterface->library);
    const auto interfaceContents = renderPackageInterfaceJson(*packageInterface);
    if (!writeFile(header, headerContents)) {
        return 1;
    }

#ifdef _WIN32
    constexpr std::string_view objectExtension = ".obj";
#else
    constexpr std::string_view objectExtension = ".o";
#endif
    std::vector<std::filesystem::path> objects;
    const auto compilePositionIndependent =
        kind == LibraryKind::Shared || positionIndependent;
    const auto generatedObject = temporary->path() / ("foundation" + std::string(objectExtension));
    const auto librarySourceIdentity = manifestPath->filename().generic_string();
    if (backend == BackendKind::Llvm) {
        Diagnostics diagnostics;
        if (!emitLlvmObject(
                *fir, generatedObject, librarySourceIdentity,
                LlvmCodegenOptions{
                    .targetTriple = defaultLlvmTargetTriple(),
                    .optimize = true,
                    .verifyAllocations = false,
                    .sourcePaths = llvmReproducibleSourcePaths(analysis.sources),
                    .entry = std::nullopt,
                    .libraryPackage = manifest.value->name,
                },
                diagnostics)) {
            std::cerr << renderDiagnostics(source.string(), {}, diagnostics);
            return 1;
        }
    } else {
        const auto generatedSource = temporary->path() / "foundation.c";
        if (!writeFile(generatedSource,
                       emitPackageC(*fir, manifest.value->name, librarySourceIdentity)) ||
            !compileLibraryObject(generatedSource, generatedObject, temporary->path(),
                                  compilePositionIndependent)) {
            return 1;
        }
    }
    objects.push_back(generatedObject);

    const auto runtimeSources = runtimeSourceFiles();
    for (std::size_t index = 0; index < runtimeSources.size(); ++index) {
        const auto object = temporary->path() /
                            ("runtime-" + std::to_string(index) + std::string(objectExtension));
        if (!compileLibraryObject(runtimeSources[index], object, temporary->path(),
                                  compilePositionIndependent)) {
            return 1;
        }
        objects.push_back(object);
    }
    for (std::size_t index = 0; index < nativeInputs.size(); ++index) {
        const auto &input = nativeInputs[index];
        if (input.extension() == ".c") {
            const auto object = temporary->path() /
                                ("native-" + std::to_string(index) +
                                 std::string(objectExtension));
            if (!compileLibraryObject(input, object, temporary->path(),
                                      compilePositionIndependent)) {
                return 1;
            }
            objects.push_back(object);
            continue;
        }
        objects.push_back(input);
    }

    const auto artifactName = kind == LibraryKind::Shared
                                  ? sharedLibraryFilename(packageInterface->library,
                                                          packageInterface->soVersion)
                                  : staticLibraryFilename(packageInterface->library);
    const auto artifact = temporary->path() / artifactName;
    const auto importLibrary = temporary->path() /
                               staticLibraryFilename(packageInterface->library);
    if (kind == LibraryKind::Static) {
        if (!archiveLibrary(artifact, objects)) {
            return 1;
        }
    } else {
        const auto control = temporary->path() /
#ifdef _WIN32
                             "exports.def";
        const auto controlContents = renderWindowsDefinition(*packageInterface);
#elif defined(__APPLE__)
                             "exports.list";
        const auto controlContents = renderExportList(*packageInterface, true);
#else
                             "exports.map";
        const auto controlContents = renderElfVersionScript(*packageInterface);
#endif
        if (!writeFile(control, controlContents) ||
            !linkSharedLibrary(artifact, objects, *packageInterface, control, importLibrary)) {
            return 1;
        }
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(outputDirectory, error);
    if (!error && std::filesystem::is_symlink(status)) {
        std::cerr << "foundationc: refusing symbolic-link output directory\n";
        return 1;
    }
    error.clear();
    const auto includeDirectory = outputDirectory / "include";
    const auto runtimeHeaderDirectory = includeDirectory / "foundation";
    const auto libraryDirectory = outputDirectory / "lib";
    const auto metadataDirectory = outputDirectory / "share" / "foundation";
    std::filesystem::create_directories(runtimeHeaderDirectory, error);
    if (!error) {
        std::filesystem::create_directories(libraryDirectory, error);
    }
    if (!error) {
        std::filesystem::create_directories(metadataDirectory, error);
    }
    if (error) {
        std::cerr << "foundationc: cannot create library output directories: "
                  << error.message() << '\n';
        return 1;
    }
    const auto runtimeHeader = readSourceFile(runtimeIncludeDirectory() / "foundation" /
                                              "library.h");
    if (!runtimeHeader.has_value() ||
        !writeFile(includeDirectory / (packageInterface->library + ".h"), headerContents) ||
        !writeFile(runtimeHeaderDirectory / "library.h", *runtimeHeader) ||
        !writeFile(metadataDirectory / (packageInterface->library + ".pii.json"),
                   interfaceContents)) {
        return 1;
    }
    const auto publishedArtifact = libraryDirectory / artifactName;
    std::filesystem::copy_file(artifact, publishedArtifact,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        std::cerr << "foundationc: cannot publish " << publishedArtifact.string() << ": "
                  << error.message() << '\n';
        return 1;
    }
#ifndef _WIN32
    if (kind == LibraryKind::Shared) {
        const auto linkName = sharedLibraryLinkFilename(packageInterface->library);
        if (linkName != artifactName) {
            const auto publishedLink = libraryDirectory / linkName;
            error.clear();
            std::filesystem::remove(publishedLink, error);
            if (!error) {
                std::filesystem::create_symlink(publishedArtifact.filename(), publishedLink,
                                                error);
            }
            if (error) {
                std::cerr << "foundationc: cannot publish shared library link: "
                          << error.message() << '\n';
                return 1;
            }
        }
    }
#endif
#ifdef _WIN32
    if (kind == LibraryKind::Shared && std::filesystem::exists(importLibrary)) {
        std::filesystem::copy_file(importLibrary,
                                   libraryDirectory / importLibrary.filename(),
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            std::cerr << "foundationc: cannot publish import library: " << error.message()
                      << '\n';
            return 1;
        }
    }
#endif
    std::cout << "library " << publishedArtifact.generic_string() << '\n'
              << "header "
              << (includeDirectory / (packageInterface->library + ".h")).generic_string()
              << '\n'
              << "interface "
              << (metadataDirectory / (packageInterface->library + ".pii.json")).generic_string()
              << '\n';
    return 0;
}

int exportPackage(const std::filesystem::path &source,
                  const std::filesystem::path &outputDirectory,
                  PackageExportFormat format,
                  const std::vector<std::filesystem::path> &nativeInputs,
                  BackendKind backend) {
    const auto manifestPath = discoverPackageManifest(source);
    if (!manifestPath.has_value()) {
        std::cerr << "foundationc: package export requires a package project\n";
        return 2;
    }
    const auto manifest = readPackageManifest(*manifestPath);
    if (!manifest.value.has_value()) {
        for (const auto &error : manifest.errors) {
            std::cerr << renderPackageError(error);
        }
        return 1;
    }
    if (format != PackageExportFormat::GoSource &&
        (!manifest.value->nativeLibrary || !manifest.value->nativeName.has_value())) {
        std::cerr << "foundationc: package export requires native_library c and native_name\n";
        return 2;
    }
    const auto lock = readPackageLock(manifestPath->parent_path() / "foundation.lock");
    if (!lock.value.has_value()) {
        for (const auto &error : lock.errors) {
            std::cerr << renderPackageError(error);
        }
        return 1;
    }
    if (lock.value->target != hostTargetPlatform()) {
        std::cerr << "foundationc: package export requires a lock for the host target\n";
        return 2;
    }

    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Production, lock.value->target);
    std::optional<FirProgram> fir;
    std::optional<PackageInterface> packageInterface;
    std::optional<PackageExport> generated;
    if (analysis.semantic.has_value()) {
        fir = lower(analysis.program, *analysis.semantic);
        packageInterface = buildPackageInterface(*fir, *manifest.value, *lock.value,
                                                 analysis.diagnostics);
        if (format != PackageExportFormat::GoSource && packageInterface.has_value() &&
            packageInterface->exports.empty()) {
            analysis.diagnostics.error("FDN2122", "native library exports no C ABI functions",
                                       {0, 0, 1, 1});
            packageInterface.reset();
        }
        if (packageInterface.has_value()) {
            generated = generatePackageExport(*fir, *packageInterface, format,
                                              analysis.diagnostics);
        }
    }
    Compilation result;
    result.sources = analysis.sources;
    result.diagnostics = analysis.diagnostics;
    if (const auto status = report(source, result); status != 0) {
        return status;
    }
    if (!fir.has_value() || !packageInterface.has_value() || !generated.has_value()) {
        return 1;
    }

    std::error_code error;
    const auto outputStatus = std::filesystem::symlink_status(outputDirectory, error);
    if (!error && std::filesystem::is_symlink(outputStatus)) {
        std::cerr << "foundationc: refusing symbolic-link package output directory\n";
        return 1;
    }
    if (error != std::errc::no_such_file_or_directory && error) {
        std::cerr << "foundationc: cannot inspect package output directory: "
                  << error.message() << '\n';
        return 1;
    }

    if (generated->artifact != PackageExportArtifact::None) {
        const auto libraryKind = generated->artifact == PackageExportArtifact::Static
                                     ? LibraryKind::Static
                                     : LibraryKind::Shared;
        const auto status = buildLibrary(source, outputDirectory / "native", libraryKind,
                                         nativeInputs, backend);
        if (status != 0) {
            return status;
        }
    }

    for (const auto &file : generated->files) {
        const auto destination = outputDirectory / file.path;
        error.clear();
        const auto destinationStatus = std::filesystem::symlink_status(destination, error);
        if (!error && std::filesystem::is_symlink(destinationStatus)) {
            std::cerr << "foundationc: refusing symbolic-link package output file\n";
            return 1;
        }
        if (error != std::errc::no_such_file_or_directory && error) {
            std::cerr << "foundationc: cannot inspect package output file: " << error.message()
                      << '\n';
            return 1;
        }
        error.clear();
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            std::cerr << "foundationc: cannot create package output directories: "
                      << error.message() << '\n';
            return 1;
        }
        if (!writeFile(destination, file.contents)) {
            return 1;
        }
        std::cout << "package " << destination.generic_string() << '\n';
    }
    return 0;
}

int runFile(const std::filesystem::path &source,
            const std::vector<std::filesystem::path> &nativeInputs,
            const std::vector<std::string> &nativeLinks,
            const std::vector<std::string> &arguments,
            BackendKind backend) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
#ifdef _WIN32
    const auto executable = temporary->path() / "program.exe";
#else
    const auto executable = temporary->path() / "program";
#endif
    const auto generated = generatedProgramPath(temporary->path(), backend);
    const auto status = buildCompilation(source, executable, generated,
                                         temporary->path() / "foundation_abi.h", nativeInputs, nativeLinks,
                                         backend);
    if (status != 0) {
        return status;
    }
    std::vector<std::string> processArguments{executable.string()};
    processArguments.insert(processArguments.end(), arguments.begin(), arguments.end());
    return runProcess(processArguments);
}

int runTests(const std::filesystem::path &source,
             const std::vector<std::filesystem::path> &nativeInputs,
             const std::vector<std::string> &nativeLinks,
             BackendKind backend) {
    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false},
                                   ProjectMode::Test);
    if (analysis.diagnostics.hasErrors()) {
        if (analysis.sources.empty()) {
            std::cerr << renderDiagnostics(source.string(), {}, analysis.diagnostics);
        } else {
            std::cerr << renderDiagnostics(analysis.sources, analysis.diagnostics);
        }
        return 1;
    }
    if (!analysis.semantic.has_value()) {
        return 1;
    }

    const auto fir = lower(analysis.program, *analysis.semantic);
    std::vector<FirFunctionId> tests;
    for (FirFunctionId function = 0; function < fir.functions.size(); ++function) {
        if (fir.functions[function].testName.has_value()) {
            tests.push_back(function);
        }
    }
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
    const auto header = temporary->path() / "foundation_abi.h";
    if (!writeFile(header, emitCHeader(fir))) {
        return 1;
    }

    std::size_t passed{};
    for (std::size_t index = 0; index < tests.size(); ++index) {
        const auto function = tests[index];
        const auto generated = temporary->path() /
                               ("test-" + std::to_string(index) +
                                std::string(generatedProgramExtension(backend)));
#ifdef _WIN32
        const auto executable =
            temporary->path() / ("test-" + std::to_string(index) + ".exe");
#else
        const auto executable = temporary->path() / ("test-" + std::to_string(index));
#endif
        if (backend == BackendKind::Llvm) {
            Diagnostics diagnostics;
            if (!emitLlvmObject(
                    fir, generated, source.generic_string(),
                    LlvmCodegenOptions{.targetTriple = defaultLlvmTargetTriple(),
                                       .optimize = true,
                                       .verifyAllocations = true,
                                       .sourcePaths = llvmSourcePaths(analysis.sources),
                                       .entry = function,
                                       .libraryPackage = std::nullopt},
                    diagnostics)) {
                std::cerr << renderDiagnostics(source.string(), {}, diagnostics);
                return 1;
            }
        } else if (!writeFile(generated,
                              emitTestC(fir, function, source.generic_string()))) {
            return 1;
        }
        const auto compiled = runProcess(
            compilerArguments(generated, executable, temporary->path(), nativeInputs,
                              nativeLinks, true),
            ProcessOutput::StdoutToStderrOnFailure);
        if (compiled != 0) {
            return compiled;
        }
        std::cout << "test " << *fir.functions[function].testName << '\n' << std::flush;
        const auto status = runProcess({executable.string()});
        if (status == 0) {
            ++passed;
            std::cout << "ok " << *fir.functions[function].testName << '\n';
        } else {
            std::cout << "FAILED " << *fir.functions[function].testName << '\n';
        }
    }
    std::cout << passed << " passed; " << tests.size() - passed << " failed\n";
    return passed == tests.size() ? 0 : 1;
}

} // namespace foundation
