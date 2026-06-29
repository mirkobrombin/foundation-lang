#include "foundation/driver.hpp"

#include "foundation/application.hpp"
#include "foundation/codegen.hpp"
#include "foundation/formatter.hpp"
#include "foundation/lower.hpp"
#include "foundation/metadata.hpp"
#include "foundation/package.hpp"
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

std::filesystem::path sourceIdentity(const std::filesystem::path &path) {
    std::error_code error;
    auto result = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    const auto canonical = std::filesystem::weakly_canonical(result, error);
    return error ? result.lexically_normal() : canonical;
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
        if (input.extension() != ".fdn") {
            std::cerr << "foundationc: expected a .fdn source file\n";
            return std::nullopt;
        }
        return std::vector<std::filesystem::path>{input};
    }
    if (!std::filesystem::is_directory(status)) {
        std::cerr << "foundationc: expected a Foundation source file or project directory\n";
        return std::nullopt;
    }

    auto sourceRoot = input;
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
                *manifest, sdk, hostTargetPlatform(), defaultPackageCachePath());
            if (!project.value.has_value()) {
                for (const auto &packageError : project.errors) {
                    std::cerr << renderPackageError(packageError);
                }
                return std::nullopt;
            }
            sourceRoot = project.value->sources.front().sourceRoot;
        }
    }

    std::vector<std::filesystem::path> sources;
    std::filesystem::recursive_directory_iterator current(sourceRoot, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && current != end) {
        const auto &entry = *current;
        const auto name = entry.path().filename().string();
        if (entry.is_directory(error) && (name == "build" || (!name.empty() && name[0] == '.'))) {
            current.disable_recursion_pending();
        } else if (!error && entry.is_regular_file(error) && entry.path().extension() == ".fdn") {
            sources.push_back(entry.path());
        }
        current.increment(error);
    }
    if (error) {
        std::cerr << "foundationc: cannot walk " << sourceRoot.string() << ": "
                  << error.message()
                  << '\n';
        return std::nullopt;
    }
    std::sort(sources.begin(), sources.end(), [](const auto &left, const auto &right) {
        return left.generic_string() < right.generic_string();
    });
    if (sources.empty()) {
        std::cerr << "foundationc: no .fdn source files found under " << sourceRoot.string()
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
                                           bool verifyAllocations = false) {
    const auto runtimeInclude =
        sdkAsset("runtime/include", std::filesystem::path{FOUNDATION_RUNTIME_INCLUDE});
    const std::vector<std::filesystem::path> runtimeSources{
        sdkAsset("runtime/src/runtime.c", std::filesystem::path{FOUNDATION_RUNTIME_SOURCE}),
        sdkAsset("runtime/src/parse.c", std::filesystem::path{FOUNDATION_RUNTIME_PARSE_SOURCE}),
        sdkAsset("runtime/src/task.c", std::filesystem::path{FOUNDATION_RUNTIME_TASK_SOURCE}),
        sdkAsset("runtime/src/cancellation.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_CANCELLATION_SOURCE}),
        sdkAsset("runtime/src/channel.c", std::filesystem::path{FOUNDATION_RUNTIME_CHANNEL_SOURCE}),
        sdkAsset("runtime/src/blocking.c", std::filesystem::path{FOUNDATION_RUNTIME_BLOCKING_SOURCE}),
        sdkAsset("runtime/src/pool.c", std::filesystem::path{FOUNDATION_RUNTIME_POOL_SOURCE}),
        sdkAsset("runtime/src/reactor.c", std::filesystem::path{FOUNDATION_RUNTIME_REACTOR_SOURCE}),
        sdkAsset("runtime/src/net.c", std::filesystem::path{FOUNDATION_RUNTIME_NET_SOURCE}),
        sdkAsset("runtime/src/plugin.c", std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SOURCE}),
        sdkAsset("runtime/src/plugin_sandbox.c",
                 std::filesystem::path{FOUNDATION_RUNTIME_PLUGIN_SANDBOX_SOURCE}),
    };
    std::vector<std::string> arguments{FOUNDATION_C_COMPILER};
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    if (compilerId == "MSVC") {
        arguments.insert(arguments.end(),
                         {"/nologo", "/std:c11", "/W4", "/WX", generated.string()});
        for (const auto &source : runtimeSources) {
            arguments.push_back(source.string());
        }
        arguments.insert(arguments.end(), {"/I" + runtimeInclude.string(),
                                           "/I" + nativeInclude.string()});
        if (verifyAllocations) {
            arguments.push_back("/DFOUNDATION_VERIFY_ALLOCATIONS=1");
        }
        for (const auto &input : nativeInputs) {
            arguments.push_back(input.string());
        }
        arguments.push_back("bcrypt.lib");
        arguments.push_back("ws2_32.lib");
        arguments.push_back("/Fe:" + output.string());
        return arguments;
    }

    arguments.insert(arguments.end(), {"-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                                       generated.string()});
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
#if !defined(__APPLE__)
    arguments.push_back("-ldl");
#endif
#endif
    for (const auto &input : nativeInputs) {
        arguments.push_back(input.string());
    }
#ifdef _WIN32
    arguments.push_back("-lbcrypt");
    arguments.push_back("-lws2_32");
#endif
    arguments.insert(arguments.end(), {"-o", output.string()});
    return arguments;
}

int buildCompilation(const std::filesystem::path &source, const std::filesystem::path &output,
                     const std::filesystem::path &temporarySource,
                     const std::filesystem::path &temporaryHeader,
                     const std::vector<std::filesystem::path> &nativeInputs) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    if (!prepareParent(output) || !writeFile(temporarySource, compilation.generatedC) ||
        !writeFile(temporaryHeader, compilation.generatedCHeader)) {
        return 1;
    }
    return runProcess(compilerArguments(temporarySource, output, temporaryHeader.parent_path(),
                                        nativeInputs),
                      ProcessOutput::StdoutToStderr);
}

} // namespace

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
                           return hasAttribute(program, function.attributes,
                                               "foundation.web.Route") ||
                                  hasAttribute(program, function.attributes,
                                               "foundation.di.Inject");
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
                                .parent_path() / ".foundation.generated.fdn";
    return SourceOverlay{sourcePath, generated};
}

} // namespace

