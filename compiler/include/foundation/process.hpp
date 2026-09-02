#ifndef FOUNDATION_PROCESS_HPP
#define FOUNDATION_PROCESS_HPP

#include <string>
#include <vector>

namespace foundation {

enum class ProcessOutput {
    Inherit,
    StdoutToStderr,
    StdoutToStderrOnFailure,
};

[[nodiscard]] int runProcess(const std::vector<std::string> &arguments,
                             ProcessOutput output = ProcessOutput::Inherit);

} // namespace foundation

#endif
