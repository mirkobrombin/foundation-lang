#ifndef FOUNDATION_LSP_HPP
#define FOUNDATION_LSP_HPP

#include <iosfwd>

namespace foundation {

[[nodiscard]] int runLanguageServer(std::istream &input, std::ostream &output,
                                    std::ostream &errors);

} // namespace foundation

#endif
