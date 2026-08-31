#include "foundation/numeric.hpp"

#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace foundation {

namespace {

template <typename Value>
bool parse(std::string_view text, Value &value) {
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return !input.fail() && input.rdbuf()->sgetc() == std::char_traits<char>::eof() &&
           std::isfinite(value);
}

} // namespace

bool parseFloating(std::string_view text, float &value) { return parse(text, value); }

bool parseFloating(std::string_view text, double &value) { return parse(text, value); }

} // namespace foundation
