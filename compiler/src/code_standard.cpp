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

std::optional<CodeStandardSeverity> parseCodeStandardSeverity(std::string_view value) {
    if (value == "off") {
        return CodeStandardSeverity::Off;
    }
    if (value == "warning") {
        return CodeStandardSeverity::Warning;
    }
    if (value == "error") {
        return CodeStandardSeverity::Error;
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

std::string_view codeStandardSeverityName(CodeStandardSeverity severity) {
    switch (severity) {
    case CodeStandardSeverity::Off:
        return "off";
    case CodeStandardSeverity::Warning:
        return "warning";
    case CodeStandardSeverity::Error:
        return "error";
    }
    return "warning";
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

bool configurableCodeStandardRule(std::string_view code) {
    return code == "FCS1001" || code == "FCS1002" || code == "FCS2001" ||
           code == "FCS2002" || code == "FCS3001" || code == "FCS4001" ||
           code == "FCS5001" || code == "FCS6001" || code == "FCS7001" ||
           code == "FCS7002" || code == "FCS7003" || code == "FCS7004";
}

} // namespace foundation
