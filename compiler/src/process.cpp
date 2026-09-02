#include "foundation/process.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace foundation {

namespace {

std::string errorMessage(int code) {
    return std::error_code(code, std::generic_category()).message();
}

void replayOutput(std::FILE *stream) {
    std::rewind(stream);
    char buffer[4096];
    while (const auto length = std::fread(buffer, 1, sizeof(buffer), stream)) {
        static_cast<void>(std::fwrite(buffer, 1, length, stderr));
    }
    std::fflush(stderr);
}

#ifdef _WIN32
std::FILE *openOutputCapture() {
    wchar_t directory[MAX_PATH + 1];
    const auto directoryLength = GetTempPathW(MAX_PATH + 1, directory);
    if (directoryLength == 0 || directoryLength > MAX_PATH) {
        errno = EIO;
        return nullptr;
    }

    wchar_t path[MAX_PATH + 1];
    if (GetTempFileNameW(directory, L"fdn", 0, path) == 0) {
        errno = EIO;
        return nullptr;
    }

    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    const auto handle = CreateFileW(
        path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &attributes,
        TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        DeleteFileW(path);
        errno = EIO;
        return nullptr;
    }

    const auto descriptor =
        _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_RDWR | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        return nullptr;
    }
    auto *stream = _fdopen(descriptor, "w+b");
    if (stream == nullptr) {
        _close(descriptor);
    }
    return stream;
}

std::string quoteWindowsArgument(const std::string &argument) {
    if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) {
        return argument;
    }

    std::string quoted{'"'};
    std::size_t backslashes{};
    for (const auto character : argument) {
        if (character == '\\') {
            ++backslashes;
            continue;
        }
        if (character == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back(character);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}
#endif

} // namespace

int runProcess(const std::vector<std::string> &arguments, ProcessOutput output) {
    if (arguments.empty()) {
        std::cerr << "foundationc: process requires an executable\n";
        return 1;
    }

#ifdef _WIN32
    auto programName = arguments.front();
    const auto separator = programName.find_last_of("/\\");
    if (separator != std::string::npos) {
        programName.erase(0, separator + 1);
    }

    std::vector<std::string> quotedArguments;
    quotedArguments.reserve(arguments.size());
    quotedArguments.push_back(quoteWindowsArgument(programName));
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        quotedArguments.push_back(quoteWindowsArgument(arguments[index]));
    }

    std::vector<const char *> argv;
    argv.reserve(quotedArguments.size() + 1);
    for (const auto &argument : quotedArguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    std::FILE *capturedStdout = nullptr;
    auto savedStdout = -1;
    if (output != ProcessOutput::Inherit) {
        std::cout.flush();
        std::fflush(stdout);
        savedStdout = _dup(_fileno(stdout));
        if (output == ProcessOutput::StdoutToStderrOnFailure) {
            capturedStdout = openOutputCapture();
        }
        const auto destination = capturedStdout == nullptr ? stderr : capturedStdout;
        if (savedStdout == -1 ||
            (output == ProcessOutput::StdoutToStderrOnFailure &&
             capturedStdout == nullptr) ||
            _dup2(_fileno(destination), _fileno(stdout)) != 0) {
            const auto error = errno;
            if (savedStdout != -1) {
                _close(savedStdout);
            }
            if (capturedStdout != nullptr) {
                std::fclose(capturedStdout);
            }
            std::cerr << "foundationc: cannot redirect process output: " << errorMessage(error)
                      << '\n';
            return 1;
        }
    }

    const auto status = _spawnvp(_P_WAIT, arguments.front().c_str(), argv.data());
    const auto spawnError = errno;
    if (savedStdout != -1) {
        std::fflush(stdout);
        if (_dup2(savedStdout, _fileno(stdout)) != 0) {
            const auto error = errno;
            _close(savedStdout);
            if (capturedStdout != nullptr) {
                std::fclose(capturedStdout);
            }
            std::cerr << "foundationc: cannot restore process output: " << errorMessage(error)
                      << '\n';
            return 1;
        }
        _close(savedStdout);
    }
    if (capturedStdout != nullptr) {
        if (status != 0) {
            replayOutput(capturedStdout);
        }
        std::fclose(capturedStdout);
    }
    if (status == -1) {
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << errorMessage(spawnError) << '\n';
        return 1;
    }
    if (status > std::numeric_limits<int>::max()) {
        std::cerr << "foundationc: process returned an unsupported exit status\n";
        return 1;
    }
    return static_cast<int>(status);
#else
    auto ownedArguments = arguments;
    std::vector<char *> argv;
    argv.reserve(ownedArguments.size() + 1);
    for (auto &argument : ownedArguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    std::FILE *capturedStdout = nullptr;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actionsPointer = nullptr;
    if (output != ProcessOutput::Inherit) {
        const auto initError = posix_spawn_file_actions_init(&actions);
        if (initError != 0) {
            std::cerr << "foundationc: cannot initialize process output: "
                      << errorMessage(initError) << '\n';
            return 1;
        }
        if (output == ProcessOutput::StdoutToStderrOnFailure) {
            capturedStdout = std::tmpfile();
            if (capturedStdout == nullptr) {
                const auto error = errno;
                posix_spawn_file_actions_destroy(&actions);
                std::cerr << "foundationc: cannot capture process output: "
                          << errorMessage(error) << '\n';
                return 1;
            }
        }
        const auto destination = capturedStdout == nullptr ? STDERR_FILENO
                                                            : fileno(capturedStdout);
        const auto redirectError =
            posix_spawn_file_actions_adddup2(&actions, destination, STDOUT_FILENO);
        if (redirectError != 0) {
            posix_spawn_file_actions_destroy(&actions);
            if (capturedStdout != nullptr) {
                std::fclose(capturedStdout);
            }
            std::cerr << "foundationc: cannot redirect process output: "
                      << errorMessage(redirectError) << '\n';
            return 1;
        }
        actionsPointer = &actions;
    }

    pid_t child{};
    const auto spawnError =
        posix_spawnp(&child, argv.front(), actionsPointer, nullptr, argv.data(), environ);
    if (actionsPointer != nullptr) {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (spawnError != 0) {
        if (capturedStdout != nullptr) {
            std::fclose(capturedStdout);
        }
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << errorMessage(spawnError) << '\n';
        return 1;
    }

    int status{};
    if (waitpid(child, &status, 0) == -1) {
        const auto error = errno;
        if (capturedStdout != nullptr) {
            std::fclose(capturedStdout);
        }
        std::cerr << "foundationc: cannot wait for process: " << errorMessage(error) << '\n';
        return 1;
    }
    int result = 1;
    if (WIFEXITED(status)) {
        result = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result = 128 + WTERMSIG(status);
    }
    if (capturedStdout != nullptr) {
        if (result != 0) {
            replayOutput(capturedStdout);
        }
        std::fclose(capturedStdout);
    }
    return result;
#endif
}

} // namespace foundation
