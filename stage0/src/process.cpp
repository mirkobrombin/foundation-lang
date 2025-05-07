#include "foundation/process.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace foundation {

int runProcess(const std::vector<std::string> &arguments) {
    if (arguments.empty()) {
        std::cerr << "foundationc: process requires an executable\n";
        return 1;
    }

    std::vector<const char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

#ifdef _WIN32
    const auto status = _spawnvp(_P_WAIT, argv.front(), argv.data());
    if (status == -1) {
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << std::strerror(errno) << '\n';
        return 1;
    }
    return status;
#else
    const auto child = fork();
    if (child == -1) {
        std::cerr << "foundationc: cannot create process: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (child == 0) {
        execvp(argv.front(), const_cast<char *const *>(argv.data()));
        std::cerr << "foundationc: cannot start " << arguments.front() << ": "
                  << std::strerror(errno) << '\n';
        _exit(127);
    }

    int status{};
    if (waitpid(child, &status, 0) == -1) {
        std::cerr << "foundationc: cannot wait for process: " << std::strerror(errno) << '\n';
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
