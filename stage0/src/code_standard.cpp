#include "foundation/code_standard.hpp"

namespace foundation {

std::optional<CodeStandardProfile> parseCodeStandardProfile(std::string_view value) {
    if (value == "valid") {
        return CodeStandardProfile::Valid;
    }
    if (value == "standard") {
        return CodeStandardProfile::Standard;
    }
    if (value == "strict") {
        return CodeStandardProfile::Strict;
    }
    return std::nullopt;
}

std::string_view codeStandardProfileName(CodeStandardProfile profile) {
    switch (profile) {
    case CodeStandardProfile::Valid:
        return "valid";
    case CodeStandardProfile::Standard:
        return "standard";
    case CodeStandardProfile::Strict:
        return "strict";
    }
    return "standard";
}

std::size_t codeStandardWidth(CodeStandardProfile profile) {
    switch (profile) {
    case CodeStandardProfile::Valid:
        return 0;
    case CodeStandardProfile::Standard:
        return 100;
    case CodeStandardProfile::Strict:
        return 80;
    }
    return 100;
}

} // namespace foundation
