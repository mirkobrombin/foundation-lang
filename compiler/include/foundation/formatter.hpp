#ifndef FOUNDATION_FORMATTER_HPP
#define FOUNDATION_FORMATTER_HPP

#include "foundation/diagnostic.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace foundation {

struct FormatResult {
    std::string contents;
    Diagnostics diagnostics;
};

[[nodiscard]] FormatResult formatSource(std::string_view source, std::size_t sourceId = 0);

} // namespace foundation

#endif
