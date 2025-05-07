#include "foundation/driver.hpp"

#include "foundation/codegen.hpp"
#include "foundation/lexer.hpp"
#include "foundation/lower.hpp"
#include "foundation/parser.hpp"
#include "foundation/process.hpp"
#include "foundation/sema.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
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

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
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
    std::cerr << renderDiagnostics(path.string(), compilation.source, compilation.diagnostics);
    return 1;
}

std::vector<std::string> compilerArguments(const std::filesystem::path &generated,
                                           const std::filesystem::path &output) {
    std::vector<std::string> arguments{FOUNDATION_C_COMPILER};
    const std::string compilerId = FOUNDATION_C_COMPILER_ID;
    if (compilerId == "MSVC") {
        arguments.insert(arguments.end(), {"/nologo", "/std:c11", "/W4", "/WX",
                                           generated.string(), FOUNDATION_RUNTIME_SOURCE,
                                           "/I" FOUNDATION_RUNTIME_INCLUDE,
                                           "/Fe:" + output.string()});
        return arguments;
    }

    arguments.insert(arguments.end(), {"-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                                       generated.string(), FOUNDATION_RUNTIME_SOURCE, "-I",
                                       FOUNDATION_RUNTIME_INCLUDE, "-o", output.string()});
    return arguments;
}

int buildCompilation(const std::filesystem::path &source, const std::filesystem::path &output,
                     const std::filesystem::path &temporarySource) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    if (!prepareParent(output) || !writeFile(temporarySource, compilation.generatedC)) {
        return 1;
    }
    return runProcess(compilerArguments(temporarySource, output), ProcessOutput::StdoutToStderr);
}

} // namespace

Compilation compile(const std::filesystem::path &path) {
    Compilation compilation;
    const auto source = readFile(path);
    if (!source.has_value()) {
        compilation.diagnostics.error("FDN3001", "cannot read source file", {0, 0, 1, 1});
        return compilation;
    }
    compilation.source = *source;

    Lexer lexer(compilation.source, compilation.diagnostics);
    auto tokens = lexer.scan();
    Parser parser(std::move(tokens), compilation.diagnostics);
    auto program = parser.parse();
    if (compilation.diagnostics.hasErrors()) {
        return compilation;
    }
    const auto semantic = analyze(program, compilation.diagnostics);
    if (!semantic.has_value()) {
        return compilation;
    }

    compilation.generatedC = emitC(lower(program, *semantic), path.generic_string());
    return compilation;
}

int checkFile(const std::filesystem::path &path) {
    const auto compilation = compile(path);
    return report(path, compilation);
}

int emitCFile(const std::filesystem::path &source, const std::filesystem::path &output) {
    const auto compilation = compile(source);
    if (const auto status = report(source, compilation); status != 0) {
        return status;
    }
    return writeFile(output, compilation.generatedC) ? 0 : 1;
}

int buildFile(const std::filesystem::path &source, const std::filesystem::path &output) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
    return buildCompilation(source, output, temporary->path() / "program.c");
}

int runFile(const std::filesystem::path &source) {
    auto temporary = createTempDirectory();
    if (!temporary.has_value()) {
        return 1;
    }
#ifdef _WIN32
    const auto executable = temporary->path() / "program.exe";
#else
    const auto executable = temporary->path() / "program";
#endif
    const auto status = buildCompilation(source, executable, temporary->path() / "program.c");
    if (status != 0) {
        return status;
    }
    return runProcess({executable.string()});
}

} // namespace foundation
