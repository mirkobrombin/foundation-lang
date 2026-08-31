#ifndef FOUNDATION_NUMERIC_HPP
#define FOUNDATION_NUMERIC_HPP

#include <string_view>

namespace foundation {

[[nodiscard]] bool parseFloating(std::string_view text, float &value);
[[nodiscard]] bool parseFloating(std::string_view text, double &value);

} // namespace foundation

#endif
