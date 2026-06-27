#include "foundation/driver.hpp"
#include "foundation/package_cli.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::ostream &output) {
    output << "usage:\n"
           << "  foundationc check <source-or-project>\n"
           << "  foundationc format <source>\n"
           << "  foundationc format --check <source-or-project>\n"
           << "  foundationc format --write <source-or-project>\n"
           << "  foundationc emit-c <source-or-project> -o <output.c>\n"
           << "  foundationc emit-c-header <source-or-project> -o <output.h>\n"
           << "  foundationc emit-metadata <source-or-project> -o <output.json>\n"
           << "  foundationc emit-app-plan <source-or-project> -o <output.json>\n"
           << "  foundationc emit-app-host <source-or-project> -o <output.fdn>\n"
           << "  foundationc build <source-or-project> -o <executable> [--native <input>]...\n"
           << "  foundationc run <source-or-project> [--native <input>]... [-- <argument>...]\n"
           << "  foundationc test <source-or-project> [--native <input>]...\n"
           << "  foundationc package <init|resolve|fetch|verify|inspect|prune> ...\n"
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

bool parseRunArguments(int argc, char **argv, int start,
                       std::vector<std::filesystem::path> &nativeInputs,
                       std::vector<std::string> &arguments) {
    auto index = start;
    while (index < argc) {
        const std::string_view value = argv[index];
        if (value == "--") {
            ++index;
            while (index < argc) {
                arguments.emplace_back(argv[index]);
                ++index;
            }
            return true;
        }
        if (value != "--native" || index + 1 >= argc) {
            return false;
        }
        nativeInputs.emplace_back(argv[index + 1]);
        index += 2;
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
        if (command == "package") {
            return foundation::runPackageCommand(argc, argv);
        }
        if (command == "check" && argc == 3) {
            return foundation::checkFile(std::filesystem::path(argv[2]));
        }
        if (command == "format" && argc == 3) {
            return foundation::formatPath(std::filesystem::path(argv[2]),
                                          foundation::FormatMode::Stdout);
        }
        if (command == "format" && argc == 4) {
            const std::string_view mode = argv[2];
            if (mode == "--check") {
                return foundation::formatPath(std::filesystem::path(argv[3]),
                                              foundation::FormatMode::Check);
            }
            if (mode == "--write") {
                return foundation::formatPath(std::filesystem::path(argv[3]),
                                              foundation::FormatMode::Write);
            }
        }
        if (command == "emit-c" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitCFile(std::filesystem::path(argv[2]),
                                         std::filesystem::path(argv[4]));
        }
        if (command == "emit-c-header" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitCHeaderFile(std::filesystem::path(argv[2]),
                                               std::filesystem::path(argv[4]));
        }
        if (command == "emit-metadata" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitMetadataFile(std::filesystem::path(argv[2]),
                                                std::filesystem::path(argv[4]));
        }
        if (command == "emit-app-plan" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitApplicationPlanFile(std::filesystem::path(argv[2]),
                                                       std::filesystem::path(argv[4]));
        }
        if (command == "emit-app-host" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitApplicationHostFile(std::filesystem::path(argv[2]),
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
            std::vector<std::string> arguments;
            if (!parseRunArguments(argc, argv, 3, nativeInputs, arguments)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::runFile(std::filesystem::path(argv[2]), nativeInputs, arguments);
        }
        if (command == "test" && argc >= 3) {
            std::vector<std::filesystem::path> nativeInputs;
            if (!parseNativeArguments(argc, argv, 3, nativeInputs)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::runTests(std::filesystem::path(argv[2]), nativeInputs);
        }
    }

    printUsage(std::cerr);
    return 2;
}
