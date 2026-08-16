#include "foundation/driver.hpp"
#include "foundation/package_cli.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::ostream &output) {
    output << "usage:\n"
           << "  foundationc check <source-or-project> [--target <platform>]\n"
           << "  foundationc lint <source-or-project> [--profile <valid|standard|strict>]\n"
           << "  foundationc format <source>\n"
           << "  foundationc format --check <source-or-project>\n"
           << "  foundationc format --write <source-or-project>\n"
           << "  foundationc emit-c <source-or-project> -o <output.c>"
              " [--target <platform>]\n"
           << "  foundationc emit-c-header <source-or-project> -o <output.h>"
              " [--target <platform>]\n"
           << "  foundationc emit-llvm <source-or-project> -o <output.ll>\n"
           << "  foundationc emit-metadata <source-or-project> -o <output.json>"
              " [--target <platform>]\n"
           << "  foundationc emit-pii <project> -o <output.json>\n"
           << "  foundationc emit-fsm <source-or-project> -o <output>"
              " --format <mermaid|graphviz> [--machine <name>]\n"
           << "  foundationc documentation <source-or-project> -o <output.md>\n"
           << "  foundationc emit-app-plan <source-or-project> -o <output.json>\n"
           << "  foundationc emit-openapi <source-or-project> -o <output.json>"
              " [--title <title>] [--version <version>]\n"
           << "  foundationc emit-app-host <source-or-project> -o <output.fn>\n"
           << "  foundationc build <source-or-project> -o <executable>"
              " [--backend <llvm|c>] [--native <input>]...\n"
           << "  foundationc run <source-or-project> [--backend <llvm|c>]"
              " [--native <input>]... [-- <argument>...]\n"
           << "  foundationc test <source-or-project> [--backend <llvm|c>]"
              " [--native <input>]...\n"
           << "  foundationc package <init|resolve|fetch|verify|inspect|prune> ...\n"
           << "  foundationc version\n";
}

bool outputArgumentsAreValid(int argc, char **argv) {
    return argc == 5 && std::string_view(argv[3]) == "-o";
}

std::optional<foundation::TargetPlatform> parseTargetArguments(int argc, char **argv,
                                                               int start) {
    if (argc == start) {
        return foundation::hostTargetPlatform();
    }
    if (argc != start + 2 || std::string_view(argv[start]) != "--target") {
        return std::nullopt;
    }
    return foundation::parseTargetPlatform(argv[start + 1]);
}

std::optional<foundation::TargetPlatform> parseOutputTargetArguments(int argc, char **argv) {
    if (argc < 5 || std::string_view(argv[3]) != "-o") {
        return std::nullopt;
    }
    return parseTargetArguments(argc, argv, 5);
}

bool parseOpenAPIArguments(int argc, char **argv, std::optional<std::string> &title,
                           std::optional<std::string> &version) {
    if (argc < 5 || std::string_view(argv[3]) != "-o") {
        return false;
    }
    for (auto index = 5; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        if (option == "--title" && !title.has_value()) {
            title = argv[index + 1];
        } else if (option == "--version" && !version.has_value()) {
            version = argv[index + 1];
        } else {
            return false;
        }
    }
    return true;
}

bool parseStateMachineArguments(int argc, char **argv,
                                std::optional<std::string> &machine,
                                foundation::StateMachineDiagramFormat &format) {
    if (argc < 7 || std::string_view(argv[3]) != "-o") {
        return false;
    }
    auto formatSeen = false;
    for (auto index = 5; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        const std::string_view value = argv[index + 1];
        if (option == "--machine" && !machine.has_value()) {
            machine = value;
        } else if (option == "--format" && !formatSeen) {
            if (value == "mermaid") {
                format = foundation::StateMachineDiagramFormat::Mermaid;
            } else if (value == "graphviz") {
                format = foundation::StateMachineDiagramFormat::Graphviz;
            } else {
                return false;
            }
            formatSeen = true;
        } else {
            return false;
        }
    }
    return formatSeen;
}

bool parseNativeArguments(int argc, char **argv, int start,
                          std::vector<std::filesystem::path> &inputs,
                          foundation::BackendKind &backend) {
    auto backendSeen = false;
    for (auto index = start; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        if (option == "--native") {
            inputs.emplace_back(argv[index + 1]);
        } else if (option == "--backend" && !backendSeen) {
            const auto parsed = foundation::parseBackendKind(argv[index + 1]);
            if (!parsed.has_value()) {
                return false;
            }
            backend = *parsed;
            backendSeen = true;
        } else {
            return false;
        }
    }
    return true;
}

bool parseBuildArguments(int argc, char **argv, int start,
                         std::filesystem::path &output,
                         std::vector<std::filesystem::path> &nativeInputs,
                         foundation::BackendKind &backend) {
    auto outputSeen = false;
    auto backendSeen = false;
    for (auto index = start; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option = argv[index];
        if (option == "-o" && !outputSeen) {
            output = argv[index + 1];
            outputSeen = true;
        } else if (option == "--native") {
            nativeInputs.emplace_back(argv[index + 1]);
        } else if (option == "--backend" && !backendSeen) {
            const auto parsed = foundation::parseBackendKind(argv[index + 1]);
            if (!parsed.has_value()) {
                return false;
            }
            backend = *parsed;
            backendSeen = true;
        } else {
            return false;
        }
    }
    return outputSeen;
}

