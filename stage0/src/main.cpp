#include "foundation/driver.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::ostream &output) {
    output << "usage:\n"
           << "  foundationc check <source-or-project>\n"
           << "  foundationc emit-c <source-or-project> -o <output.c>\n"
           << "  foundationc emit-c-header <source-or-project> -o <output.h>\n"
           << "  foundationc build <source-or-project> -o <executable> [--native <input>]...\n"
           << "  foundationc run <source-or-project> [--native <input>]...\n"
           << "  foundationc version\n";
}

bool outputArgumentsAreValid(int argc, char **argv) {
    return argc == 5 && std::string_view(argv[3]) == "-o";
}

bool parseNativeArguments(int argc, char **argv, int start,
                          std::vector<std::filesystem::path> &inputs) {
    for (auto index = start; index < argc; index += 2) {
        if (index + 1 >= argc || std::string_view(argv[index]) != "--native") {
            return false;
        }
        inputs.emplace_back(argv[index + 1]);
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2) {
        const std::string_view command = argv[1];
        if (command == "version" || command == "--version") {
            std::cout << "foundationc 0.1.0-stage0\n";
            return 0;
        }
        if (command == "help" || command == "--help" || command == "-h") {
            printUsage(std::cout);
            return 0;
        }
    }

    if (argc >= 2) {
        const std::string_view command = argv[1];
        if (command == "check" && argc == 3) {
            return foundation::checkFile(std::filesystem::path(argv[2]));
        }
        if (command == "emit-c" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitCFile(std::filesystem::path(argv[2]),
                                         std::filesystem::path(argv[4]));
        }
        if (command == "emit-c-header" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitCHeaderFile(std::filesystem::path(argv[2]),
                                               std::filesystem::path(argv[4]));
        }
        if (command == "build" && argc >= 5 && std::string_view(argv[3]) == "-o") {
            std::vector<std::filesystem::path> nativeInputs;
            if (!parseNativeArguments(argc, argv, 5, nativeInputs)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::buildFile(std::filesystem::path(argv[2]),
                                         std::filesystem::path(argv[4]), nativeInputs);
        }
        if (command == "run" && argc >= 3) {
            std::vector<std::filesystem::path> nativeInputs;
            if (!parseNativeArguments(argc, argv, 3, nativeInputs)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::runFile(std::filesystem::path(argv[2]), nativeInputs);
        }
    }

    printUsage(std::cerr);
    return 2;
}
