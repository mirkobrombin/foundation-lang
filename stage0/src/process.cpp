#include "foundation/process.hpp"

#include <cerrno>
#include <iostream>
#include <limits>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;
#endif

namespace foundation {

namespace {

std::string errorMessage(int code) {
    return std::error_code(code, std::generic_category()).message();
}

} // namespace

int runProcess(const std::vector<std::string> &arguments) {
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

    const auto status = _spawnv(_P_WAIT, arguments.front().c_str(), argv.data());
    if (status == -1) {
        const auto error = errno;
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << errorMessage(error) << '\n';
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

    pid_t child{};
    const auto spawnError =
        posix_spawnp(&child, argv.front(), nullptr, nullptr, argv.data(), environ);
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