bool parseRunArguments(int argc, char **argv, int start,
                       std::vector<std::filesystem::path> &nativeInputs,
                       std::vector<std::string> &arguments,
                       foundation::BackendKind &backend) {
    auto index = start;
    auto backendSeen = false;
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
        if (index + 1 >= argc) {
            return false;
        }
        if (value == "--native") {
            nativeInputs.emplace_back(argv[index + 1]);
        } else if (value == "--backend" && !backendSeen) {
            const auto parsed = foundation::parseBackendKind(argv[index + 1]);
            if (!parsed.has_value()) {
                return false;
            }
            backend = *parsed;
            backendSeen = true;
        } else {
            return false;
        }
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
        if (command == "check" && argc >= 3) {
            const auto target = parseTargetArguments(argc, argv, 3);
            if (!target.has_value()) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::checkFile(std::filesystem::path(argv[2]), *target);
        }
        if (command == "lint" && argc == 3) {
            return foundation::lintFile(std::filesystem::path(argv[2]));
        }
        if (command == "lint" && argc == 5 && std::string_view(argv[3]) == "--profile") {
            const auto profile = foundation::parseCodeStandardProfile(argv[4]);
            if (!profile.has_value()) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::lintFile(std::filesystem::path(argv[2]), profile);
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
        if (command == "emit-c") {
            const auto target = parseOutputTargetArguments(argc, argv);
            if (!target.has_value()) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::emitCFile(std::filesystem::path(argv[2]),
                                         std::filesystem::path(argv[4]), *target);
        }
        if (command == "emit-c-header") {
            const auto target = parseOutputTargetArguments(argc, argv);
            if (!target.has_value()) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::emitCHeaderFile(std::filesystem::path(argv[2]),
                                               std::filesystem::path(argv[4]), *target);
        }
        if (command == "emit-llvm" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitLlvmIrFile(std::filesystem::path(argv[2]),
                                              std::filesystem::path(argv[4]));
        }
        if (command == "emit-metadata") {
            const auto target = parseOutputTargetArguments(argc, argv);
            if (!target.has_value()) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::emitMetadataFile(std::filesystem::path(argv[2]),
                                                std::filesystem::path(argv[4]), *target);
        }
        if (command == "emit-pii" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitPackageInterfaceFile(std::filesystem::path(argv[2]),
                                                        std::filesystem::path(argv[4]));
        }
        if (command == "emit-fsm") {
            std::optional<std::string> machine;
            auto format = foundation::StateMachineDiagramFormat::Mermaid;
            if (!parseStateMachineArguments(argc, argv, machine, format)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::emitStateMachineDiagramFile(
                std::filesystem::path(argv[2]), std::filesystem::path(argv[4]), machine,
                format);
        }
        if (command == "documentation" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitDocumentationFile(std::filesystem::path(argv[2]),
                                                     std::filesystem::path(argv[4]));
        }
        if (command == "emit-app-plan" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitApplicationPlanFile(std::filesystem::path(argv[2]),
                                                       std::filesystem::path(argv[4]));
        }
        if (command == "emit-openapi") {
            std::optional<std::string> title;
            std::optional<std::string> version;
            if (!parseOpenAPIArguments(argc, argv, title, version)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::emitOpenAPIFile(std::filesystem::path(argv[2]),
                                               std::filesystem::path(argv[4]), title,
                                               version);
        }
        if (command == "emit-app-host" && outputArgumentsAreValid(argc, argv)) {
            return foundation::emitApplicationHostFile(std::filesystem::path(argv[2]),
                                                       std::filesystem::path(argv[4]));
        }
        if (command == "build" && argc >= 5) {
            std::filesystem::path output;
            std::vector<std::filesystem::path> nativeInputs;
            auto backend = foundation::defaultBackendKind();
            if (!parseBuildArguments(argc, argv, 3, output, nativeInputs, backend)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::buildFile(std::filesystem::path(argv[2]), output, nativeInputs,
                                         backend);
        }
        if (command == "run" && argc >= 3) {
            std::vector<std::filesystem::path> nativeInputs;
            std::vector<std::string> arguments;
            auto backend = foundation::defaultBackendKind();
            if (!parseRunArguments(argc, argv, 3, nativeInputs, arguments, backend)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::runFile(std::filesystem::path(argv[2]), nativeInputs,
                                       arguments, backend);
        }
        if (command == "test" && argc >= 3) {
            std::vector<std::filesystem::path> nativeInputs;
            auto backend = foundation::defaultBackendKind();
            if (!parseNativeArguments(argc, argv, 3, nativeInputs, backend)) {
                printUsage(std::cerr);
                return 2;
            }
            return foundation::runTests(std::filesystem::path(argv[2]), nativeInputs,
                                        backend);
        }
    }

    printUsage(std::cerr);
    return 2;
}
