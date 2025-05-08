#ifndef FOUNDATION_LEXER_HPP
#define FOUNDATION_LEXER_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/token.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace foundation {

class Lexer {
  public:
    Lexer(std::string_view source, Diagnostics &diagnostics, std::size_t sourceId = 0);
    [[nodiscard]] std::vector<Token> scan();

  private:
    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] char peek(std::size_t distance = 0) const;
    char advance();
    void skipIgnored();
    Token next();
    Token identifier();
    Token integer();
    Token string();
    [[nodiscard]] SourceSpan spanFrom(std::size_t offset, std::size_t line,
                                      std::size_t column) const;

    std::string_view source_;
    Diagnostics &diagnostics_;
    std::size_t offset_{};
    std::size_t line_{1};
    std::size_t column_{1};
    std::size_t sourceId_{};
};

} // namespace foundation

#endif
