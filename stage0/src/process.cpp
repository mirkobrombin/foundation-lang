#include "foundation/process.hpp"

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <limits>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
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

    std::vector<const char *> argv;
    argv.reserve(arguments.size() + 1);
    argv.push_back(programName.c_str());
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        argv.push_back(arguments[index].c_str());
    }
    argv.push_back(nullptr);

    auto savedStdout = -1;
    if (output == ProcessOutput::StdoutToStderr) {
        std::cout.flush();
        std::fflush(stdout);
        savedStdout = _dup(_fileno(stdout));
        if (savedStdout == -1 || _dup2(_fileno(stderr), _fileno(stdout)) != 0) {
            const auto error = errno;
            if (savedStdout != -1) {
                _close(savedStdout);
            }
            std::cerr << "foundationc: cannot redirect process output: " << errorMessage(error)
                      << '\n';
            return 1;
        }
    }

    const auto status = _spawnv(_P_WAIT, arguments.front().c_str(), argv.data());
    const auto spawnError = errno;
    if (savedStdout != -1) {
        std::fflush(stdout);
        if (_dup2(savedStdout, _fileno(stdout)) != 0) {
            const auto error = errno;
            _close(savedStdout);
            std::cerr << "foundationc: cannot restore process output: " << errorMessage(error)
                      << '\n';
            return 1;
        }
        _close(savedStdout);
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

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actionsPointer = nullptr;
    if (output == ProcessOutput::StdoutToStderr) {
        const auto initError = posix_spawn_file_actions_init(&actions);
        if (initError != 0) {
            std::cerr << "foundationc: cannot initialize process output: "
                      << errorMessage(initError) << '\n';
            return 1;
        }
        const auto redirectError =
            posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
        if (redirectError != 0) {
            posix_spawn_file_actions_destroy(&actions);
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
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << errorMessage(spawnError) << '\n';
        return 1;
    }

    int status{};
    if (waitpid(child, &status, 0) == -1) {
        const auto error = errno;
        std::cerr << "foundationc: cannot wait for process: " << errorMessage(error) << '\n';
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
#endif
}

} // namespace foundation
