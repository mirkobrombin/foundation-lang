#ifndef FOUNDATION_SEMA_HPP
#define FOUNDATION_SEMA_HPP

#include "foundation/ast.hpp"
#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"
#include "foundation/type.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace foundation {

enum class CallTargetKind {
    Function,
    Print,
};

struct CallTarget {
    CallTargetKind kind{CallTargetKind::Function};
    FirFunctionId function{};
};

struct SemanticLocal {
    std::string name;
    TypeKind type{TypeKind::Invalid};
    bool mutableBinding{};
};

struct SemanticFunction {
    TypeKind returnType{TypeKind::Invalid};
    std::vector<TypeKind> parameterTypes;
    std::vector<FirLocalId> parameters;
    std::vector<SemanticLocal> locals;
};

struct SemanticModel {
    std::vector<TypeKind> expressionTypes;
    std::vector<std::optional<FirLocalId>> expressionLocals;
    std::vector<std::optional<CallTarget>> callTargets;
    std::vector<std::optional<FirLocalId>> statementLocals;
    std::vector<SemanticFunction> functions;
    FirFunctionId main{};
};

[[nodiscard]] std::optional<SemanticModel> analyze(const Program &program,
                                                   Diagnostics &diagnostics);

} // namespace foundation

#endif
