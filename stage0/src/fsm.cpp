#include "foundation/fsm.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace foundation {
namespace {

std::string_view simpleName(std::string_view name) {
    const auto separator = name.rfind('.');
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

std::vector<FirEnumId> stateMachines(const FirProgram &program) {
    std::vector<FirEnumId> result;
    for (FirEnumId index = 0; index < program.enums.size(); ++index) {
        if (program.enums[index].stateMachine) {
            result.push_back(index);
        }
    }
    return result;
}

std::optional<FirEnumId> selectMachine(const FirProgram &program,
                                       const std::optional<std::string> &requested,
                                       Diagnostics &diagnostics) {
    const auto machines = stateMachines(program);
    if (!requested.has_value()) {
        if (machines.empty()) {
            diagnostics.error("FDN2420", "project declares no state machine", {});
            return std::nullopt;
        }
        if (machines.size() != 1) {
            diagnostics.error("FDN2421",
                              "project declares multiple state machines; select one with "
                              "--machine",
                              {});
            return std::nullopt;
        }
        return machines.front();
    }

    std::vector<FirEnumId> matches;
    for (const auto machine : machines) {
        const auto &name = program.enums[machine].name;
        if (name == *requested || simpleName(name) == *requested) {
            matches.push_back(machine);
        }
    }
    if (matches.empty()) {
        diagnostics.error("FDN2422", "unknown state machine " + *requested, {});
        return std::nullopt;
    }
    if (matches.size() != 1) {
        diagnostics.error("FDN2423", "ambiguous state machine " + *requested, {});
        return std::nullopt;
    }
    return matches.front();
}

std::optional<FirEnumId> transitionMachine(const FirProgram &program,
                                           const FirFunction &function) {
    if (!function.stateTransition.has_value() || function.parameters.empty()) {
        return std::nullopt;
    }
    const auto receiver = function.parameters.front();
    if (receiver >= function.locals.size()) {
        return std::nullopt;
    }
    const auto &receiverType = function.locals[receiver].type;
    if (receiverType.kind != TypeKind::Edit || receiverType.arguments.size() != 1) {
        return std::nullopt;
    }
    const auto &machineType = receiverType.arguments.front();
    if (machineType.kind != TypeKind::Enum || machineType.declaration >= program.enums.size()) {
        return std::nullopt;
    }
    return machineType.declaration;
}

std::set<std::pair<std::string, std::string>> transitions(const FirProgram &program,
                                                          FirEnumId machine) {
    std::set<std::pair<std::string, std::string>> result;
    const auto &declaration = program.enums[machine];
    for (const auto &function : program.functions) {
        const auto owner = transitionMachine(program, function);
        if (!owner.has_value() || *owner != machine || !function.stateTransition.has_value()) {
            continue;
        }
        const auto &transition = *function.stateTransition;
        if (transition.destinationVariant >= declaration.variants.size()) {
            continue;
        }
        const auto &destination = declaration.variants[transition.destinationVariant].name;
        for (const auto source : transition.sourceVariants) {
            if (source < declaration.variants.size()) {
                result.emplace(declaration.variants[source].name, destination);
            }
        }
    }
    return result;
}

std::string emitMermaid(const std::set<std::pair<std::string, std::string>> &edges) {
    std::ostringstream output;
    output << "stateDiagram-v2\n";
    for (const auto &[source, destination] : edges) {
        output << "    " << source << " --> " << destination << '\n';
    }
    return output.str();
}

std::string emitGraphviz(const std::set<std::pair<std::string, std::string>> &edges) {
    std::ostringstream output;
    output << "digraph FSM {\n"
           << "    rankdir=LR;\n"
           << "    node [shape=box style=rounded];\n";
    for (const auto &[source, destination] : edges) {
        output << "    " << source << " -> " << destination << ";\n";
    }
    output << "}\n";
    return output.str();
}

} // namespace

std::string emitStateMachineDiagram(const FirProgram &program, Diagnostics &diagnostics,
                                    const std::optional<std::string> &machine,
                                    StateMachineDiagramFormat format) {
    const auto selected = selectMachine(program, machine, diagnostics);
    if (!selected.has_value()) {
        return {};
    }
    const auto edges = transitions(program, *selected);
    switch (format) {
    case StateMachineDiagramFormat::Mermaid:
        return emitMermaid(edges);
    case StateMachineDiagramFormat::Graphviz:
        return emitGraphviz(edges);
    }
    return {};
}

} // namespace foundation
