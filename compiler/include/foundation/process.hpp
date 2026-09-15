#ifndef FOUNDATION_PROCESS_HPP
#define FOUNDATION_PROCESS_HPP

#include <string>
#include <vector>

namespace foundation {

enum class ProcessOutput {
    Inherit,
    StdoutToStderr,
    StdoutToStderrOnFailure,
    // Stores the child's standard output in the captured string.
    Capture,
};

[[nodiscard]] int runProcess(const std::vector<std::string> &arguments,
                             ProcessOutput output = ProcessOutput::Inherit,
                             std::string *captured = nullptr);

} // namespace foundation

#endif
