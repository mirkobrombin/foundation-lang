#ifndef FOUNDATION_FSM_HPP
#define FOUNDATION_FSM_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"

#include <optional>
#include <string>

namespace foundation {

enum class StateMachineDiagramFormat {
    Mermaid,
    Graphviz,
};

[[nodiscard]] std::string emitStateMachineDiagram(
    const FirProgram &program, Diagnostics &diagnostics,
    const std::optional<std::string> &machine, StateMachineDiagramFormat format);

} // namespace foundation

#endif