ProjectAnalysis analyzeProject(const std::filesystem::path &path,
                               const std::vector<SourceOverlay> &overlays,
                               AnalyzeOptions options) {
    ProjectAnalysis analysis;
    auto loaded = loadProject(path, analysis.diagnostics, overlays);
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
        loaded = loadProject(path, analysis.diagnostics, completeOverlays);
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
                    const std::vector<SourceOverlay> &overlays) {
    Compilation compilation;
    auto analysis = analyzeProject(path, overlays);
    compilation.sources = std::move(analysis.sources);
    compilation.diagnostics = std::move(analysis.diagnostics);
    if (!analysis.semantic.has_value()) {
        return compilation;
    }

    const auto fir = lower(analysis.program, *analysis.semantic);
    compilation.generatedC = emitC(fir, path.generic_string());
    compilation.generatedCHeader = emitCHeader(fir);
    compilation.generatedMetadata = emitMetadata(fir);
    return compilation;
}

int checkFile(const std::filesystem::path &path) {
    const auto compilation = compile(path);
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

int emitCFile(const std::filesystem::path &source, const std::filesystem::path &output) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedC) ? 0 : 1;
}

int emitCHeaderFile(const std::filesystem::path &source, const std::filesystem::path &output) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedCHeader) ? 0 : 1;
}

int emitMetadataFile(const std::filesystem::path &source, const std::filesystem::path &output) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedMetadata) ? 0 : 1;
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

namespace {

int emitApplicationHostSourceFile(const std::filesystem::path &source,
                                  const std::filesystem::path &output) {
    if (output.extension() != ".fdn") {
        std::cerr << "foundationc: generated source output must use the .fdn extension\n";
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
              const std::vector<std::filesystem::path> &nativeInputs) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
    return buildCompilation(source, output, temporary->path() / "program.c",
                            temporary->path() / "foundation_abi.h", nativeInputs);
}

int runFile(const std::filesystem::path &source,
            const std::vector<std::filesystem::path> &nativeInputs,
            const std::vector<std::string> &arguments) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
#ifdef _WIN32
    const auto executable = temporary->path() / "program.exe";
#else
    const auto executable = temporary->path() / "program";
#endif
    const auto status = buildCompilation(source, executable, temporary->path() / "program.c",
                                         temporary->path() / "foundation_abi.h", nativeInputs);
    if (status != 0) {
        return status;
    }
    std::vector<std::string> processArguments{executable.string()};
    processArguments.insert(processArguments.end(), arguments.begin(), arguments.end());
    return runProcess(processArguments);
}

int runTests(const std::filesystem::path &source,
             const std::vector<std::filesystem::path> &nativeInputs) {
    auto analysis = analyzeProject(source, {}, AnalyzeOptions{.requireMain = false});
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
        const auto generated = temporary->path() / ("test-" + std::to_string(index) + ".c");
#ifdef _WIN32
        const auto executable =
            temporary->path() / ("test-" + std::to_string(index) + ".exe");
#else
        const auto executable = temporary->path() / ("test-" + std::to_string(index));
#endif
        if (!writeFile(generated, emitTestC(fir, function, source.generic_string()))) {
            return 1;
        }
        const auto compiled = runProcess(
            compilerArguments(generated, executable, temporary->path(), nativeInputs, true),
            ProcessOutput::StdoutToStderr);
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
