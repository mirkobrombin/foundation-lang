#ifndef FOUNDATION_PROCESS_HPP
#define FOUNDATION_PROCESS_HPP

#include <string>
#include <vector>

namespace foundation {

[[nodiscard]] int runProcess(const std::vector<std::string> &arguments);

} // namespace foundation

#endif
