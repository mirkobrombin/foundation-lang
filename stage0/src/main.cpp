#include "foundation/driver.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream &output) {
    output << "usage:\n"
           << "  foundationc check <source-or-project>\n"
           << "  foundationc emit-c <source-or-project> -o <output.c>\n"
           << "  foundationc build <source-or-project> -o <executable>\n"
           << "  foundationc run <source-or-project>\n"
           << "  foundationc version\n";
}

bool outputArgumentsAreValid(int argc, char **argv) {
    return argc == 5 && std::string_view(argv[3]) == "-o";
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
        if (command == "build" && outputArgumentsAreValid(argc, argv)) {
            return foundation::buildFile(std::filesystem::path(argv[2]),
                                         std::filesystem::path(argv[4]));
        }
        if (command == "run" && argc == 3) {
            return foundation::runFile(std::filesystem::path(argv[2]));
        }
    }

    printUsage(std::cerr);
    return 2;
}
