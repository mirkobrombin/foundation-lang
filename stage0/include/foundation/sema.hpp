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
    Panic,
};

struct CallTarget {
    CallTargetKind kind{CallTargetKind::Function};
    FirFunctionId function{};
    std::vector<Type> typeArguments;
};

struct SemanticLocal {
    std::string name;
    Type type{invalidType};
    bool mutableBinding{};
};

struct SemanticFunction {
    std::size_t typeParameterCount{};
    Type returnType{invalidType};
    std::vector<Type> parameterTypes;
    std::vector<FirLocalId> parameters;
    std::vector<SemanticLocal> locals;
};

struct SemanticStruct {
    std::size_t typeParameterCount{};
    std::vector<Type> fieldTypes;
};

struct SemanticEnum {
    std::size_t typeParameterCount{};
    std::vector<std::optional<Type>> payloadTypes;
};

struct StructLiteralTarget {
    Type type{invalidType};
    std::vector<FirFieldId> fields;
};

struct EnumTarget {
    Type type{invalidType};
    FirVariantId variant{};
};

struct MatchTarget {
    Type type{invalidType};
    std::vector<FirVariantId> variants;
    std::vector<std::optional<FirLocalId>> bindings;
    std::vector<std::vector<FirLocalId>> drops;
};

struct OwnershipTarget {
    OwnershipOperator operation{OwnershipOperator::Own};
    std::optional<FirLocalId> local;
};

struct SemanticModel {
    std::vector<Type> expressionTypes;
    std::vector<std::optional<FirLocalId>> expressionLocals;
    std::vector<std::optional<CallTarget>> callTargets;
    std::vector<std::optional<StructLiteralTarget>> structTargets;
    std::vector<std::optional<FirFieldId>> expressionFields;
    std::vector<std::optional<EnumTarget>> enumTargets;
    std::vector<std::optional<MatchTarget>> matchTargets;
    std::vector<std::optional<OwnershipTarget>> ownershipTargets;
    std::vector<bool> expressionMoves;
    std::vector<std::optional<FirLocalId>> statementLocals;
    std::vector<std::optional<FirLocalId>> statementElseLocals;
    std::vector<std::vector<FirLocalId>> statementDrops;
    std::vector<std::vector<FirLocalId>> blockDrops;
    std::vector<SemanticStruct> structs;
    std::vector<SemanticEnum> enums;
    std::vector<SemanticFunction> functions;
    FirFunctionId main{};
};

[[nodiscard]] std::optional<SemanticModel> analyze(const Program &program,
                                                   Diagnostics &diagnostics);

} // namespace foundation

#endif
