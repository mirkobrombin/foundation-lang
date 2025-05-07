#ifndef FOUNDATION_AST_HPP
#define FOUNDATION_AST_HPP

#include "foundation/diagnostic.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace foundation {

struct PrintStatement {
    std::string value;
    SourceSpan span;
};

struct ReturnStatement {
    std::int64_t value{};
    SourceSpan span;
};

using Statement = std::variant<PrintStatement, ReturnStatement>;

struct Function {
    std::string name;
    std::string returnType;
    std::vector<Statement> statements;
    SourceSpan span;
};

struct Program {
    std::vector<Function> functions;
};

} // namespace foundation

#endif
