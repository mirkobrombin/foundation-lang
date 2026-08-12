#include "foundation/application.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace foundation {

namespace {

constexpr std::string_view scopeAttribute = "foundation.di.Scope";
constexpr std::string_view nameAttribute = "foundation.di.Name";
constexpr std::string_view fromAttribute = "foundation.di.From";
constexpr std::string_view actionNameAttribute = "foundation.actions.Name";
constexpr std::string_view actionKeyAttribute = "foundation.actions.Key";
constexpr std::string_view actionPolicyAttribute = "foundation.actions.Policy";
constexpr std::string_view webRouteAttribute = "foundation.web.Route";
constexpr std::string_view webPathAttribute = "foundation.web.Path";
constexpr std::string_view webQueryAttribute = "foundation.web.Query";
constexpr std::string_view webHeaderAttribute = "foundation.web.Header";
constexpr std::string_view webFormAttribute = "foundation.web.Form";
constexpr std::string_view webBodyAttribute = "foundation.web.Body";
constexpr std::string_view webInjectAttribute = "foundation.web.Inject";
constexpr std::string_view webGlobalMiddlewareAttribute =
    "foundation.web.GlobalMiddleware";
constexpr std::string_view webGroupMiddlewareAttribute =
    "foundation.web.GroupMiddleware";
constexpr std::string_view webRouteMiddlewareAttribute =
    "foundation.web.RouteMiddleware";
constexpr std::string_view openAPISummaryAttribute = "foundation.openapi.Summary";
constexpr std::string_view openAPIDescriptionAttribute =
    "foundation.openapi.Description";
constexpr std::string_view openAPIResponseAttribute = "foundation.openapi.Response";
constexpr std::string_view openAPIMinimumAttribute = "foundation.openapi.Minimum";
constexpr std::string_view openAPIEnumValueAttribute = "foundation.openapi.EnumValue";
constexpr std::string_view bindableAttribute = "foundation.bind.Bindable";
constexpr std::string_view bindNameAttribute = "foundation.bind.Name";
constexpr std::string_view bindIgnoreAttribute = "foundation.bind.Ignore";
constexpr std::string_view bindFromAttribute = "foundation.bind.From";
constexpr std::string_view bindDefaultAttribute = "foundation.bind.Default";
constexpr std::string_view bindJsonNameAttribute = "foundation.bind.JsonName";
constexpr std::string_view bindJsonAttribute = "foundation.bind.JSON";
constexpr std::string_view validatableAttribute = "foundation.validation.Validatable";
constexpr std::string_view validationRequiredAttribute = "foundation.validation.Required";
constexpr std::string_view validationMinAttribute = "foundation.validation.Min";
constexpr std::string_view validationMaxAttribute = "foundation.validation.Max";
constexpr std::string_view validationEmailAttribute = "foundation.validation.Email";
constexpr std::string_view validationPatternAttribute = "foundation.validation.Pattern";
constexpr std::string_view validationNestedAttribute = "foundation.validation.Nested";
constexpr std::string_view validationRuleAttribute = "foundation.validation.Rule";
constexpr std::string_view guardPolicyAttribute = "foundation.guard.Policy";
constexpr std::string_view guardAllowAttribute = "foundation.guard.Allow";
constexpr std::string_view guardDynamicAttribute = "foundation.guard.Dynamic";
constexpr std::string_view serializableAttribute = "foundation.serializer.Serializable";
constexpr std::string_view serializerNameAttribute = "foundation.serializer.Name";
constexpr std::string_view serializerIgnoreAttribute = "foundation.serializer.Ignore";
constexpr std::string_view serializerEncodeAttribute = "foundation.serializer.Encode";
constexpr std::string_view serializerDecodeAttribute = "foundation.serializer.Decode";

enum class Lifetime {
    Transient,
    Scoped,
    Singleton,
};

enum class ParameterMode {
    Read,
    Edit,
    Transfer,
};

struct Dependency {
    std::string parameter;
    Type type{invalidType};
    ParameterMode mode{ParameterMode::Read};
    std::optional<FirStructId> provider;
    bool input{};
};

struct ServicePlan {
    FirStructId type{};
    FirFunctionId constructor{};
    Lifetime lifetime{Lifetime::Transient};
    bool fallible{};
    std::vector<Dependency> dependencies;
};

struct ActionPlan {
    FirFunctionId function{};
    FirStructId service{};
    std::string name;
    std::vector<std::string> keys;
    std::vector<std::string> policies;
};

enum class WebBindingSource {
    Path,
    Query,
    Header,
    Form,
    Body,
    Inject,
};

struct WebParameterPlan {
    std::size_t index{};
    WebBindingSource source{WebBindingSource::Path};
    std::string name;
    std::optional<FirStructId> provider;
    std::string description;
    std::optional<std::string> minimum;
    std::vector<std::string> enumValues;
};

struct WebRoutePlan {
    FirFunctionId function{};
    std::string method;
    std::string path;
    std::vector<WebParameterPlan> parameters;
    bool task{};
    std::optional<Type> executionError;
    std::optional<Type> activationError;
    std::string summary;
    std::string description;
    std::map<std::uint16_t, std::string> responses;
};

enum class WebMiddlewareScope {
    Global,
    Group,
    Route,
};

struct WebMiddlewarePlan {
    FirFunctionId function{};
    WebMiddlewareScope scope{WebMiddlewareScope::Global};
    std::string method;
    std::string path;
    std::int64_t order{};
};

enum class StructBindingFieldKind {
    String,
    Boolean,
    Integer,
    F32,
    F64,
    Duration,
    StringList,
};

struct StructBindingFieldPlan {
    std::size_t field{};
    std::string key;
    std::string jsonKey;
    StructBindingFieldKind kind{StructBindingFieldKind::String};
    std::vector<std::pair<std::string, std::string>> sources;
    std::optional<std::string> defaultValue;
};

struct StructBindingPlan {
    FirStructId type{};
    std::vector<StructBindingFieldPlan> fields;
    std::optional<std::size_t> jsonBodyField;
};

enum class ValidationRuleKind {
    Required,
    Min,
    Max,
    Email,
    Pattern,
    Nested,
};

struct ValidationRulePlan {
    std::size_t field{};
    ValidationRuleKind kind{ValidationRuleKind::Required};
    std::optional<std::string> argument;
    long double numericLimit{};
};

struct StructValidationPlan {
    FirStructId type{};
    std::vector<ValidationRulePlan> rules;
    std::vector<FirFunctionId> customRules;
};

struct GuardStaticRulePlan {
    std::string role;
    std::string operation;
};

enum class GuardDynamicRuleKind {
    Role,
    Relationships,
};

struct GuardDynamicRulePlan {
    std::size_t field{};
    std::string operation;
    GuardDynamicRuleKind kind{GuardDynamicRuleKind::Role};
};

struct StructGuardPlan {
    FirStructId type{};
    std::vector<GuardStaticRulePlan> staticRules;
    std::vector<GuardDynamicRulePlan> dynamicRules;
};

struct SerializerFieldPlan {
    std::size_t field{};
    std::optional<std::string> name;
    bool ignored{};
};

struct StructSerializerPlan {
    FirStructId type{};
    std::vector<SerializerFieldPlan> fields;
    std::optional<FirFunctionId> encoder;
    std::optional<FirFunctionId> decoder;
};

const char *webBindingSourceName(WebBindingSource source) {
    switch (source) {
    case WebBindingSource::Path:
        return "Path";
    case WebBindingSource::Query:
        return "Query";
    case WebBindingSource::Header:
        return "Header";
    case WebBindingSource::Form:
        return "Form";
    case WebBindingSource::Body:
        return "Body";
    case WebBindingSource::Inject:
        return "Inject";
    }
    return "Body";
}

bool webIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto letter = [](const char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    };
    if (value.front() != '_' && !letter(value.front())) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [&](const char byte) {
        return byte == '_' || letter(byte) || (byte >= '0' && byte <= '9');
    });
}

bool validValidationPattern(std::string_view pattern);

bool validWebConstraints(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    constexpr auto regexPrefix = std::string_view{"regex("};
    if (value.starts_with(regexPrefix)) {
        if (!value.ends_with(')')) {
            return false;
        }
        return validValidationPattern(
            value.substr(regexPrefix.size(), value.size() - regexPrefix.size() - 1));
    }
    std::size_t start{};
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto constraint = value.substr(
            start, end == std::string_view::npos ? value.size() - start : end - start);
        if (constraint != "int" && constraint != "alpha") {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

bool validateWebRoutePath(std::string_view path, std::string &reason) {
    if (path.empty() || path.front() != '/') {
        reason = "path must begin with /";
        return false;
    }
    std::set<std::string> names;
    std::size_t start = 1;
    while (start <= path.size()) {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto segment = path.substr(start, end - start);
        const auto open = segment.find('{');
        const auto close = segment.find('}');
        if (open != std::string_view::npos || close != std::string_view::npos) {
            if (segment.size() < 3 || segment.front() != '{' || segment.back() != '}' ||
                segment.find('{', 1) != std::string_view::npos ||
                segment.find('}') != segment.size() - 1) {
                reason = "parameter braces must occupy one complete segment";
                return false;
            }
            auto inner = segment.substr(1, segment.size() - 2);
            const auto catchAll = !inner.empty() && inner.front() == '*';
            if (catchAll) {
                inner.remove_prefix(1);
                if (separator != std::string_view::npos) {
                    reason = "catch-all parameter must be the last segment";
                    return false;
                }
                if (inner.find(':') != std::string_view::npos) {
                    reason = "catch-all parameter cannot declare constraints";
                    return false;
                }
            }
            const auto colon = inner.find(':');
            const auto name = inner.substr(0, colon);
            const auto constraints =
                colon == std::string_view::npos ? std::string_view{} : inner.substr(colon + 1);
            if (!webIdentifier(name)) {
                reason = "route parameter name is invalid";
                return false;
            }
            if (!names.insert(std::string(name)).second) {
                reason = "route parameter name is duplicated";
                return false;
            }
            if (!catchAll && !validWebConstraints(constraints)) {
                reason = "route constraint is unsupported";
                return false;
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return true;
}

bool validateWebGroupPrefix(std::string_view prefix) {
    if (prefix.empty() || prefix.front() != '/' ||
        (prefix.size() > 1 && prefix.back() == '/')) {
        return false;
    }
    return prefix.find_first_of("{}?#") == std::string_view::npos;
}

bool webRouteInGroup(std::string_view route, std::string_view prefix) {
    if (prefix == "/") {
        return route.starts_with('/');
    }
    return route == prefix ||
           (route.size() > prefix.size() && route.starts_with(prefix) &&
            route[prefix.size()] == '/');
}

std::vector<std::string_view> webRouteSegments(std::string_view path) {
    std::vector<std::string_view> segments;
    std::size_t start = 1;
    while (start < path.size()) {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        segments.push_back(path.substr(start, end - start));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return segments;
}

bool webParameterSegment(std::string_view segment) {
    return segment.size() >= 3 && segment.front() == '{' && segment.back() == '}';
}

bool webCatchAllSegment(std::string_view segment) {
    return webParameterSegment(segment) && segment[1] == '*';
}

std::pair<std::string_view, std::string_view> webParameterParts(std::string_view segment) {
    auto inner = segment.substr(1, segment.size() - 2);
    if (!inner.empty() && inner.front() == '*') {
        inner.remove_prefix(1);
    }
    const auto colon = inner.find(':');
    return {inner.substr(0, colon),
            colon == std::string_view::npos ? std::string_view{} : inner.substr(colon + 1)};
}

bool webRoutePatternsAmbiguous(std::string_view left, std::string_view right) {
    const auto leftSegments = webRouteSegments(left);
    const auto rightSegments = webRouteSegments(right);
    const auto count = std::min(leftSegments.size(), rightSegments.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto leftSegment = leftSegments[index];
        const auto rightSegment = rightSegments[index];
        const auto leftParameter = webParameterSegment(leftSegment);
        const auto rightParameter = webParameterSegment(rightSegment);
        if (!leftParameter || !rightParameter) {
            if (leftParameter != rightParameter || leftSegment != rightSegment) {
                return false;
            }
            continue;
        }

        const auto leftCatchAll = webCatchAllSegment(leftSegment);
        const auto rightCatchAll = webCatchAllSegment(rightSegment);
        if (leftCatchAll != rightCatchAll) {
            return true;
        }
        const auto leftParts = webParameterParts(leftSegment);
        const auto rightParts = webParameterParts(rightSegment);
        if (leftParts != rightParts) {
            return true;
        }
    }
    return false;
}

bool webRouteHasParameter(std::string_view path, std::string_view expected) {
    std::size_t start = 1;
    while (start <= path.size()) {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto segment = path.substr(start, end - start);
        if (segment.size() >= 3 && segment.front() == '{' && segment.back() == '}') {
            auto inner = segment.substr(1, segment.size() - 2);
            if (!inner.empty() && inner.front() == '*') {
                inner.remove_prefix(1);
            }
            const auto colon = inner.find(':');
            if (inner.substr(0, colon) == expected) {
                return true;
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return false;
}

bool isBuiltinOption(const FirProgram &program, const Type &type) {
    return type.kind == TypeKind::Enum && type.declaration < program.enums.size() &&
           program.enums[type.declaration].builtin &&
           program.enums[type.declaration].name == "Option" && type.arguments.size() == 1;
}

bool webSourceTypeSupported(const FirProgram &program, const Type &type) {
    auto value = type;
    while ((value.kind == TypeKind::View || value.kind == TypeKind::Edit ||
            value.kind == TypeKind::Own) &&
           value.arguments.size() == 1) {
        value = value.arguments.front();
    }
    if (value == stringType || value == boolType || isInteger(value)) {
        return true;
    }
    return isBuiltinOption(program, value) && value.arguments.front() == stringType;
}

const char *webIntegerParser(const Type &type) {
    switch (type.kind) {
    case TypeKind::I8:
        return "I8";
    case TypeKind::I16:
        return "I16";
    case TypeKind::I32:
        return "I32";
    case TypeKind::I64:
        return "I64";
    case TypeKind::U8:
        return "U8";
    case TypeKind::U16:
        return "U16";
    case TypeKind::U32:
        return "U32";
    case TypeKind::U64:
        return "U64";
    case TypeKind::Isize:
        return "Isize";
    case TypeKind::Usize:
        return "Usize";
    default:
        return "";
    }
}

Type baseType(Type type);
bool hasAttribute(const FirProgram &program, const std::vector<FirAttributeUse> &uses,
                  std::string_view name);

bool isNamedStruct(const FirProgram &program, const Type &type, std::string_view name) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           program.structs[value.declaration].name == name;
}

bool isNamedEnum(const FirProgram &program, const Type &type, std::string_view name) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Enum && value.declaration < program.enums.size() &&
           program.enums[value.declaration].name == name;
}

bool isStringList(const FirProgram &program, const Type &type) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           program.structs[value.declaration].name == "std.collections.List" &&
           value.arguments.size() == 1 && value.arguments.front() == stringType;
}

bool isList(const FirProgram &program, const Type &type) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           program.structs[value.declaration].name == "std.collections.List" &&
           value.arguments.size() == 1;
}

const char *serializerScalarName(const Type &type) {
    switch (baseType(type).kind) {
    case TypeKind::String:
        return "String";
    case TypeKind::Bool:
        return "Boolean";
    case TypeKind::I8:
        return "I8";
    case TypeKind::I16:
        return "I16";
    case TypeKind::I32:
        return "I32";
    case TypeKind::I64:
        return "I64";
    case TypeKind::Isize:
        return "Isize";
    case TypeKind::U8:
        return "U8";
    case TypeKind::U16:
        return "U16";
    case TypeKind::U32:
        return "U32";
    case TypeKind::U64:
        return "U64";
    case TypeKind::Usize:
        return "Usize";
    case TypeKind::F32:
        return "F32";
    case TypeKind::F64:
        return "F64";
    default:
        return nullptr;
    }
}

bool serializerTypeSupported(const FirProgram &program, const Type &type) {
    const auto value = baseType(type);
    if (serializerScalarName(value) != nullptr ||
        isNamedStruct(program, value, "std.prelude.UUID")) {
        return true;
    }
    if (isBuiltinOption(program, value)) {
        return serializerTypeSupported(program, value.arguments.front());
    }
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           hasAttribute(program, program.structs[value.declaration].attributes,
                        serializableAttribute);
}

bool serializerResult(const FirProgram &program, const Type &type, const Type &success) {
    const auto value = baseType(type);
    return isNamedEnum(program, value, "Result") && value.arguments.size() == 2 &&
           value.arguments.front() == success &&
           isNamedStruct(program, value.arguments[1], "foundation.serializer.Error");
}

std::optional<StructBindingFieldKind> structBindingFieldKind(const FirProgram &program,
                                                             const Type &type) {
    const auto value = baseType(type);
    if (value == stringType) {
        return StructBindingFieldKind::String;
    }
    if (value == boolType) {
        return StructBindingFieldKind::Boolean;
    }
    if (isInteger(value)) {
        return StructBindingFieldKind::Integer;
    }
    if (value == f32Type) {
        return StructBindingFieldKind::F32;
    }
    if (value == f64Type) {
        return StructBindingFieldKind::F64;
    }
    if (isNamedStruct(program, value, "std.time.Duration")) {
        return StructBindingFieldKind::Duration;
    }
    if (isStringList(program, value)) {
        return StructBindingFieldKind::StringList;
    }
    return std::nullopt;
}

const char *structBindingParser(const Type &type) {
    if (type == boolType) {
        return "Bool";
    }
    if (type == f32Type) {
        return "F32";
    }
    if (type == f64Type) {
        return "F64";
    }
    return webIntegerParser(type);
}

const FirAttributeDeclaration *attributeDeclaration(const FirProgram &program,
                                                    const FirAttributeUse &use) {
    if (use.declaration >= program.attributeDeclarations.size()) {
        return nullptr;
    }
    return &program.attributeDeclarations[use.declaration];
}

bool hasAttribute(const FirProgram &program, const std::vector<FirAttributeUse> &uses,
                  std::string_view name) {
    return std::any_of(uses.begin(), uses.end(), [&](const auto &use) {
        const auto declaration = attributeDeclaration(program, use);
        return declaration != nullptr && declaration->name == name;
    });
}

const FirAttributeUse *findAttribute(const FirProgram &program,
                                     const std::vector<FirAttributeUse> &uses,
                                     std::string_view name) {
    const auto found = std::find_if(uses.begin(), uses.end(), [&](const auto &use) {
        const auto declaration = attributeDeclaration(program, use);
        return declaration != nullptr && declaration->name == name;
    });
    return found == uses.end() ? nullptr : &*found;
}

std::optional<std::string> stringArgument(const FirAttributeUse *use,
                                          std::size_t index = 0) {
    if (use == nullptr || index >= use->arguments.size() ||
        use->arguments[index].value.kind != FirAttributeValueKind::String) {
        return std::nullopt;
    }
    return use->arguments[index].value.text;
}

struct NumericAttributeArgument {
    std::string literal;
    long double value{};
    bool integer{};
    std::uint64_t magnitude{};
    bool negative{};
};

std::optional<NumericAttributeArgument> numericArgument(const FirAttributeUse *use,
                                                        std::size_t index = 0) {
    if (use == nullptr || index >= use->arguments.size()) {
        return std::nullopt;
    }
    const auto &value = use->arguments[index].value;
    if (value.kind == FirAttributeValueKind::Integer) {
        auto literal = std::to_string(value.magnitude);
        auto numeric = static_cast<long double>(value.magnitude);
        if (value.negative) {
            literal.insert(literal.begin(), '-');
            numeric = -numeric;
        }
        return NumericAttributeArgument{std::move(literal), numeric, true, value.magnitude,
                                        value.negative};
    }
    if (value.kind != FirAttributeValueKind::Floating) {
        return std::nullopt;
    }
    double numeric{};
    const auto conversion = std::from_chars(value.text.data(),
                                            value.text.data() + value.text.size(), numeric);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != value.text.data() + value.text.size() || !std::isfinite(numeric)) {
        return std::nullopt;
    }
    return NumericAttributeArgument{value.text, static_cast<long double>(numeric), false, 0,
                                    false};
}

bool validationIntegerFits(const Type &type, const NumericAttributeArgument &value) {
    if (!value.integer) {
        return false;
    }
    if (isUnsignedInteger(type)) {
        if (value.negative) {
            return false;
        }
        switch (type.kind) {
        case TypeKind::U8:
            return value.magnitude <= UINT8_MAX;
        case TypeKind::U16:
            return value.magnitude <= UINT16_MAX;
        case TypeKind::U32:
            return value.magnitude <= UINT32_MAX;
        case TypeKind::U64:
            return true;
        case TypeKind::Usize:
            return value.magnitude <= std::numeric_limits<std::size_t>::max();
        default:
            return false;
        }
    }
    std::uint64_t maximum{};
    switch (type.kind) {
    case TypeKind::I8:
        maximum = INT8_MAX;
        break;
    case TypeKind::I16:
        maximum = INT16_MAX;
        break;
    case TypeKind::I32:
        maximum = INT32_MAX;
        break;
    case TypeKind::I64:
        maximum = INT64_MAX;
        break;
    case TypeKind::Isize:
        maximum = static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max());
        break;
    default:
        return false;
    }
    return value.magnitude <= maximum + (value.negative ? UINT64_C(1) : UINT64_C(0));
}

bool escapedPatternByte(std::string_view pattern, std::size_t index) {
    auto escapes = std::size_t{};
    while (index > 0 && pattern[index - 1] == '\\') {
        --index;
        ++escapes;
    }
    return escapes % 2 != 0;
}

bool validValidationPattern(std::string_view pattern) {
    if (pattern.size() > 1024) {
        return false;
    }
    auto cursor = std::size_t{};
    auto end = pattern.size();
    if (cursor < end && pattern[cursor] == '^') {
        ++cursor;
    }
    if (end > cursor && pattern[end - 1] == '$' &&
        !escapedPatternByte(pattern, end - 1)) {
        --end;
    }
    while (cursor < end) {
        const auto current = pattern[cursor];
        if (current == '*' || current == '+' || current == '?' || current == '$' ||
            current == '^' || current == '(' || current == ')' || current == '|' ||
            current == '{' || current == '}') {
            return false;
        }
        if (current == '\\') {
            cursor += 2;
            if (cursor > end) {
                return false;
            }
        } else if (current == '[') {
            ++cursor;
            if (cursor < end && pattern[cursor] == '^') {
                ++cursor;
            }
            const auto contents = cursor;
            auto closed = false;
            while (cursor < end) {
                if (pattern[cursor] == '\\') {
                    cursor += 2;
                    if (cursor > end) {
                        return false;
                    }
                    continue;
                }
                if (pattern[cursor] == ']') {
                    closed = true;
                    break;
                }
                ++cursor;
            }
            if (!closed || cursor == contents) {
                return false;
            }
            ++cursor;
        } else {
            ++cursor;
        }
        if (cursor < end &&
            (pattern[cursor] == '*' || pattern[cursor] == '+' ||
             pattern[cursor] == '?')) {
            ++cursor;
            if (cursor < end &&
                (pattern[cursor] == '*' || pattern[cursor] == '+' ||
                 pattern[cursor] == '?')) {
                return false;
            }
        }
    }
    return true;
}

std::string enumCase(const FirProgram &program, const FirAttributeUse *use,
                     std::size_t index = 0) {
    if (use == nullptr || index >= use->arguments.size()) {
        return {};
    }
    const auto &value = use->arguments[index].value;
    if (value.kind != FirAttributeValueKind::Enum || value.type.kind != TypeKind::Enum ||
        value.type.declaration >= program.enums.size()) {
        return {};
    }
    const auto &type = program.enums[value.type.declaration];
    if (value.variant >= type.variants.size()) {
        return {};
    }
    return type.variants[value.variant].name;
}

std::optional<std::int64_t> signedIntegerArgument(const FirAttributeUse *use,
                                                  std::size_t index) {
    if (use == nullptr || index >= use->arguments.size()) {
        return std::nullopt;
    }
    const auto &value = use->arguments[index].value;
    if (value.kind != FirAttributeValueKind::Integer) {
        return std::nullopt;
    }
    constexpr auto minimumMagnitude = std::uint64_t{INT64_MAX} + 1;
    if (value.negative) {
        if (value.magnitude > minimumMagnitude) {
            return std::nullopt;
        }
        return value.magnitude == minimumMagnitude
                   ? INT64_MIN
                   : -static_cast<std::int64_t>(value.magnitude);
    }
    if (value.magnitude > INT64_MAX) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value.magnitude);
}

Lifetime serviceLifetime(const FirProgram &program, const FirStruct &service) {
    const auto value = enumCase(program, findAttribute(program, service.attributes,
                                                       scopeAttribute));
    if (value == "Scoped") {
        return Lifetime::Scoped;
    }
    if (value == "Singleton") {
        return Lifetime::Singleton;
    }
    return Lifetime::Transient;
}

const char *lifetimeName(Lifetime lifetime) {
    switch (lifetime) {
    case Lifetime::Transient:
        return "transient";
    case Lifetime::Scoped:
        return "scoped";
    case Lifetime::Singleton:
        return "singleton";
    }
    return "transient";
}

int lifetimeRank(Lifetime lifetime) {
    switch (lifetime) {
    case Lifetime::Transient:
        return 0;
    case Lifetime::Scoped:
        return 1;
    case Lifetime::Singleton:
        return 2;
    }
    return 0;
}

Type baseType(Type type) {
    while ((type.kind == TypeKind::View || type.kind == TypeKind::Edit ||
            type.kind == TypeKind::Own) &&
           type.arguments.size() == 1) {
        type = type.arguments.front();
    }
    return type;
}

Type openAPIParameterType(const FirProgram &program, Type type, bool &optional) {
    type = baseType(std::move(type));
    optional = isBuiltinOption(program, type);
    if (optional) {
        return baseType(type.arguments.front());
    }
    return type;
}

std::string_view openAPISchemaType(const FirProgram &program, const Type &type) {
    bool optional{};
    const auto value = openAPIParameterType(program, type, optional);
    if (value == stringType) {
        return "string";
    }
    if (value == boolType) {
        return "boolean";
    }
    if (isInteger(value)) {
        return "integer";
    }
    if (isFloating(value)) {
        return "number";
    }
    return {};
}

ParameterMode parameterMode(const FirFunction &function, std::size_t index) {
    const auto local = function.parameters[index];
    const auto &type = function.locals[local].type;
    if (type.kind == TypeKind::Edit) {
        return ParameterMode::Edit;
    }
    if (type.kind == TypeKind::View ||
        (index < function.readParameters.size() && function.readParameters[index])) {
        return ParameterMode::Read;
    }
    return ParameterMode::Transfer;
}

bool webMiddlewareSignature(const FirProgram &program, const FirFunction &function) {
    if (!function.generic || function.typeParameterCount != 1 || function.parameters.size() != 2) {
        return false;
    }
    const auto requestLocal = function.parameters[0];
    const auto nextLocal = function.parameters[1];
    if (parameterMode(function, 0) != ParameterMode::Transfer ||
        parameterMode(function, 1) != ParameterMode::Read ||
        !isNamedStruct(program, function.locals[requestLocal].type,
                       "foundation.web.Request")) {
        return false;
    }
    const auto next = baseType(function.locals[nextLocal].type);
    if (next.kind != TypeKind::Function || next.arguments.size() != 2 ||
        next.arguments.front() != function.returnType ||
        !isNamedStruct(program, next.arguments[1], "foundation.web.Request")) {
        return false;
    }
    if (!isNamedEnum(program, function.returnType, "Result") ||
        function.returnType.arguments.size() != 2 ||
        !program.enums[baseType(function.returnType).declaration].builtin ||
        !isNamedStruct(program, function.returnType.arguments[0],
                       "foundation.web.Response")) {
        return false;
    }
    const auto dispatchError = baseType(function.returnType.arguments[1]);
    return isNamedEnum(program, dispatchError, "foundation.web.DispatchError") &&
           dispatchError.arguments.size() == 1 &&
           dispatchError.arguments.front().kind == TypeKind::Parameter &&
           dispatchError.arguments.front().declaration == 0;
}

const char *parameterModeName(ParameterMode mode) {
    switch (mode) {
    case ParameterMode::Read:
        return "read";
    case ParameterMode::Edit:
        return "edit";
    case ParameterMode::Transfer:
        return "transfer";
    }
    return "read";
}

const char *receiverName(FirReceiverKind receiver) {
    switch (receiver) {
    case FirReceiverKind::View:
        return "read";
    case FirReceiverKind::Edit:
        return "edit";
    case FirReceiverKind::Own:
        return "transfer";
    }
    return "read";
}

std::string typeName(const FirProgram &program, const Type &type) {
    if (isMachineScalar(type)) {
        return foundation::typeName(type);
    }
    if (type == stringType) {
        return "String";
    }
    if (type.kind == TypeKind::Parameter) {
        return "$" + std::to_string(type.declaration);
    }
    if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
        return "[" + std::to_string(type.declaration) + "]" +
               typeName(program, type.arguments.front());
    }
    if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
        return "[" + typeName(program, type.arguments.front()) + "]";
    }
    if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
        type.arguments.size() == 1) {
        return std::string(type.kind == TypeKind::Raw ? "*" : "*const ") +
               typeName(program, type.arguments.front());
    }

    std::string result;
    if (type.kind == TypeKind::Own) {
        result = "own ";
    } else if (type.kind == TypeKind::View) {
        result = "view ";
    } else if (type.kind == TypeKind::Edit) {
        result = "edit ";
    } else if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
        result = program.structs[type.declaration].name;
    } else if (type.kind == TypeKind::Enum && type.declaration < program.enums.size()) {
        result = program.enums[type.declaration].name;
    } else if (type.kind == TypeKind::Contract && type.declaration < program.contracts.size()) {
        result = program.contracts[type.declaration].name;
    } else {
        result = foundation::typeName(type);
    }
    if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
         type.kind == TypeKind::Edit) &&
        type.arguments.size() == 1) {
        return result + typeName(program, type.arguments.front());
    }
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                result += ',';
            }
            result += typeName(program, type.arguments[index]);
        }
        result += '>';
    }
    return result;
}

void emitString(std::ostringstream &out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const auto byte : value) {
        const auto valueByte = static_cast<unsigned char>(byte);
        if (byte == '"' || byte == '\\') {
            out << '\\' << byte;
        } else if (byte == '\n') {
            out << "\\n";
        } else if (byte == '\r') {
            out << "\\r";
        } else if (byte == '\t') {
            out << "\\t";
        } else if (valueByte < 0x20) {
            out << "\\u00" << hex[valueByte >> 4] << hex[valueByte & 0x0f];
        } else {
            out << byte;
        }
    }
    out << '"';
}

std::string ownerName(const FirFunction &function) {
    const auto separator = function.name.rfind('.');
    return separator == std::string::npos ? std::string{}
                                          : function.name.substr(0, separator);
}

bool returnsService(const FirProgram &program, const FirFunction &function,
                    FirStructId service, bool &fallible) {
    fallible = false;
    if (function.returnType.kind == TypeKind::Struct &&
        function.returnType.declaration == service) {
        return true;
    }
    if (function.returnType.kind != TypeKind::Enum ||
        function.returnType.declaration >= program.enums.size() ||
        !program.enums[function.returnType.declaration].builtin ||
        program.enums[function.returnType.declaration].name != "Result" ||
        function.returnType.arguments.size() != 2) {
        return false;
    }
    const auto &value = function.returnType.arguments.front();
    if (value.kind != TypeKind::Struct || value.declaration != service) {
        return false;
    }
    fallible = true;
    return true;
}

bool isResultWithError(const FirProgram &program, const Type &type, const Type &error) {
    return type.kind == TypeKind::Enum && type.declaration < program.enums.size() &&
           program.enums[type.declaration].builtin &&
           program.enums[type.declaration].name == "Result" && type.arguments.size() == 2 &&
           type.arguments[1] == error;
}

bool isResultType(const FirProgram &program, const Type &type) {
    return type.kind == TypeKind::Enum && type.declaration < program.enums.size() &&
           program.enums[type.declaration].builtin &&
           program.enums[type.declaration].name == "Result" && type.arguments.size() == 2;
}

bool implements(const FirStruct &service, const Type &contract) {
    return std::find(service.implementations.begin(), service.implementations.end(), contract) !=
           service.implementations.end();
}

std::vector<FirStructId> providerCandidates(const FirProgram &program, const Type &requested,
                                            const std::optional<std::string> &name) {
    std::vector<FirStructId> candidates;
    const auto base = baseType(requested);
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        const auto &service = program.structs[index];
        if (!service.service) {
            continue;
        }
        const auto matchesType =
            (base.kind == TypeKind::Struct && base.declaration == index) ||
            (base.kind == TypeKind::Contract && implements(service, base));
        if (!matchesType) {
            continue;
        }
        if (name.has_value() &&
            stringArgument(findAttribute(program, service.attributes, nameAttribute)) != name) {
            continue;
        }
        candidates.push_back(index);
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto left, const auto right) {
        return program.structs[left].name < program.structs[right].name;
    });
    return candidates;
}

bool serviceDependencyType(const FirProgram &program, const Type &requested) {
    const auto base = baseType(requested);
    if (base.kind == TypeKind::Contract) {
        return true;
    }
    return base.kind == TypeKind::Struct && base.declaration < program.structs.size() &&
           program.structs[base.declaration].service;
}

std::string joinServiceNames(const FirProgram &program,
                             const std::vector<FirStructId> &services) {
    std::string result;
    for (const auto service : services) {
        if (!result.empty()) {
            result += ", ";
        }
        result += program.structs[service].name;
    }
    return result;
}

SourceSpan parameterSpan(const FirFunction &function) {
    return function.sourceSpan;
}

std::string_view shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return name.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

std::string packageName(std::string_view name) {
    const auto separator = name.rfind('.');
    return separator == std::string_view::npos ? std::string{}
                                               : std::string(name.substr(0, separator));
}

bool webBodyTypeSupported(const FirProgram &program, const Type &type,
                          std::string_view routePackage) {
    const auto value = baseType(type);
    if (value == stringType) {
        return true;
    }
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           value.arguments.empty() &&
           program.structs[value.declaration].typeParameterCount == 0 &&
           packageName(program.structs[value.declaration].name) == routePackage &&
           hasAttribute(program, program.structs[value.declaration].attributes,
                        bindableAttribute);
}

std::string hostIdentifier(std::string_view value, bool exported) {
    std::string result;
    auto capitalize = true;
    for (const auto byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        if (!std::isalnum(character) && byte != '_') {
            capitalize = true;
            continue;
        }
        if (capitalize && std::isalpha(character)) {
            result += static_cast<char>(std::toupper(character));
        } else {
            result += byte;
        }
        capitalize = false;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(0, "Action");
    }
    if (!exported && std::isalpha(static_cast<unsigned char>(result.front()))) {
        result.front() = static_cast<char>(std::tolower(
            static_cast<unsigned char>(result.front())));
    }
    return result;
}

void collectTypePackages(const FirProgram &program, const Type &type,
                         std::set<std::string> &packages) {
    std::string declaration;
    if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
        declaration = program.structs[type.declaration].name;
    } else if (type.kind == TypeKind::Enum && type.declaration < program.enums.size()) {
        declaration = program.enums[type.declaration].name;
    } else if (type.kind == TypeKind::Contract &&
               type.declaration < program.contracts.size()) {
        declaration = program.contracts[type.declaration].name;
    }
    if (!declaration.empty()) {
        const auto package = packageName(declaration);
        if (!package.empty()) {
            packages.insert(package);
        }
    }
    for (const auto &argument : type.arguments) {
        collectTypePackages(program, argument, packages);
    }
}

std::string qualifiedName(std::string_view name, std::string_view rootPackage,
                          const std::map<std::string, std::string> &aliases) {
    const auto package = packageName(name);
    const auto declaration = shortName(name);
    if (package.empty() || package == rootPackage) {
        return std::string(declaration);
    }
    const auto alias = aliases.find(package);
    if (alias == aliases.end()) {
        return std::string(name);
    }
    return alias->second + '.' + std::string(declaration);
}

std::string sourceTypeName(const FirProgram &program, const Type &type,
                           std::string_view rootPackage,
                           const std::map<std::string, std::string> &aliases);

std::string sourceFunctionParameterType(
    const FirProgram &program, const Type &type, std::string_view rootPackage,
    const std::map<std::string, std::string> &aliases) {
    if (type.kind == TypeKind::Edit && type.arguments.size() == 1) {
        return '&' + sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    if (type.kind == TypeKind::Own && type.arguments.size() == 1) {
        return '$' + sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    if (type.kind == TypeKind::View && type.arguments.size() == 1) {
        return sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    return sourceTypeName(program, type, rootPackage, aliases);
}

std::string sourceTypeName(const FirProgram &program, const Type &type,
                           std::string_view rootPackage,
                           const std::map<std::string, std::string> &aliases) {
    if (isMachineScalar(type) || type == stringType) {
        return foundation::typeName(type);
    }
    if (type.kind == TypeKind::Parameter) {
        return "T" + std::to_string(type.declaration);
    }
    if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
         type.kind == TypeKind::Edit) &&
        type.arguments.size() == 1) {
        return std::string(type.kind == TypeKind::Own
                               ? "own "
                               : type.kind == TypeKind::View ? "view " : "edit ") +
               sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
        type.arguments.size() == 1) {
        return std::string(type.kind == TypeKind::Raw ? "*" : "*const ") +
               sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
        return '[' + std::to_string(type.declaration) + ']' +
               sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
    }
    if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
        return '[' + sourceTypeName(program, type.arguments.front(), rootPackage, aliases) + ']';
    }
    if (type.kind == TypeKind::Function && !type.arguments.empty()) {
        std::string result =
            isTransferableFunction(type) ? "transferable fn(" : "fn(";
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            if (index != 1) {
                result += ", ";
            }
            result += sourceFunctionParameterType(program, type.arguments[index], rootPackage,
                                                  aliases);
        }
        result += ") " +
                  sourceTypeName(program, type.arguments.front(), rootPackage, aliases);
        return result;
    }
    if ((type.kind == TypeKind::Task || type.kind == TypeKind::Channel ||
         type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver) &&
        type.arguments.size() == 1) {
        return std::string(foundation::typeName(type)) + '<' +
               sourceTypeName(program, type.arguments.front(), rootPackage, aliases) + '>';
    }

    std::string result;
    if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
        result = qualifiedName(program.structs[type.declaration].name, rootPackage, aliases);
    } else if (type.kind == TypeKind::Enum && type.declaration < program.enums.size()) {
        result = qualifiedName(program.enums[type.declaration].name, rootPackage, aliases);
    } else if (type.kind == TypeKind::Contract &&
               type.declaration < program.contracts.size()) {
        result = qualifiedName(program.contracts[type.declaration].name, rootPackage, aliases);
    } else {
        result = foundation::typeName(type);
    }
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                result += ", ";
            }
            result += sourceTypeName(program, type.arguments[index], rootPackage, aliases);
        }
        result += '>';
    }
    return result;
}

const char *parameterMarker(ParameterMode mode) {
    switch (mode) {
    case ParameterMode::Read:
        return "";
    case ParameterMode::Edit:
        return "&";
    case ParameterMode::Transfer:
        return "$";
    }
    return "";
}

std::string serviceFieldName(const FirProgram &program, FirStructId service) {
    return "foundation" + hostIdentifier(shortName(program.structs[service].name), true);
}

std::string inputName(const FirProgram &program, FirStructId service,
                      std::string_view parameter) {
    return serviceFieldName(program, service) + hostIdentifier(parameter, true);
}

std::string functionName(const FirFunction &function, std::string_view rootPackage,
                         const std::map<std::string, std::string> &aliases) {
    auto local = std::string_view(function.name);
    if (!function.packageName.empty() &&
        local.starts_with(function.packageName + '.')) {
        local.remove_prefix(function.packageName.size() + 1);
    }
    if (function.packageName.empty() || function.packageName == rootPackage) {
        return std::string(local);
    }
    const auto alias = aliases.find(function.packageName);
    return alias == aliases.end() ? function.name : alias->second + '.' + std::string(local);
}

std::string join(const std::vector<std::string> &values) {
    std::string result;
    for (const auto &value : values) {
        if (!result.empty()) {
            result += ", ";
        }
        result += value;
    }
    return result;
}

std::string emitApplicationHostSource(const FirProgram &program,
                                      const std::map<FirStructId, ServicePlan> &plans,
                                      const std::vector<FirStructId> &order,
                                      const std::vector<ActionPlan> &actions,
                                      const std::vector<WebRoutePlan> &webRoutes,
                                      const std::vector<WebMiddlewarePlan> &webMiddlewares,
                                      Diagnostics &diagnostics,
                                      const std::string &rootPackage,
                                      std::string_view generatedSourcePath,
                                      bool includeApplicationHost) {
    std::vector<StructBindingPlan> structBindings;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        const auto &type = program.structs[index];
        if (packageName(type.name) != rootPackage || type.sourcePath == generatedSourcePath) {
            continue;
        }
        const auto bindable = hasAttribute(program, type.attributes, bindableAttribute);
        const auto hasFieldMetadata = std::any_of(
            type.fields.begin(), type.fields.end(), [&](const auto &field) {
                return hasAttribute(program, field.attributes, bindNameAttribute) ||
                       hasAttribute(program, field.attributes, bindIgnoreAttribute) ||
                       hasAttribute(program, field.attributes, bindFromAttribute) ||
                       hasAttribute(program, field.attributes, bindDefaultAttribute) ||
                       hasAttribute(program, field.attributes, bindJsonNameAttribute) ||
                       hasAttribute(program, field.attributes, bindJsonAttribute);
            });
        if (!bindable) {
            if (hasFieldMetadata) {
                diagnostics.error(
                    "FDN2371",
                    "binding field metadata requires @bind.Bindable on " + type.name,
                    type.sourceSpan);
            }
            continue;
        }
        if (type.typeParameterCount != 0) {
            diagnostics.error("FDN2370", "generated binding requires a concrete struct",
                              type.sourceSpan);
            continue;
        }
        const auto collides = std::find_if(
            program.functions.begin(), program.functions.end(), [&](const auto &function) {
                return function.sourcePath != generatedSourcePath &&
                       ownerName(function) == type.name &&
                       (shortName(function.name) == "Bind" ||
                        shortName(function.name) == "BindSources" ||
                        shortName(function.name) == "BindJSON");
            });
        if (collides != program.functions.end()) {
            diagnostics.error("FDN2375", "generated binding method already exists for " +
                                               type.name,
                              collides->sourceSpan);
            continue;
        }

        StructBindingPlan binding{index, {}, std::nullopt};
        std::set<std::string> keys;
        std::set<std::string> jsonKeys;
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            if (!field.exported) {
                continue;
            }
            const auto ignored = hasAttribute(program, field.attributes, bindIgnoreAttribute);
            const auto named = findAttribute(program, field.attributes, bindNameAttribute);
            const auto defaulted =
                findAttribute(program, field.attributes, bindDefaultAttribute);
            const auto jsonNamed =
                findAttribute(program, field.attributes, bindJsonNameAttribute);
            const auto jsonBody = hasAttribute(program, field.attributes, bindJsonAttribute);
            const auto hasSources =
                hasAttribute(program, field.attributes, bindFromAttribute);
            if (ignored && (named != nullptr || defaulted != nullptr || jsonNamed != nullptr ||
                            jsonBody || hasSources)) {
                diagnostics.error("FDN2372", "binding field cannot combine @bind.Ignore with "
                                                   "other binding metadata: " + field.name,
                                  type.sourceSpan);
                continue;
            }
            if (ignored) {
                continue;
            }
            if (jsonBody) {
                if (named != nullptr || defaulted != nullptr || jsonNamed != nullptr ||
                    hasSources) {
                    diagnostics.error("FDN2378", "JSON body field cannot combine with other "
                                                       "binding metadata: " + field.name,
                                      type.sourceSpan);
                    continue;
                }
                if (binding.jsonBodyField.has_value()) {
                    diagnostics.error("FDN2378", "binding type has more than one JSON body field",
                                      type.sourceSpan);
                    continue;
                }
                const auto bodyType = baseType(field.type);
                if (bodyType.kind != TypeKind::Struct ||
                    bodyType.declaration >= program.structs.size() ||
                    !hasAttribute(program, program.structs[bodyType.declaration].attributes,
                                  bindableAttribute) ||
                    packageName(program.structs[bodyType.declaration].name) != rootPackage) {
                    diagnostics.error("FDN2379", "JSON body field requires a local concrete "
                                                       "@bind.Bindable struct: " + field.name,
                                      type.sourceSpan);
                    continue;
                }
                binding.jsonBodyField = fieldIndex;
                continue;
            }
            auto key = stringArgument(named).value_or(field.name);
            if (key.empty()) {
                diagnostics.error("FDN2372", "binding field key cannot be empty: " + field.name,
                                  type.sourceSpan);
                continue;
            }
            if (!keys.insert(key).second) {
                diagnostics.error("FDN2373", "duplicate binding field key " + key,
                                  type.sourceSpan);
                continue;
            }
            auto jsonKey = stringArgument(jsonNamed).value_or(field.name);
            if (jsonKey.empty()) {
                diagnostics.error("FDN2380", "JSON binding field key cannot be empty: " +
                                                   field.name,
                                  type.sourceSpan);
                continue;
            }
            if (!jsonKeys.insert(jsonKey).second) {
                diagnostics.error("FDN2381", "duplicate JSON binding field key " + jsonKey,
                                  type.sourceSpan);
                continue;
            }
            const auto kind = structBindingFieldKind(program, field.type);
            if (!kind.has_value()) {
                diagnostics.error("FDN2374",
                                  "unsupported generated binding field type " +
                                      typeName(program, field.type) + " for " + field.name,
                                  type.sourceSpan);
                continue;
            }
            std::vector<std::pair<std::string, std::string>> sources;
            std::set<std::pair<std::string, std::string>> sourceKeys;
            for (const auto &attribute : field.attributes) {
                const auto declaration = attributeDeclaration(program, attribute);
                if (declaration == nullptr || declaration->name != bindFromAttribute) {
                    continue;
                }
                auto source = stringArgument(&attribute, 0).value_or("");
                auto sourceKey = stringArgument(&attribute, 1).value_or("");
                if (source.empty() || sourceKey.empty()) {
                    diagnostics.error("FDN2376", "binding source and key cannot be empty: " +
                                                       field.name,
                                      type.sourceSpan);
                    continue;
                }
                if (!sourceKeys.emplace(source, sourceKey).second) {
                    diagnostics.error("FDN2377", "duplicate binding source " + source + ":" +
                                                       sourceKey + " for " + field.name,
                                      type.sourceSpan);
                    continue;
                }
                sources.emplace_back(std::move(source), std::move(sourceKey));
            }
            binding.fields.push_back({fieldIndex, std::move(key), std::move(jsonKey), *kind,
                                      std::move(sources), stringArgument(defaulted)});
        }
        structBindings.push_back(std::move(binding));
    }
    std::vector<StructValidationPlan> structValidations;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        const auto &type = program.structs[index];
        if (packageName(type.name) != rootPackage || type.sourcePath == generatedSourcePath) {
            continue;
        }
        const auto validatable = hasAttribute(program, type.attributes, validatableAttribute);
        const auto hasValidationMetadata = std::any_of(
            type.fields.begin(), type.fields.end(), [&](const auto &field) {
                return hasAttribute(program, field.attributes, validationRequiredAttribute) ||
                       hasAttribute(program, field.attributes, validationMinAttribute) ||
                       hasAttribute(program, field.attributes, validationMaxAttribute) ||
                       hasAttribute(program, field.attributes, validationEmailAttribute) ||
                       hasAttribute(program, field.attributes, validationPatternAttribute) ||
                       hasAttribute(program, field.attributes, validationNestedAttribute);
            }) ||
            std::any_of(program.functions.begin(), program.functions.end(),
                        [&](const auto &function) {
                            return function.sourcePath != generatedSourcePath &&
                                   ownerName(function) == type.name &&
                                   hasAttribute(program, function.attributes,
                                                validationRuleAttribute);
                        });
        if (!validatable) {
            if (hasValidationMetadata) {
                diagnostics.error(
                    "FDN2385",
                    "validation metadata requires @validation.Validatable on " + type.name,
                    type.sourceSpan);
            }
            continue;
        }
        if (type.typeParameterCount != 0) {
            diagnostics.error("FDN2384", "generated validation requires a concrete struct",
                              type.sourceSpan);
            continue;
        }
        const auto collides = std::find_if(
            program.functions.begin(), program.functions.end(), [&](const auto &function) {
                return function.sourcePath != generatedSourcePath &&
                       ownerName(function) == type.name &&
                       shortName(function.name) == "Validate";
            });
        if (collides != program.functions.end()) {
            diagnostics.error("FDN2386", "generated validation method already exists for " +
                                                   type.name,
                              collides->sourceSpan);
            continue;
        }

        StructValidationPlan validation{index, {}, {}};
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            std::optional<long double> minimum;
            std::optional<long double> maximum;
            for (const auto &attribute : field.attributes) {
                const auto declaration = attributeDeclaration(program, attribute);
                if (declaration == nullptr) {
                    continue;
                }
                if (declaration->name == validationRequiredAttribute) {
                    if (baseType(field.type) != stringType && !isList(program, field.type)) {
                        diagnostics.error(
                            "FDN2387",
                            "@validation.Required requires String or List field: " +
                                field.name,
                            type.sourceSpan);
                        continue;
                    }
                    validation.rules.push_back(
                        {fieldIndex, ValidationRuleKind::Required, std::nullopt, 0});
                    continue;
                }
                if (declaration->name == validationEmailAttribute) {
                    if (baseType(field.type) != stringType) {
                        diagnostics.error("FDN2387",
                                          "@validation.Email requires String field: " +
                                              field.name,
                                          type.sourceSpan);
                        continue;
                    }
                    validation.rules.push_back(
                        {fieldIndex, ValidationRuleKind::Email, std::nullopt, 0});
                    continue;
                }
                if (declaration->name == validationPatternAttribute) {
                    const auto expression = stringArgument(&attribute);
                    if (baseType(field.type) != stringType) {
                        diagnostics.error("FDN2387",
                                          "@validation.Pattern requires String field: " +
                                              field.name,
                                          type.sourceSpan);
                        continue;
                    }
                    if (!expression.has_value() || !validValidationPattern(*expression)) {
                        diagnostics.error(
                            "FDN2391",
                            "validation pattern uses invalid or unsupported syntax: " +
                                field.name,
                            type.sourceSpan);
                        continue;
                    }
                    validation.rules.push_back(
                        {fieldIndex, ValidationRuleKind::Pattern, *expression, 0});
                    continue;
                }
                if (declaration->name == validationNestedAttribute) {
                    const auto nested = baseType(field.type);
                    if (nested.kind != TypeKind::Struct ||
                        nested.declaration >= program.structs.size() ||
                        !hasAttribute(program, program.structs[nested.declaration].attributes,
                                      validatableAttribute)) {
                        diagnostics.error(
                            "FDN2387",
                            "@validation.Nested requires a concrete @validation.Validatable "
                            "struct field: " + field.name,
                            type.sourceSpan);
                        continue;
                    }
                    validation.rules.push_back(
                        {fieldIndex, ValidationRuleKind::Nested, std::nullopt, 0});
                    continue;
                }
                if (declaration->name != validationMinAttribute &&
                    declaration->name != validationMaxAttribute) {
                    continue;
                }
                const auto fieldType = baseType(field.type);
                const auto limit = numericArgument(&attribute);
                if (!isNumeric(fieldType) || !limit.has_value()) {
                    diagnostics.error(
                        "FDN2387",
                        "numeric validation limit requires a numeric field: " + field.name,
                        type.sourceSpan);
                    continue;
                }
                if (isInteger(fieldType) && !validationIntegerFits(fieldType, *limit)) {
                    diagnostics.error(
                        "FDN2387",
                        "validation limit does not fit integer field: " + field.name,
                        type.sourceSpan);
                    continue;
                }
                if (fieldType == f32Type &&
                    std::fabs(limit->value) > std::numeric_limits<float>::max()) {
                    diagnostics.error("FDN2387",
                                      "validation limit does not fit f32 field: " + field.name,
                                      type.sourceSpan);
                    continue;
                }
                const auto kind = declaration->name == validationMinAttribute
                                      ? ValidationRuleKind::Min
                                      : ValidationRuleKind::Max;
                validation.rules.push_back(
                    {fieldIndex, kind, limit->literal, limit->value});
                if (kind == ValidationRuleKind::Min) {
                    minimum = limit->value;
                } else {
                    maximum = limit->value;
                }
            }
            if (minimum.has_value() && maximum.has_value() && *minimum > *maximum) {
                diagnostics.error("FDN2388", "validation minimum exceeds maximum: " +
                                                   field.name,
                                  type.sourceSpan);
            }
        }
        for (FirFunctionId functionIndex = 0; functionIndex < program.functions.size();
             ++functionIndex) {
            const auto &function = program.functions[functionIndex];
            if (function.sourcePath == generatedSourcePath || ownerName(function) != type.name ||
                !hasAttribute(program, function.attributes, validationRuleAttribute)) {
                continue;
            }
            auto validRule = function.receiver == FirReceiverKind::View && !function.generic &&
                             !function.task && !function.blocking && !function.callback &&
                             !function.action && function.returnType == voidType &&
                             function.parameters.size() == 2;
            if (validRule) {
                const auto errorsLocal = function.parameters[1];
                validRule = errorsLocal < function.locals.size() &&
                            function.locals[errorsLocal].type.kind == TypeKind::Edit &&
                            isNamedStruct(program, function.locals[errorsLocal].type,
                                          "foundation.validation.Errors");
            }
            if (!validRule) {
                diagnostics.error(
                    "FDN2392",
                    "@validation.Rule requires fn name(self, &errors "
                    "validation.Errors) void",
                    function.sourceSpan);
                continue;
            }
            validation.customRules.push_back(functionIndex);
        }
        structValidations.push_back(std::move(validation));
    }
    std::map<FirStructId, std::size_t> validationIndexes;
    for (std::size_t index = 0; index < structValidations.size(); ++index) {
        validationIndexes.emplace(structValidations[index].type, index);
    }
    std::vector<unsigned char> validationStates(structValidations.size());
    std::function<void(std::size_t)> visitValidation = [&](const std::size_t index) {
        validationStates[index] = 1;
        const auto &validation = structValidations[index];
        const auto &type = program.structs[validation.type];
        for (const auto &rule : validation.rules) {
            if (rule.kind != ValidationRuleKind::Nested) {
                continue;
            }
            const auto nested = baseType(type.fields[rule.field].type);
            const auto found = validationIndexes.find(nested.declaration);
            if (found == validationIndexes.end()) {
                continue;
            }
            if (validationStates[found->second] == 1) {
                diagnostics.error("FDN2390", "nested validation cycle through " + type.name,
                                  type.sourceSpan);
                continue;
            }
            if (validationStates[found->second] == 0) {
                visitValidation(found->second);
            }
        }
        validationStates[index] = 2;
    };
    for (std::size_t index = 0; index < structValidations.size(); ++index) {
        if (validationStates[index] == 0) {
            visitValidation(index);
        }
    }
    std::vector<StructGuardPlan> structGuards;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        const auto &type = program.structs[index];
        if (packageName(type.name) != rootPackage || type.sourcePath == generatedSourcePath) {
            continue;
        }
        const auto guarded = hasAttribute(program, type.attributes, guardPolicyAttribute);
        const auto hasGuardMetadata =
            hasAttribute(program, type.attributes, guardAllowAttribute) ||
            std::any_of(type.fields.begin(), type.fields.end(), [&](const auto &field) {
                return hasAttribute(program, field.attributes, guardDynamicAttribute);
            });
        if (!guarded) {
            if (hasGuardMetadata) {
                diagnostics.error(
                    "FDN2430",
                    "guard metadata requires @guard.Policy on " + type.name,
                    type.sourceSpan);
            }
            continue;
        }
        if (type.typeParameterCount != 0) {
            diagnostics.error("FDN2431", "generated guard requires a concrete struct",
                              type.sourceSpan);
            continue;
        }
        for (const auto method : {std::string_view{"Can"},
                                  std::string_view{"GetRoles"}}) {
            const auto collides = std::find_if(
                program.functions.begin(), program.functions.end(),
                [&](const auto &function) {
                    return function.sourcePath != generatedSourcePath &&
                           ownerName(function) == type.name &&
                           shortName(function.name) == method;
                });
            if (collides != program.functions.end()) {
                diagnostics.error("FDN2432",
                                  "generated guard method already exists for " + type.name +
                                      ": " + std::string(method),
                                  collides->sourceSpan);
            }
        }

        StructGuardPlan guard{index, {}, {}};
        std::set<std::pair<std::string, std::string>> staticRules;
        for (const auto &attribute : type.attributes) {
            const auto declaration = attributeDeclaration(program, attribute);
            if (declaration == nullptr || declaration->name != guardAllowAttribute) {
                continue;
            }
            const auto role = stringArgument(&attribute, 0).value_or("");
            const auto operation = stringArgument(&attribute, 1).value_or("");
            if (role.empty() || operation.empty()) {
                diagnostics.error("FDN2433", "guard role and operation cannot be empty",
                                  type.sourceSpan);
                continue;
            }
            if (!staticRules.emplace(role, operation).second) {
                diagnostics.error("FDN2434",
                                  "duplicate guard rule " + role + ":" + operation,
                                  type.sourceSpan);
                continue;
            }
            guard.staticRules.push_back({role, operation});
        }

        std::set<std::pair<std::size_t, std::string>> dynamicRules;
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            for (const auto &attribute : field.attributes) {
                const auto declaration = attributeDeclaration(program, attribute);
                if (declaration == nullptr || declaration->name != guardDynamicAttribute) {
                    continue;
                }
                const auto operation = stringArgument(&attribute).value_or("");
                if (operation.empty()) {
                    diagnostics.error("FDN2433", "guard operation cannot be empty: " +
                                                       field.name,
                                      type.sourceSpan);
                    continue;
                }
                GuardDynamicRuleKind kind;
                if (baseType(field.type) == stringType) {
                    kind = GuardDynamicRuleKind::Role;
                } else if (isNamedStruct(program, field.type,
                                         "foundation.guard.Relationships")) {
                    kind = GuardDynamicRuleKind::Relationships;
                } else {
                    diagnostics.error(
                        "FDN2435",
                        "@guard.Dynamic requires String or guard.Relationships field: " +
                            field.name,
                        type.sourceSpan);
                    continue;
                }
                if (!dynamicRules.emplace(fieldIndex, operation).second) {
                    diagnostics.error("FDN2434",
                                      "duplicate dynamic guard rule " + field.name + ":" +
                                          operation,
                                      type.sourceSpan);
                    continue;
                }
                guard.dynamicRules.push_back({fieldIndex, operation, kind});
            }
        }
        if (guard.staticRules.size() + guard.dynamicRules.size() > 65536) {
            diagnostics.error("FDN2436", "guard policy exceeds 65536 rules",
                              type.sourceSpan);
        }
        structGuards.push_back(std::move(guard));
    }
    std::vector<StructSerializerPlan> structSerializers;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        const auto &type = program.structs[index];
        if (packageName(type.name) != rootPackage || type.sourcePath == generatedSourcePath) {
            continue;
        }
        const auto serializable = hasAttribute(program, type.attributes, serializableAttribute);
        const auto hasFieldMetadata = std::any_of(
            type.fields.begin(), type.fields.end(), [&](const auto &field) {
                return hasAttribute(program, field.attributes, serializerNameAttribute) ||
                       hasAttribute(program, field.attributes, serializerIgnoreAttribute);
            });
        const auto hasMethodMetadata = std::any_of(
            program.functions.begin(), program.functions.end(), [&](const auto &function) {
                return function.sourcePath != generatedSourcePath &&
                       ownerName(function) == type.name &&
                       (hasAttribute(program, function.attributes, serializerEncodeAttribute) ||
                        hasAttribute(program, function.attributes, serializerDecodeAttribute));
            });
        if (!serializable) {
            if (hasFieldMetadata || hasMethodMetadata) {
                diagnostics.error(
                    "FDN2410",
                    "serializer metadata requires @serializer.Serializable on " + type.name,
                    type.sourceSpan);
            }
            continue;
        }
        if (type.typeParameterCount != 0) {
            diagnostics.error("FDN2411", "generated serialization requires a concrete struct",
                              type.sourceSpan);
            continue;
        }

        StructSerializerPlan serializer{index, {}, std::nullopt, std::nullopt};
        std::set<std::string> explicitNames;
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            const auto name = stringArgument(
                findAttribute(program, field.attributes, serializerNameAttribute));
            const auto ignored =
                hasAttribute(program, field.attributes, serializerIgnoreAttribute);
            if (name.has_value() && name->empty()) {
                diagnostics.error("FDN2412", "serializer field name cannot be empty: " +
                                                   type.name + "." + field.name,
                                  type.sourceSpan);
            }
            if (name.has_value() && !explicitNames.insert(*name).second) {
                diagnostics.error("FDN2413", "duplicate serializer field name " + *name,
                                  type.sourceSpan);
            }
            if (ignored && name.has_value()) {
                diagnostics.error("FDN2414", "ignored serializer field cannot declare a name: " +
                                                   type.name + "." + field.name,
                                  type.sourceSpan);
            }
            serializer.fields.push_back({fieldIndex, name, ignored});
        }

        for (FirFunctionId functionIndex = 0; functionIndex < program.functions.size();
             ++functionIndex) {
            const auto &function = program.functions[functionIndex];
            if (function.sourcePath == generatedSourcePath || ownerName(function) != type.name) {
                continue;
            }
            const auto encode =
                hasAttribute(program, function.attributes, serializerEncodeAttribute);
            const auto decode =
                hasAttribute(program, function.attributes, serializerDecodeAttribute);
            if (!encode && !decode) {
                continue;
            }
            if (encode && decode) {
                diagnostics.error("FDN2415", "one serializer method cannot encode and decode",
                                  function.sourceSpan);
                continue;
            }
            const auto policyParameter = [&](const std::size_t parameter) {
                if (parameter >= function.parameters.size()) {
                    return false;
                }
                const auto local = function.parameters[parameter];
                return local < function.locals.size() &&
                       isNamedStruct(program, function.locals[local].type,
                                     "foundation.serializer.Policy");
            };
            auto valid = !function.generic && !function.task && !function.blocking &&
                         !function.callback && !function.action;
            if (encode) {
                valid = valid && function.receiver == FirReceiverKind::View &&
                        function.parameters.size() == 2 && policyParameter(1) &&
                        isNamedEnum(program, baseType(function.returnType), "Result") &&
                        function.returnType.arguments.size() == 2 &&
                        isNamedEnum(program, function.returnType.arguments.front(),
                                    "std.json.Value") &&
                        isNamedStruct(program, function.returnType.arguments[1],
                                      "foundation.serializer.Error");
                if (!valid) {
                    diagnostics.error(
                        "FDN2415",
                        "@serializer.Encode requires fn name(self, policy "
                        "serializer.Policy) Result<json.Value, serializer.Error>",
                        function.sourceSpan);
                } else if (serializer.encoder.has_value()) {
                    diagnostics.error("FDN2415", "serializable type has multiple encoders",
                                      function.sourceSpan);
                } else {
                    serializer.encoder = functionIndex;
                }
            } else {
                const Type owner{TypeKind::Struct, index};
                valid = valid && !function.receiver.has_value() && function.method &&
                        function.parameters.size() == 2 && policyParameter(1) &&
                        isNamedEnum(program, function.locals[function.parameters[0]].type,
                                    "std.json.Value") &&
                        parameterMode(function, 0) == ParameterMode::Transfer &&
                        serializerResult(program, function.returnType, owner);
                if (!valid) {
                    diagnostics.error(
                        "FDN2415",
                        "@serializer.Decode requires fn name($value json.Value, policy "
                        "serializer.Policy) Result<Self, serializer.Error>",
                        function.sourceSpan);
                } else if (serializer.decoder.has_value()) {
                    diagnostics.error("FDN2415", "serializable type has multiple decoders",
                                      function.sourceSpan);
                } else {
                    serializer.decoder = functionIndex;
                }
            }
        }
        if (serializer.encoder.has_value() != serializer.decoder.has_value()) {
            diagnostics.error("FDN2416", "custom serializer requires both encode and decode",
                              type.sourceSpan);
        }
        if (serializer.encoder.has_value() && hasFieldMetadata) {
            diagnostics.error("FDN2416", "custom serializer cannot declare field metadata",
                              type.sourceSpan);
        }
        if (!serializer.encoder.has_value()) {
            for (const auto &field : serializer.fields) {
                if (!field.ignored &&
                    !serializerTypeSupported(program, type.fields[field.field].type)) {
                    diagnostics.error(
                        "FDN2417",
                        "unsupported generated serializer field type " +
                            typeName(program, baseType(type.fields[field.field].type)) + " for " +
                            type.name + "." + type.fields[field.field].name,
                        type.sourceSpan);
                }
                const auto &declaration = type.fields[field.field];
                if (field.ignored && !declaration.hasDefault &&
                    !serializerTypeSupported(program, declaration.type)) {
                    diagnostics.error(
                        "FDN2417",
                        "serializer cannot construct ignored field " + type.name + "." +
                            declaration.name + " without a default",
                        type.sourceSpan);
                }
            }
        }
        for (const auto &name : {"Marshal", "MarshalWith", "ToJSON", "Unmarshal",
                                 "UnmarshalWith", "FromJSON"}) {
            const auto collides = std::find_if(
                program.functions.begin(), program.functions.end(), [&](const auto &function) {
                    return function.sourcePath != generatedSourcePath &&
                           ownerName(function) == type.name && shortName(function.name) == name;
                });
            if (collides != program.functions.end()) {
                diagnostics.error("FDN2418", "generated serializer method already exists for " +
                                                   type.name + "." + name,
                                  collides->sourceSpan);
            }
        }
        structSerializers.push_back(std::move(serializer));
    }
    std::map<FirStructId, const StructBindingPlan *> structBindingsByType;
    for (const auto &binding : structBindings) {
        structBindingsByType.emplace(binding.type, &binding);
    }
    std::map<FirStructId, const StructValidationPlan *> structValidationsByType;
    for (const auto &validation : structValidations) {
        structValidationsByType.emplace(validation.type, &validation);
    }
    std::set<FirStructId> typedWebBodyTypes;
    for (const auto &route : webRoutes) {
        const auto &function = program.functions[route.function];
        for (const auto &parameter : route.parameters) {
            if (parameter.source != WebBindingSource::Body) {
                continue;
            }
            const auto local = function.parameters[parameter.index];
            const auto type = baseType(function.locals[local].type);
            if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
                typedWebBodyTypes.insert(type.declaration);
            }
        }
    }
    std::set<FirStructId> checkedWebBodyTypes;
    std::set<FirStructId> activeWebBodyTypes;
    std::function<bool(FirStructId)> validateWebBodyInitializer = [&](const auto typeId) {
        if (checkedWebBodyTypes.contains(typeId)) {
            return true;
        }
        if (!activeWebBodyTypes.insert(typeId).second) {
            diagnostics.error("FDN2383", "recursive typed web body cannot be initialized",
                              program.structs[typeId].sourceSpan);
            return false;
        }
        const auto plan = structBindingsByType.find(typeId);
        if (plan == structBindingsByType.end()) {
            activeWebBodyTypes.erase(typeId);
            return false;
        }
        const auto &type = program.structs[typeId];
        auto valid = true;
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            if (field.hasDefault) {
                continue;
            }
            const auto bound = std::any_of(
                plan->second->fields.begin(), plan->second->fields.end(),
                [&](const auto &candidate) { return candidate.field == fieldIndex; });
            if (bound) {
                continue;
            }
            if (plan->second->jsonBodyField == fieldIndex) {
                const auto nested = baseType(field.type);
                if (nested.kind == TypeKind::Struct &&
                    validateWebBodyInitializer(nested.declaration)) {
                    continue;
                }
            }
            diagnostics.error(
                "FDN2382",
                "typed web body field requires a default or generated binding value: " +
                    type.name + "." + field.name,
                type.sourceSpan);
            valid = false;
        }
        activeWebBodyTypes.erase(typeId);
        if (valid) {
            checkedWebBodyTypes.insert(typeId);
        }
        return valid;
    };
    for (const auto type : typedWebBodyTypes) {
        validateWebBodyInitializer(type);
    }
    if (diagnostics.hasErrors()) {
        return {};
    }

    std::set<FirStructId> buildServices;
    std::function<void(FirStructId)> collectBuildService = [&](const auto service) {
        if (!buildServices.insert(service).second) {
            return;
        }
        for (const auto &dependency : plans.at(service).dependencies) {
            if (dependency.provider.has_value() && plans.contains(*dependency.provider)) {
                collectBuildService(*dependency.provider);
            }
        }
    };
    for (const auto &[service, plan] : plans) {
        if (plan.lifetime == Lifetime::Singleton) {
            collectBuildService(service);
        }
    }

    std::set<FirStructId> scopeServices;
    std::function<void(FirStructId)> collectScopeService = [&](const auto service) {
        if (plans.at(service).lifetime == Lifetime::Singleton ||
            !scopeServices.insert(service).second) {
            return;
        }
        for (const auto &dependency : plans.at(service).dependencies) {
            if (dependency.provider.has_value() && plans.contains(*dependency.provider)) {
                collectScopeService(*dependency.provider);
            }
        }
    };
    for (const auto &[service, plan] : plans) {
        if (plan.lifetime == Lifetime::Scoped) {
            collectScopeService(service);
        }
    }
    const auto hasScope = std::any_of(plans.begin(), plans.end(), [](const auto &entry) {
        return entry.second.lifetime == Lifetime::Scoped;
    });

    std::map<FirStructId, bool> scopeRequirement;
    std::function<bool(FirStructId)> requiresScope = [&](const auto service) {
        if (const auto found = scopeRequirement.find(service);
            found != scopeRequirement.end()) {
            return found->second;
        }
        if (plans.at(service).lifetime == Lifetime::Scoped) {
            scopeRequirement.emplace(service, true);
            return true;
        }
        if (plans.at(service).lifetime == Lifetime::Singleton) {
            scopeRequirement.emplace(service, false);
            return false;
        }
        scopeRequirement.emplace(service, false);
        for (const auto &dependency : plans.at(service).dependencies) {
            if (dependency.provider.has_value() && requiresScope(*dependency.provider)) {
                scopeRequirement[service] = true;
                return true;
            }
        }
        return false;
    };

    std::map<FirFunctionId, std::optional<Type>> activationErrors;
    for (const auto &action : actions) {
        std::optional<Type> errorType;
        std::set<FirStructId> visited;
        std::function<void(FirStructId)> collectActivationError = [&](const auto service) {
            const auto &plan = plans.at(service);
            if (plan.lifetime != Lifetime::Transient || !visited.insert(service).second) {
                return;
            }
            if (plan.fallible) {
                const auto &constructor = program.functions[plan.constructor];
                const auto &candidate = constructor.returnType.arguments[1];
                if (errorType.has_value() && *errorType != candidate) {
                    diagnostics.error(
                        "FDN2340",
                        "fallible constructors in one action activation must use the same error "
                        "type",
                        constructor.sourceSpan);
                } else {
                    errorType = candidate;
                }
            }
            for (const auto &dependency : plan.dependencies) {
                if (dependency.provider.has_value() && plans.contains(*dependency.provider)) {
                    collectActivationError(*dependency.provider);
                }
            }
        };
        collectActivationError(action.service);
        activationErrors.emplace(action.function, std::move(errorType));
    }

    std::optional<Type> startupError;
    std::optional<Type> scopeError;

    for (const auto &[service, plan] : plans) {
        const auto &type = program.structs[service];
        const auto &constructor = program.functions[plan.constructor];
        if (plan.lifetime == Lifetime::Scoped && buildServices.contains(service)) {
            diagnostics.error("FDN2338",
                              "singleton construction cannot depend on a scoped service",
                              constructor.sourceSpan);
        }
        if (plan.fallible && buildServices.contains(service)) {
            const auto &error = constructor.returnType.arguments[1];
            if (startupError.has_value() && *startupError != error) {
                diagnostics.error(
                    "FDN2332",
                    "fallible constructors in one application must use the same error type",
                    constructor.sourceSpan);
            } else {
                startupError = error;
            }
        }
        if (plan.fallible && scopeServices.contains(service)) {
            const auto &error = constructor.returnType.arguments[1];
            if (scopeError.has_value() && *scopeError != error) {
                diagnostics.error(
                    "FDN2339",
                    "fallible constructors in one scope must use the same error type",
                    constructor.sourceSpan);
            } else {
                scopeError = error;
            }
        }
        if (packageName(type.name) != rootPackage && !type.exported) {
            diagnostics.error("FDN2336", "generated host cannot access private service " +
                                               type.name,
                              type.sourceSpan);
        }
        if (constructor.packageName != rootPackage && !constructor.exported) {
            diagnostics.error("FDN2336", "generated host cannot access private constructor " +
                                               constructor.name,
                              constructor.sourceSpan);
        }
        for (const auto &dependency : plan.dependencies) {
            if (dependency.provider.has_value() && dependency.mode == ParameterMode::Transfer &&
                plans.at(*dependency.provider).lifetime != Lifetime::Transient) {
                diagnostics.error(
                    "FDN2333",
                    "generated host cannot transfer stored service " +
                        program.structs[*dependency.provider].name,
                    constructor.sourceSpan);
            }
        }
    }
    std::map<FirFunctionId, std::string> actionMethods;
    std::map<std::string, FirFunctionId> methodNames;
    for (const auto &action : actions) {
        const auto &function = program.functions[action.function];
        const auto source = action.name == function.name ? shortName(function.name)
                                                        : std::string_view(action.name);
        const auto method = hostIdentifier(source, true);
        if (!methodNames.emplace(method, action.function).second) {
            diagnostics.error("FDN2334", "generated action method name collides: " + method,
                              function.sourceSpan);
        }
        if (function.packageName != rootPackage && !function.exported) {
            diagnostics.error("FDN2336", "generated host cannot access private action " +
                                               function.name,
                              function.sourceSpan);
        }
        actionMethods.emplace(action.function, method);
    }
    if (hasScope && methodNames.contains("NewScope")) {
        diagnostics.error("FDN2334", "generated action method name collides: NewScope",
                          program.functions[methodNames.at("NewScope")].sourceSpan);
    }
    for (const auto reserved : {"Dispatch", "DispatchName", "DispatchKey",
                                "DispatchScoped", "DispatchScopedName",
                                "DispatchScopedKey", "HasAction", "ActionNames",
                                "ActionKeyBindings"}) {
        if (methodNames.contains(reserved)) {
            diagnostics.error("FDN2334",
                              "generated action method name collides: " +
                                  std::string(reserved),
                              program.functions[methodNames.at(reserved)].sourceSpan);
        }
    }

    std::set<std::string> dispatchPayloadTypeNames;
    for (const auto &[function, method] : actionMethods) {
        static_cast<void>(function);
        dispatchPayloadTypeNames.insert(rootPackage + ".Foundation" + method + "Action");
    }

    const auto applicationName = rootPackage + ".FoundationApplication";
    const auto scopeName = rootPackage + ".FoundationScope";
    const auto actionTypeName = rootPackage + ".FoundationAction";
    const auto scopedActionTypeName = rootPackage + ".FoundationScopedAction";
    const auto actionResultTypeName = rootPackage + ".FoundationActionResult";
    const auto dispatchErrorTypeName = rootPackage + ".FoundationDispatchError";
    const auto actionKeyBindingTypeName =
        rootPackage + ".FoundationActionKeyBinding";
    const auto builderName = rootPackage + ".BuildFoundationApplication";
    for (const auto &type : program.structs) {
        if (type.name == applicationName && type.sourcePath != generatedSourcePath) {
            diagnostics.error("FDN2335", "generated type FoundationApplication already exists",
                              type.sourceSpan);
        }
        if (hasScope && type.name == scopeName && type.sourcePath != generatedSourcePath) {
            diagnostics.error("FDN2335", "generated type FoundationScope already exists",
                              type.sourceSpan);
        }
        if (!actions.empty() && type.sourcePath != generatedSourcePath &&
            (type.name == actionTypeName || type.name == scopedActionTypeName ||
             type.name == actionResultTypeName || type.name == dispatchErrorTypeName ||
             type.name == actionKeyBindingTypeName ||
             dispatchPayloadTypeNames.contains(type.name))) {
            diagnostics.error("FDN2335", "generated dispatcher type already exists: " +
                                               std::string(shortName(type.name)),
                              type.sourceSpan);
        }
    }
    for (const auto &type : program.enums) {
        if (type.name == applicationName) {
            diagnostics.error("FDN2335", "generated type FoundationApplication already exists",
                              SourceSpan{});
        }
        if (hasScope && type.name == scopeName) {
            diagnostics.error("FDN2335", "generated type FoundationScope already exists",
                              SourceSpan{});
        }
        if (!actions.empty() && generatedSourcePath.empty() &&
            (type.name == actionTypeName || type.name == scopedActionTypeName ||
             type.name == actionResultTypeName || type.name == dispatchErrorTypeName ||
             type.name == actionKeyBindingTypeName ||
             dispatchPayloadTypeNames.contains(type.name))) {
            diagnostics.error("FDN2335", "generated dispatcher type already exists: " +
                                               std::string(shortName(type.name)),
                              SourceSpan{});
        }
    }
    for (const auto &type : program.contracts) {
        if (type.name == applicationName) {
            diagnostics.error("FDN2335", "generated type FoundationApplication already exists",
                              SourceSpan{});
        }
        if (hasScope && type.name == scopeName) {
            diagnostics.error("FDN2335", "generated type FoundationScope already exists",
                              SourceSpan{});
        }
        if (!actions.empty() &&
            (type.name == actionTypeName || type.name == scopedActionTypeName ||
             type.name == actionResultTypeName || type.name == dispatchErrorTypeName ||
             type.name == actionKeyBindingTypeName ||
             dispatchPayloadTypeNames.contains(type.name))) {
            diagnostics.error("FDN2335", "generated dispatcher type already exists: " +
                                               std::string(shortName(type.name)),
                              SourceSpan{});
        }
    }
    for (const auto &function : program.functions) {
        if (function.name == builderName && function.sourcePath != generatedSourcePath) {
            diagnostics.error("FDN2335",
                              "generated function BuildFoundationApplication already exists",
                              function.sourceSpan);
        }
    }
    if (diagnostics.hasErrors()) {
        return {};
    }

    std::set<std::string> packages;
    for (const auto &[service, plan] : plans) {
        const auto &type = program.structs[service];
        const auto &constructor = program.functions[plan.constructor];
        const auto servicePackage = packageName(type.name);
        if (!servicePackage.empty()) {
            packages.insert(servicePackage);
        }
        if (!constructor.packageName.empty()) {
            packages.insert(constructor.packageName);
        }
        for (const auto &dependency : plan.dependencies) {
            collectTypePackages(program, dependency.type, packages);
        }
        if (plan.fallible) {
            collectTypePackages(program, constructor.returnType.arguments[1], packages);
        }
    }
    for (const auto &action : actions) {
        const auto &function = program.functions[action.function];
        if (!function.packageName.empty()) {
            packages.insert(function.packageName);
        }
        collectTypePackages(program, function.returnType, packages);
        for (const auto parameter : function.parameters) {
            collectTypePackages(program, function.locals[parameter].type, packages);
        }
        if (!action.policies.empty()) {
            packages.insert(std::string(packageName(actionPolicyAttribute)));
        }
    }
    if (!webRoutes.empty()) {
        packages.insert("foundation.web");
        packages.insert("std.text");
        auto usesParse = false;
        for (const auto &route : webRoutes) {
            const auto &function = program.functions[route.function];
            if (!function.packageName.empty()) {
                packages.insert(function.packageName);
            }
            collectTypePackages(program, function.returnType, packages);
            for (const auto parameter : function.parameters) {
                collectTypePackages(program, function.locals[parameter].type, packages);
            }
            for (const auto &parameter : route.parameters) {
                if (parameter.source == WebBindingSource::Inject ||
                    parameter.source == WebBindingSource::Body) {
                    continue;
                }
                const auto local = function.parameters[parameter.index];
                const auto type = baseType(function.locals[local].type);
                usesParse = usesParse || type == boolType || isInteger(type);
            }
        }
        for (const auto &middleware : webMiddlewares) {
            const auto &function = program.functions[middleware.function];
            if (!function.packageName.empty()) {
                packages.insert(function.packageName);
            }
            collectTypePackages(program, function.returnType, packages);
            for (const auto parameter : function.parameters) {
                collectTypePackages(program, function.locals[parameter].type, packages);
            }
        }
        if (usesParse) {
            packages.insert("std.parse");
        }
    }
    if (!structBindings.empty()) {
        packages.insert("foundation.bind");
        packages.insert("std.json");
        packages.insert("std.parse");
        for (const auto &binding : structBindings) {
            for (const auto &field : program.structs[binding.type].fields) {
                collectTypePackages(program, field.type, packages);
            }
        }
        const auto usesDuration = std::any_of(
            structBindings.begin(), structBindings.end(), [](const auto &binding) {
                return std::any_of(binding.fields.begin(), binding.fields.end(),
                                   [](const auto &field) {
                                       return field.kind == StructBindingFieldKind::Duration;
                                   });
            });
        if (usesDuration) {
            packages.insert("std.time");
        }
    }
    if (!structValidations.empty()) {
        packages.insert("foundation.validation");
    }
    if (!structGuards.empty()) {
        packages.insert("foundation.guard");
        packages.insert("std.text");
    }
    if (!structSerializers.empty()) {
        packages.insert("foundation.serializer");
        packages.insert("std.json");
        for (const auto &serializer : structSerializers) {
            for (const auto &field : program.structs[serializer.type].fields) {
                collectTypePackages(program, field.type, packages);
            }
        }
    }
    packages.erase(rootPackage);
    packages.erase("");

    std::map<std::string, std::string> aliases;
    std::size_t aliasIndex{};
    for (const auto &package : packages) {
        aliases.emplace(package, "foundationHost" + std::to_string(aliasIndex++));
    }

    std::map<FirStructId, std::string> fields;
    std::set<std::string> usedFields;
    for (const auto service : order) {
        if (plans.at(service).lifetime != Lifetime::Singleton) {
            continue;
        }
        auto field = serviceFieldName(program, service);
        auto candidate = field;
        std::size_t suffix = 2;
        while (!usedFields.insert(candidate).second) {
            candidate = field + std::to_string(suffix++);
        }
        fields.emplace(service, std::move(candidate));
    }
    std::map<FirStructId, std::string> scopeFields;
    std::set<std::string> usedScopeFields;
    for (const auto service : order) {
        if (plans.at(service).lifetime != Lifetime::Scoped) {
            continue;
        }
        auto field = serviceFieldName(program, service);
        auto candidate = field;
        std::size_t suffix = 2;
        while (!usedScopeFields.insert(candidate).second) {
            candidate = field + std::to_string(suffix++);
        }
        scopeFields.emplace(service, std::move(candidate));
    }

    std::map<FirFunctionId, std::string> webMethods;
    std::set<std::string> usedWebMethods;
    for (const auto &route : webRoutes) {
        auto base = hostIdentifier(shortName(program.functions[route.function].name), true);
        auto candidate = base;
        std::size_t suffix = 2;
        while (!usedWebMethods.insert(candidate).second) {
            candidate = base + std::to_string(suffix++);
        }
        webMethods.emplace(route.function, std::move(candidate));
    }

    struct HostInput {
        std::string name;
        Type type{invalidType};
        ParameterMode mode{ParameterMode::Read};
    };
    struct ConstructionContext {
        std::vector<HostInput> inputs;
        std::set<std::string> identifiers;
        std::ostringstream body;
        std::string indent{"    "};
        std::string scopeName{"scope"};
        bool applicationReceiver{};
        bool scopeReceiver{};
    };
    struct DispatchParameter {
        std::string name;
        Type type{invalidType};
        ParameterMode mode{ParameterMode::Read};
    };
    struct DispatchAction {
        FirFunctionId function{};
        std::string method;
        std::string name;
        std::vector<std::string> keys;
        std::vector<std::string> policies;
        std::vector<DispatchParameter> parameters;
        Type successType{voidType};
        std::optional<Type> executionError;
        std::optional<Type> activationError;
        bool scoped{};
        bool scopeEdit{};
        bool receiverEdit{};
    };
    std::vector<DispatchAction> dispatchActions;

    const auto uniqueIdentifier = [](ConstructionContext &context, std::string base) {
        auto candidate = base;
        std::size_t suffix = 2;
        while (!context.identifiers.insert(candidate).second) {
            candidate = base + std::to_string(suffix++);
        }
        return candidate;
    };

    std::function<std::string(FirStructId, ConstructionContext &, std::optional<std::string>,
                              bool)>
        emitConstruction;
    emitConstruction = [&](const auto service, ConstructionContext &context,
                           std::optional<std::string> requestedName,
                           const bool mutableBinding) {
        const auto &plan = plans.at(service);
        const auto &constructor = program.functions[plan.constructor];
        std::vector<std::string> arguments;
        arguments.reserve(plan.dependencies.size());
        for (const auto &dependency : plan.dependencies) {
            std::string value;
            if (dependency.input) {
                value = uniqueIdentifier(context,
                                         inputName(program, service, dependency.parameter));
                context.inputs.push_back({value, dependency.type, dependency.mode});
            } else if (plans.at(*dependency.provider).lifetime == Lifetime::Singleton) {
                value = (context.applicationReceiver ? "self." : "") +
                        fields.at(*dependency.provider);
            } else if (plans.at(*dependency.provider).lifetime == Lifetime::Scoped) {
                value = (context.scopeReceiver ? context.scopeName + "." : "") +
                        scopeFields.at(*dependency.provider);
            } else {
                value = emitConstruction(*dependency.provider, context, std::nullopt, false);
            }
            arguments.push_back(std::string(parameterMarker(dependency.mode)) + value);
        }

        auto local = requestedName.has_value()
                         ? *requestedName
                         : uniqueIdentifier(context, serviceFieldName(program, service));
        context.identifiers.insert(local);
        context.body << context.indent << (mutableBinding ? "var " : "const ") << local << " = "
                     << functionName(constructor, rootPackage, aliases) << '('
                     << join(arguments) << ')';
        if (plan.fallible) {
            context.body << " else error {\n"
                         << context.indent << "    return .Err(error)\n"
                         << context.indent << '}';
        }
        context.body << '\n';
        return local;
    };

    std::ostringstream out;
    out << "package " << rootPackage << "\n\n";
    for (const auto &[package, alias] : aliases) {
        out << "import " << package << " as " << alias << "\n";
    }
    if (!aliases.empty()) {
        out << '\n';
    }
    const auto webApplicationType =
        qualifiedName("foundation.web.Application", rootPackage, aliases);
    const auto webBindingErrorType =
        qualifiedName("foundation.web.RegistrationError", rootPackage, aliases);
    const auto webDispatchErrorType =
        qualifiedName("foundation.web.DispatchError", rootPackage, aliases);
    const auto webRequestType =
        qualifiedName("foundation.web.Request", rootPackage, aliases);
    const auto webResponseType =
        qualifiedName("foundation.web.Response", rootPackage, aliases);
    const auto webRouteMatchType =
        qualifiedName("foundation.web.RouteMatch", rootPackage, aliases);
    const auto webRouteTableType =
        qualifiedName("foundation.web.RouteTable", rootPackage, aliases);
    const auto webTextFunction =
        qualifiedName("foundation.web.Text", rootPackage, aliases);
    const auto textCopyFunction =
        qualifiedName("std.text.Copy", rootPackage, aliases);
    const auto parseBoolFunction =
        qualifiedName("std.parse.Bool", rootPackage, aliases);
    const auto bindValuesType =
        qualifiedName("foundation.bind.Values", rootPackage, aliases);
    const auto bindSourcesType =
        qualifiedName("foundation.bind.Sources", rootPackage, aliases);
    const auto bindErrorType =
        qualifiedName("foundation.bind.Error", rootPackage, aliases);
    const auto bindErrorKindType =
        qualifiedName("foundation.bind.ErrorKind", rootPackage, aliases);
    const auto jsonParseFunction =
        qualifiedName("std.json.Parse", rootPackage, aliases);
    const auto durationType =
        qualifiedName("std.time.Duration", rootPackage, aliases);
    const auto durationZeroFunction =
        qualifiedName("std.time.Zero", rootPackage, aliases);
    const auto collectionsNewListFunction =
        qualifiedName("std.collections.NewList", rootPackage, aliases);
    const auto bindNewValuesFunction =
        qualifiedName("foundation.bind.NewValues", rootPackage, aliases);
    const auto validationErrorsType =
        qualifiedName("foundation.validation.Errors", rootPackage, aliases);
    const auto validationNewErrorsFunction =
        qualifiedName("foundation.validation.NewErrors", rootPackage, aliases);
    const auto validationEmailFunction =
        qualifiedName("foundation.validation.IsEmail", rootPackage, aliases);
    const auto validationPatternFunction =
        qualifiedName("foundation.validation.IsPattern", rootPackage, aliases);
    const auto guardIdentityType =
        qualifiedName("foundation.guard.Identity", rootPackage, aliases);
    const auto guardErrorType =
        qualifiedName("foundation.guard.Error", rootPackage, aliases);
    const auto guardRolesType =
        qualifiedName("foundation.guard.Roles", rootPackage, aliases);
    const auto guardRoleSourceErrorType =
        qualifiedName("foundation.guard.RoleSourceError", rootPackage, aliases);
    const auto guardNewRolesFunction =
        qualifiedName("foundation.guard.NewRoles", rootPackage, aliases);
    const auto guardIsRoleFunction =
        qualifiedName("foundation.guard.IsRole", rootPackage, aliases);
    const auto serializerPolicyType =
        qualifiedName("foundation.serializer.Policy", rootPackage, aliases);
    const auto serializerErrorType =
        qualifiedName("foundation.serializer.Error", rootPackage, aliases);
    const auto serializerDefaultFunction =
        qualifiedName("foundation.serializer.Default", rootPackage, aliases);
    const auto serializerFieldNameFunction =
        qualifiedName("foundation.serializer.FieldName", rootPackage, aliases);
    const auto serializerKeepFunction =
        qualifiedName("foundation.serializer.Keep", rootPackage, aliases);
    const auto serializerAtFunction =
        qualifiedName("foundation.serializer.At", rootPackage, aliases);
    const auto serializerUnknownFunction =
        qualifiedName("foundation.serializer.Unknown", rootPackage, aliases);
    const auto serializerSyntaxFunction =
        qualifiedName("foundation.serializer.Syntax", rootPackage, aliases);
    const auto serializerWriteFunction =
        qualifiedName("foundation.serializer.Write", rootPackage, aliases);
    const auto serializerSetFunction =
        qualifiedName("foundation.serializer.Set", rootPackage, aliases);
    const auto serializerObjectFunction =
        qualifiedName("foundation.serializer.RequireObject", rootPackage, aliases);
    const auto serializerNonNullFunction =
        qualifiedName("foundation.serializer.NonNull", rootPackage, aliases);
    const auto jsonValueType = qualifiedName("std.json.Value", rootPackage, aliases);
    const auto jsonNewObjectFunction =
        qualifiedName("std.json.NewObject", rootPackage, aliases);
    const auto jsonStringifyFunction =
        qualifiedName("std.json.Stringify", rootPackage, aliases);
    const auto usesTypedWebBody = std::any_of(
        webRoutes.begin(), webRoutes.end(), [&](const auto &route) {
            const auto &function = program.functions[route.function];
            return std::any_of(route.parameters.begin(), route.parameters.end(),
                               [&](const auto &parameter) {
                                   if (parameter.source != WebBindingSource::Body) {
                                       return false;
                                   }
                                   const auto local = function.parameters[parameter.index];
                                   return baseType(function.locals[local].type) != stringType;
                               });
        });
    const auto usesWebValidation = std::any_of(
        webRoutes.begin(), webRoutes.end(), [&](const auto &route) {
            const auto &function = program.functions[route.function];
            return std::any_of(route.parameters.begin(), route.parameters.end(),
                               [&](const auto &parameter) {
                                   if (parameter.source != WebBindingSource::Body) {
                                       return false;
                                   }
                                   const auto local = function.parameters[parameter.index];
                                   const auto type = baseType(function.locals[local].type);
                                   return type.kind == TypeKind::Struct &&
                                          structValidationsByType.contains(type.declaration);
                               });
        });
    const auto usesWebBool = std::any_of(
        webRoutes.begin(), webRoutes.end(), [&](const auto &route) {
            const auto &function = program.functions[route.function];
            return std::any_of(route.parameters.begin(), route.parameters.end(),
                               [&](const auto &parameter) {
                                   const auto local = function.parameters[parameter.index];
                                   return parameter.source != WebBindingSource::Inject &&
                                          baseType(function.locals[local].type) == boolType;
                               });
        });

    std::function<std::string(FirStructId)> webBodyInitializer = [&](const auto typeId) {
        const auto &type = program.structs[typeId];
        const auto &binding = *structBindingsByType.at(typeId);
        std::ostringstream value;
        value << sourceTypeName(program, Type{TypeKind::Struct, typeId}, rootPackage, aliases)
              << " {";
        for (std::size_t fieldIndex = 0; fieldIndex < type.fields.size(); ++fieldIndex) {
            const auto &field = type.fields[fieldIndex];
            if (field.hasDefault) {
                continue;
            }
            value << ' ' << field.name << " = ";
            const auto planned = std::find_if(
                binding.fields.begin(), binding.fields.end(),
                [&](const auto &candidate) { return candidate.field == fieldIndex; });
            if (planned != binding.fields.end()) {
                switch (planned->kind) {
                case StructBindingFieldKind::String:
                    value << "\"\"";
                    break;
                case StructBindingFieldKind::Boolean:
                    value << "false";
                    break;
                case StructBindingFieldKind::Integer:
                    value << '0';
                    break;
                case StructBindingFieldKind::F32:
                case StructBindingFieldKind::F64:
                    value << "0.0";
                    break;
                case StructBindingFieldKind::Duration:
                    value << durationZeroFunction << "()";
                    break;
                case StructBindingFieldKind::StringList:
                    value << collectionsNewListFunction << "<String>()";
                    break;
                }
            } else {
                const auto nested = baseType(field.type);
                value << webBodyInitializer(nested.declaration);
            }
        }
        value << " }";
        return value.str();
    };

    const auto serializerHelperName = [](const FirStructId type, const std::size_t field,
                                         std::string_view suffix, const bool decode) {
        return "foundationSerializer" + std::string(decode ? "Decode" : "Encode") +
               std::to_string(type) + "Field" + std::to_string(field) +
               std::string(suffix);
    };
    std::function<std::string(const Type &)> serializerZeroValue = [&](const Type &fieldType) {
        const auto value = baseType(fieldType);
        if (value == stringType) {
            return std::string{"\"\""};
        }
        if (value == boolType) {
            return std::string{"false"};
        }
        if (isInteger(value)) {
            return std::string{"0"};
        }
        if (value == f32Type || value == f64Type) {
            return std::string{"0.0"};
        }
        if (isNamedStruct(program, value, "std.prelude.UUID")) {
            return std::string{"UUID.Nil()"};
        }
        if (isBuiltinOption(program, value)) {
            return std::string{".None"};
        }
        if (value.kind == TypeKind::Struct && value.declaration < program.structs.size()) {
            return sourceTypeName(program, value, rootPackage, aliases) +
                   ".FoundationSerializerZero()";
        }
        return std::string{""};
    };

    std::set<std::string> emittedSerializerHelpers;
    std::function<void(const Type &, const std::string &, bool)> emitSerializerHelper;
    emitSerializerHelper = [&](const Type &fieldType, const std::string &name,
                               const bool decode) {
        if (!emittedSerializerHelpers.insert(name).second) {
            return;
        }
        const auto value = baseType(fieldType);
        const auto sourceType = sourceTypeName(program, value, rootPackage, aliases);
        out << "fn " << name << '(' << (decode ? "$value " + jsonValueType : "value " + sourceType)
            << ", policy " << serializerPolicyType << ", field String) Result<"
            << (decode ? sourceType : jsonValueType) << ", " << serializerErrorType
            << "> {\n";
        if (const auto scalar = serializerScalarName(value); scalar != nullptr) {
            const auto decoder = std::string_view{scalar} == "String"
                                     ? std::string{"Text"}
                                     : std::string{scalar};
            const auto function = qualifiedName(
                std::string("foundation.serializer.") +
                    (decode ? decoder : std::string{scalar} + "Value"),
                rootPackage, aliases);
            if (decode) {
                out << "    " << function << "($value, field)\n";
            } else {
                out << "    .Ok(" << function << "(value))\n";
            }
            out << "}\n\n";
            return;
        }
        if (isNamedStruct(program, value, "std.prelude.UUID")) {
            const auto function = qualifiedName(
                decode ? "foundation.serializer.UUIDValueFromJSON"
                       : "foundation.serializer.UUIDValue",
                rootPackage, aliases);
            if (decode) {
                out << "    " << function << "($value, field)\n";
            } else {
                out << "    .Ok(" << function << "(value))\n";
            }
            out << "}\n\n";
            return;
        }
        if (isBuiltinOption(program, value)) {
            const auto childName = name + "Value";
            if (decode) {
                out << "    const selected = " << serializerNonNullFunction
                    << "($value)\n"
                    << "    match selected {\n"
                    << "        None: .Ok(.None)\n"
                    << "        Some(found): {\n"
                    << "            const decoded = " << childName
                    << "($found, policy, field) else error {\n"
                    << "                return .Err(error)\n"
                    << "            }\n"
                    << "            .Ok(.Some(decoded))\n"
                    << "        }\n"
                    << "    }\n";
            } else {
                out << "    match value {\n"
                    << "        None: .Ok(.Null)\n"
                    << "        Some(found): " << childName
                    << "(found, policy, field)\n"
                    << "    }\n";
            }
            out << "}\n\n";
            emitSerializerHelper(value.arguments.front(), childName, decode);
            return;
        }
        if (value.kind == TypeKind::Struct && value.declaration < program.structs.size()) {
            const auto typeName = sourceTypeName(program, value, rootPackage, aliases);
            if (decode) {
                out << "    const decoded = " << typeName
                    << ".FromJSON($value, policy) else error {\n"
                    << "        return .Err(" << serializerAtFunction
                    << "(field, $error))\n"
                    << "    }\n"
                    << "    .Ok(decoded)\n";
            } else {
                out << "    const encoded = value.ToJSON(policy) else error {\n"
                    << "        return .Err(" << serializerAtFunction
                    << "(field, $error))\n"
                    << "    }\n"
                    << "    .Ok(encoded)\n";
            }
            out << "}\n\n";
            return;
        }
        out << "    panic(\"unsupported compiler serializer type\")\n"
            << "}\n\n";
    };

    for (const auto &serializer : structSerializers) {
        const auto &type = program.structs[serializer.type];
        const auto typeName = qualifiedName(type.name, rootPackage, aliases);
        out << "methods " << typeName << " {\n"
            << "    fn Marshal(self) Result<String, " << serializerErrorType << "> {\n"
            << "        self.MarshalWith(" << serializerDefaultFunction << "())\n"
            << "    }\n\n"
            << "    fn MarshalWith(\n"
            << "        self,\n"
            << "        policy " << serializerPolicyType << "\n"
            << "    ) Result<String, " << serializerErrorType << "> {\n"
            << "        const value = self.ToJSON(policy) else error {\n"
            << "            return .Err(error)\n"
            << "        }\n"
            << "        const encoded = " << jsonStringifyFunction
            << "($value) else error {\n"
            << "            return .Err(" << serializerWriteFunction << "(error))\n"
            << "        }\n"
            << "        .Ok(encoded)\n"
            << "    }\n\n"
            << "    fn Unmarshal(source String) Result<" << typeName << ", "
            << serializerErrorType << "> {\n"
            << "        " << typeName << ".UnmarshalWith(source, "
            << serializerDefaultFunction << "())\n"
            << "    }\n\n"
            << "    fn UnmarshalWith(\n"
            << "        source String,\n"
            << "        policy " << serializerPolicyType << "\n"
            << "    ) Result<" << typeName << ", " << serializerErrorType << "> {\n"
            << "        const value = " << jsonParseFunction << "(source) else error {\n"
            << "            return .Err(" << serializerSyntaxFunction << "(error))\n"
            << "        }\n"
            << "        " << typeName << ".FromJSON($value, policy)\n"
            << "    }\n\n"
            << "    fn FoundationSerializerZero() " << typeName << " {\n"
            << "        " << typeName << " {";
        for (const auto &field : type.fields) {
            if (field.hasDefault) {
                continue;
            }
            out << ' ' << field.name << " = " << serializerZeroValue(field.type);
        }
        out << " }\n"
            << "    }\n\n"
            << "    fn ToJSON(\n"
            << "        self,\n"
            << "        policy " << serializerPolicyType << "\n"
            << "    ) Result<" << jsonValueType << ", " << serializerErrorType << "> {\n";
        if (serializer.encoder.has_value()) {
            out << "        self." << shortName(program.functions[*serializer.encoder].name)
                << "(policy)\n";
        } else {
            out << "        var object = " << jsonNewObjectFunction << "()\n";
            for (const auto &fieldPlan : serializer.fields) {
                if (fieldPlan.ignored) {
                    continue;
                }
                const auto &field = type.fields[fieldPlan.field];
                const auto encodeName = serializerHelperName(serializer.type, fieldPlan.field,
                                                             "", false);
                out << "        const encoded" << fieldPlan.field << " = " << encodeName
                    << "(self." << field.name << ", policy, ";
                emitString(out, field.name);
                out << ") else error {\n"
                    << "            return .Err(error)\n"
                    << "        }\n"
                    << "        const kept" << fieldPlan.field << " = "
                    << serializerKeepFunction << "($encoded" << fieldPlan.field
                    << ", policy.IgnoreNone, policy.IgnoreZero)\n"
                    << "        match kept" << fieldPlan.field << " {\n"
                    << "            None: {}\n"
                    << "            Some(value): {\n"
                    << "                const fieldKey = ";
                if (fieldPlan.name.has_value()) {
                    emitString(out, *fieldPlan.name);
                } else {
                    out << serializerFieldNameFunction << '(';
                    emitString(out, field.name);
                    out << ", policy.Naming)";
                }
                out << "\n"
                    << "                " << serializerSetFunction
                    << "(&object, $fieldKey, $value) else error {\n"
                    << "                    return .Err(error)\n"
                    << "                }\n"
                    << "            }\n"
                    << "        }\n";
            }
            out << "        .Ok(.Object(object))\n";
        }
        out << "    }\n\n"
            << "    fn FromJSON(\n"
            << "        $value " << jsonValueType << ",\n"
            << "        policy " << serializerPolicyType << "\n"
            << "    ) Result<" << typeName << ", " << serializerErrorType << "> {\n";
        if (serializer.decoder.has_value()) {
            out << "        " << typeName << '.'
                << shortName(program.functions[*serializer.decoder].name)
                << "($value, policy)\n";
        } else {
            out << "        const selected = " << serializerObjectFunction
                << "($value, \"\") else error {\n"
                << "            return .Err(error)\n"
                << "        }\n"
                << "        var object = selected\n"
                << "        var result = " << typeName << ".FoundationSerializerZero()\n";
            for (const auto &fieldPlan : serializer.fields) {
                if (fieldPlan.ignored) {
                    continue;
                }
                const auto &field = type.fields[fieldPlan.field];
                const auto decodeName = serializerHelperName(serializer.type, fieldPlan.field,
                                                             "", true);
                out << "        const fieldKey" << fieldPlan.field << " = ";
                if (fieldPlan.name.has_value()) {
                    emitString(out, *fieldPlan.name);
                } else {
                    out << serializerFieldNameFunction << '(';
                    emitString(out, field.name);
                    out << ", policy.Naming)";
                }
                out << "\n"
                    << "        const fieldValue" << fieldPlan.field << " = object.Take(fieldKey"
                    << fieldPlan.field << ")\n"
                    << "        match fieldValue" << fieldPlan.field << " {\n"
                    << "            None: {}\n"
                    << "            Some(found): {\n"
                    << "                const decoded = " << decodeName
                    << "($found, policy, fieldKey" << fieldPlan.field << ") else error {\n"
                    << "                    return .Err(error)\n"
                    << "                }\n"
                    << "                result." << field.name << " = decoded\n"
                    << "            }\n"
                    << "        }\n";
            }
            out << "        if policy.RejectUnknown && object.Len() > 0 {\n"
                << "            const selectedKey = object.FirstKey()\n"
                << "            const unknownKey = match selectedKey {\n"
                << "                None: \"\"\n"
                << "                Some(found): found\n"
                << "            }\n"
                << "            return .Err(" << serializerUnknownFunction
                << "(unknownKey))\n"
                << "        }\n"
                << "        .Ok(result)\n";
        }
        out << "    }\n"
            << "}\n\n";
    }

    for (const auto &serializer : structSerializers) {
        if (serializer.encoder.has_value()) {
            continue;
        }
        const auto &type = program.structs[serializer.type];
        for (const auto &field : serializer.fields) {
            if (field.ignored) {
                continue;
            }
            emitSerializerHelper(type.fields[field.field].type,
                                 serializerHelperName(serializer.type, field.field, "", false),
                                 false);
            emitSerializerHelper(type.fields[field.field].type,
                                 serializerHelperName(serializer.type, field.field, "", true),
                                 true);
        }
    }

    for (const auto &guard : structGuards) {
        const auto &type = program.structs[guard.type];
        const auto guardedTypeName = qualifiedName(type.name, rootPackage, aliases);
        out << "methods " << guardedTypeName << " {\n"
            << "    fn Can(\n"
            << "        self,\n"
            << "        identity " << guardIdentityType << ",\n"
            << "        operation String\n"
            << "    ) Result<void, " << guardErrorType << "> {\n"
            << "        var guardRuleFound = false\n";
        for (const auto &rule : guard.staticRules) {
            out << "        if ";
            if (rule.operation == "*") {
                out << "true";
            } else {
                out << "operation == ";
                emitString(out, rule.operation);
            }
            out << " {\n"
                << "            guardRuleFound = true\n";
            if (rule.role == "*") {
                out << "            return .Ok\n";
            } else {
                out << "            if identity.HasRole(";
                emitString(out, rule.role);
                out << ") return .Ok\n";
            }
            out << "        }\n";
        }
        for (const auto &rule : guard.dynamicRules) {
            const auto &field = type.fields[rule.field];
            out << "        if ";
            if (rule.operation == "*") {
                out << "true";
            } else {
                out << "operation == ";
                emitString(out, rule.operation);
            }
            out << " {\n"
                << "            guardRuleFound = true\n";
            if (rule.kind == GuardDynamicRuleKind::Role) {
                out << "            if " << guardIsRoleFunction << "(self." << field.name
                    << ") && identity.HasRole(self." << field.name << ") return .Ok\n";
            } else {
                out << "            const guardAllowed" << rule.field << " = self."
                    << field.name << ".Allows(identity) else sourceError {\n"
                    << "                return .Err(.RoleSource(" << guardRoleSourceErrorType
                    << " {\n"
                    << "                    Field = ";
                emitString(out, field.name);
                out << "\n"
                    << "                    Error = sourceError\n"
                    << "                }))\n"
                    << "            }\n"
                    << "            if guardAllowed" << rule.field << " return .Ok\n";
            }
            out << "        }\n";
        }
        out << "        if !guardRuleFound return .Err(.NoPolicy(" << textCopyFunction
            << "(operation)))\n"
            << "        .Err(.PermissionDenied(" << textCopyFunction << "(operation)))\n"
            << "    }\n\n"
            << "    fn GetRoles(\n"
            << "        self,\n"
            << "        identity " << guardIdentityType << "\n"
            << "    ) Result<own " << guardRolesType << ", " << guardErrorType << "> {\n"
            << "        var guardRoles = " << guardNewRolesFunction << "()\n";
        std::size_t roleIndex{};
        for (const auto &rule : guard.staticRules) {
            if (rule.role == "*") {
                continue;
            }
            out << "        if identity.HasRole(";
            emitString(out, rule.role);
            out << ") {\n"
                << "            const guardAdded" << roleIndex << " = guardRoles.Add(";
            emitString(out, rule.role);
            out << ") else roleError {\n"
                << "                discard guardRoles\n"
                << "                discard roleError\n"
                << "                return .Err(.TooManyRoles)\n"
                << "            }\n"
                << "            discard guardAdded" << roleIndex << "\n"
                << "        }\n";
            ++roleIndex;
        }
        for (const auto &rule : guard.dynamicRules) {
            const auto &field = type.fields[rule.field];
            if (rule.kind == GuardDynamicRuleKind::Role) {
                out << "        if " << guardIsRoleFunction << "(self." << field.name
                    << ") {\n"
                    << "            const guardAdded" << roleIndex << " = guardRoles.Add(self."
                    << field.name << ") else roleError {\n"
                    << "                discard guardRoles\n"
                    << "                discard roleError\n"
                    << "                return .Err(.TooManyRoles)\n"
                    << "            }\n"
                    << "            discard guardAdded" << roleIndex << "\n"
                    << "        }\n";
                ++roleIndex;
                continue;
            }
            out << "        const guardDerived" << roleIndex << " = self." << field.name
                << ".Roles(identity.ID()) else sourceError {\n"
                << "            discard guardRoles\n"
                << "            return .Err(.RoleSource(" << guardRoleSourceErrorType
                << " {\n"
                << "                Field = ";
            emitString(out, field.name);
            out << "\n"
                << "                Error = sourceError\n"
                << "            }))\n"
                << "        }\n"
                << "        guardRoles.Merge($guardDerived" << roleIndex
                << ") else roleError {\n"
                << "            discard guardRoles\n"
                << "            discard roleError\n"
                << "            return .Err(.TooManyRoles)\n"
                << "        }\n";
            ++roleIndex;
        }
        out << "        .Ok(guardRoles)\n"
            << "    }\n"
            << "}\n\n";
    }

    for (const auto &validation : structValidations) {
        const auto &type = program.structs[validation.type];
        out << "methods " << qualifiedName(type.name, rootPackage, aliases) << " {\n"
            << "    fn Validate(self) own " << validationErrorsType << " {\n"
            << "        var errors = " << validationNewErrorsFunction << "()\n";
        for (const auto &rule : validation.rules) {
            const auto &field = type.fields[rule.field];
            if (rule.kind == ValidationRuleKind::Nested) {
                out << "        errors.AddNested(\n            ";
                emitString(out, field.name);
                out << ",\n            $self." << field.name << ".Validate()\n        )\n";
                continue;
            }
            out << "        if ";
            switch (rule.kind) {
            case ValidationRuleKind::Required:
                out << "!self." << field.name;
                break;
            case ValidationRuleKind::Min:
                out << "self." << field.name << " < " << *rule.argument;
                break;
            case ValidationRuleKind::Max:
                out << "self." << field.name << " > " << *rule.argument;
                break;
            case ValidationRuleKind::Email:
                out << '!' << validationEmailFunction << "(self." << field.name << ')';
                break;
            case ValidationRuleKind::Pattern:
                out << '!' << validationPatternFunction << "(self." << field.name << ", ";
                emitString(out, *rule.argument);
                out << ')';
                break;
            case ValidationRuleKind::Nested:
                break;
            }
            out << " {\n"
                << "            errors.Add(\n"
                << "                .";
            switch (rule.kind) {
            case ValidationRuleKind::Required:
                out << "Required";
                break;
            case ValidationRuleKind::Min:
                out << "Min";
                break;
            case ValidationRuleKind::Max:
                out << "Max";
                break;
            case ValidationRuleKind::Email:
                out << "Email";
                break;
            case ValidationRuleKind::Pattern:
                out << "Pattern";
                break;
            case ValidationRuleKind::Nested:
                break;
            }
            out << ",\n                ";
            emitString(out, field.name);
            out << ",\n                ";
            switch (rule.kind) {
            case ValidationRuleKind::Required:
                emitString(out, "required");
                break;
            case ValidationRuleKind::Min:
                emitString(out, "min " + *rule.argument);
                break;
            case ValidationRuleKind::Max:
                emitString(out, "max " + *rule.argument);
                break;
            case ValidationRuleKind::Email:
                emitString(out, "invalid email");
                break;
            case ValidationRuleKind::Pattern:
                emitString(out, "pattern " + *rule.argument);
                break;
            case ValidationRuleKind::Nested:
                break;
            }
            out << "\n            )\n"
                << "        }\n";
        }
        for (const auto function : validation.customRules) {
            out << "        self." << shortName(program.functions[function].name)
                << "(&errors)\n";
        }
        out << "        errors\n"
            << "    }\n"
            << "}\n\n";
    }

    for (const auto &binding : structBindings) {
        const auto &type = program.structs[binding.type];
        out << "methods " << qualifiedName(type.name, rootPackage, aliases) << " {\n"
            << "    fn Bind(\n"
            << "        &self,\n"
            << "        &source " << bindValuesType << "\n"
            << "    ) Result<void, " << bindErrorType << "> {\n";
        for (std::size_t index = 0; index < binding.fields.size(); ++index) {
            const auto &fieldPlan = binding.fields[index];
            const auto &field = type.fields[fieldPlan.field];
            const auto suffix = std::to_string(index);
            out << "        if source.Contains(";
            emitString(out, fieldPlan.key);
            out << ") {\n"
                << "            const value = source.Required(";
            emitString(out, fieldPlan.key);
            out << ")\n";
            if (fieldPlan.kind == StructBindingFieldKind::String) {
                out << "            self." << field.name << " = value\n";
            } else if (fieldPlan.kind == StructBindingFieldKind::StringList) {
                out << "            "
                    << qualifiedName("foundation.bind.Append", rootPackage, aliases)
                    << "(&self." << field.name << ", $value)\n";
            } else {
                std::string parser;
                if (fieldPlan.kind == StructBindingFieldKind::Duration) {
                    parser = durationType + ".Parse";
                } else {
                    parser = qualifiedName(
                        "std.parse." +
                            std::string(structBindingParser(baseType(field.type))),
                        rootPackage, aliases);
                }
                out << "            const bindingParsed" << suffix << " = " << parser
                    << "(value) else error {\n"
                    << "                const bindingKind" << suffix << ' '
                    << bindErrorKindType << " = match error {\n"
                    << "                    Empty: .Empty\n"
                    << "                    Invalid: .Invalid\n";
                if (fieldPlan.kind == StructBindingFieldKind::Integer) {
                    out << "                    Overflow: .OutOfRange\n";
                } else if (fieldPlan.kind != StructBindingFieldKind::Boolean) {
                    out << "                    OutOfRange: .OutOfRange\n";
                }
                out << "                }\n"
                    << "                return .Err(" << bindErrorType << " {\n"
                    << "                    Kind = bindingKind" << suffix << "\n"
                    << "                    Field = ";
                emitString(out, field.name);
                out << "\n"
                    << "                    Key = ";
                emitString(out, fieldPlan.key);
                out << "\n"
                    << "                    Value = value\n"
                    << "                })\n"
                    << "            }\n"
                    << "            self." << field.name << " = bindingParsed" << suffix
                    << "\n";
            }
            out << "        }\n";
        }
        out << "        .Ok\n"
            << "    }\n";

        const auto hasSources = std::any_of(
            binding.fields.begin(), binding.fields.end(), [](const auto &field) {
                return !field.sources.empty() || field.defaultValue.has_value();
            });
        if (hasSources) {
            out << "\n"
                << "    fn BindSources(\n"
                << "        &self,\n"
                << "        &sources " << bindSourcesType << "\n"
                << "    ) Result<void, " << bindErrorType << "> {\n"
                << "        var bindingValues = " << bindNewValuesFunction << "()\n";
            for (std::size_t index = 0; index < binding.fields.size(); ++index) {
                const auto &field = binding.fields[index];
                if (field.sources.empty() && !field.defaultValue.has_value()) {
                    continue;
                }
                const auto suffix = std::to_string(index);
                out << "        var bindingSourceFound" << suffix << " = false\n";
                for (const auto &[source, key] : field.sources) {
                    out << "        if !bindingSourceFound" << suffix << " {\n"
                        << "            bindingSourceFound" << suffix
                        << " = sources.CopyInto(\n"
                        << "                &bindingValues,\n"
                        << "                ";
                    emitString(out, source);
                    out << ",\n"
                        << "                ";
                    emitString(out, key);
                    out << ",\n"
                        << "                ";
                    emitString(out, field.key);
                    out << "\n"
                        << "            )\n"
                        << "        }\n";
                }
                if (field.defaultValue.has_value()) {
                    out << "        if !bindingSourceFound" << suffix << " {\n"
                        << "            bindingValues.Set(";
                    emitString(out, field.key);
                    out << ", ";
                    emitString(out, *field.defaultValue);
                    out << ")\n"
                        << "        }\n";
                }
            }
            out << "        self.Bind(&bindingValues)\n"
                << "    }\n";
        }
        out << "\n"
            << "    fn BindJSON(\n"
            << "        &self,\n"
            << "        source String\n"
            << "    ) Result<void, " << bindErrorType << "> {\n";
        if (binding.jsonBodyField.has_value()) {
            out << "        self." << type.fields[*binding.jsonBodyField].name
                << ".BindJSON(source)\n";
        } else {
            out << "        const bindingJsonRoot = " << jsonParseFunction
                << "(source) else error {\n"
                << "            return .Err("
                << qualifiedName("foundation.bind.JsonSyntaxError", rootPackage, aliases)
                << "(error))\n"
                << "        }\n"
                << "        const bindingJsonSelected = "
                << qualifiedName("foundation.bind.JsonObject", rootPackage, aliases)
                << "($bindingJsonRoot) else error {\n"
                << "            return .Err(error)\n"
                << "        }\n"
                << "        var bindingJsonObject = bindingJsonSelected\n"
                << "        var bindingJsonValues = " << bindNewValuesFunction << "()\n";
            for (std::size_t index = 0; index < binding.fields.size(); ++index) {
                const auto &fieldPlan = binding.fields[index];
                const auto &field = type.fields[fieldPlan.field];
                std::string helper;
                if (fieldPlan.kind == StructBindingFieldKind::Boolean) {
                    helper = "foundation.bind.CopyJsonBool";
                } else if (fieldPlan.kind == StructBindingFieldKind::Integer ||
                           fieldPlan.kind == StructBindingFieldKind::F32 ||
                           fieldPlan.kind == StructBindingFieldKind::F64) {
                    helper = "foundation.bind.CopyJsonNumber";
                } else if (fieldPlan.kind == StructBindingFieldKind::StringList) {
                    helper = "foundation.bind.CopyJsonTextList";
                } else {
                    helper = "foundation.bind.CopyJsonText";
                }
                out << "        const bindingJsonField" << index << " = "
                    << qualifiedName(helper, rootPackage, aliases) << "(\n"
                    << "            &bindingJsonObject,\n";
                if (fieldPlan.kind == StructBindingFieldKind::StringList) {
                    out << "            &self." << field.name << ",\n";
                } else {
                    out << "            &bindingJsonValues,\n";
                }
                out << "            ";
                emitString(out, fieldPlan.jsonKey);
                out << ",\n";
                if (fieldPlan.kind != StructBindingFieldKind::StringList) {
                    out << "            ";
                    emitString(out, fieldPlan.key);
                    out << ",\n";
                }
                out << "            ";
                emitString(out, field.name);
                out << "\n"
                    << "        ) else error {\n"
                    << "            return .Err(error)\n"
                    << "        }\n"
                    << "        discard bindingJsonField" << index << "\n";
            }
            out << "        if bindingJsonObject.Len() != 0 {\n"
                << "            return .Err("
                << qualifiedName("foundation.bind.JsonUnknownField", rootPackage, aliases)
                << "(&bindingJsonObject))\n"
                << "        }\n"
                << "        self.Bind(&bindingJsonValues)\n";
        }
        out << "    }\n";
        out << "}\n\n";
    }

    if (!includeApplicationHost) {
        out << "// foundation:generated package/v1\n";
        return out.str();
    }

    if (!webRoutes.empty()) {
        out << "enum FoundationWebBindingSource {\n"
            << "    Path\n"
            << "    Query\n"
            << "    Header\n"
            << "    Form\n"
            << "    Body\n"
            << "}\n\n"
            << "enum FoundationWebBindingKind {\n"
            << "    Missing\n"
            << "    Invalid\n"
            << "    UnsupportedMediaType\n"
            << "}\n\n"
            << "struct FoundationWebBindingError {\n"
            << "    Kind FoundationWebBindingKind\n"
            << "    Source FoundationWebBindingSource\n"
            << "    Name String\n"
            << "}\n\n"
            << "enum FoundationWebError {\n"
            << "    Binding(error FoundationWebBindingError)\n";
        if (usesTypedWebBody) {
            out << "    JSON(error " << bindErrorType << ")\n";
        }
        if (usesWebValidation) {
            out << "    Validation(errors own " << validationErrorsType << ")\n";
        }
        for (const auto &route : webRoutes) {
            if (route.activationError.has_value()) {
                out << "    " << webMethods.at(route.function) << "ActivationFailed(error "
                    << sourceTypeName(program, *route.activationError, rootPackage, aliases)
                    << ")\n";
            }
            if (!route.executionError.has_value()) {
                continue;
            }
            out << "    " << webMethods.at(route.function) << "Failed(error "
                << sourceTypeName(program, *route.executionError, rootPackage, aliases)
                << ")\n";
        }
        out << "}\n\n";
    }

    out << "struct FoundationApplication";
    if (!webRoutes.empty()) {
        out << " implements " << webApplicationType << "<FoundationWebError>";
    }
    out << " {\n";
    if (!webRoutes.empty()) {
        out << "    foundationRoutes own " << webRouteTableType << "\n";
    }
    for (const auto service : order) {
        if (plans.at(service).lifetime != Lifetime::Singleton) {
            continue;
        }
        out << "    " << fields.at(service) << ' '
            << sourceTypeName(program, Type{TypeKind::Struct, service}, rootPackage, aliases)
            << "\n";
    }
    out << "}\n\n";
    if (hasScope) {
        out << "struct FoundationScope {\n";
        for (const auto service : order) {
            if (plans.at(service).lifetime != Lifetime::Scoped) {
                continue;
            }
            out << "    " << scopeFields.at(service) << ' '
                << sourceTypeName(program, Type{TypeKind::Struct, service}, rootPackage,
                                  aliases)
                << "\n";
        }
        out << "}\n\n";
    }

    ConstructionContext builder;
    if (!webRoutes.empty()) {
        builder.identifiers.insert("foundationRoutes");
        builder.body << "    var foundationRoutes = "
                     << qualifiedName("foundation.web.NewRouteTable", rootPackage, aliases)
                     << "()\n";
        for (std::size_t index = 0; index < webRoutes.size(); ++index) {
            builder.body << "    foundationRequireWebRoute($foundationRoutes.Add("
                         << (index + 1) << ", ." << webRoutes[index].method << ", ";
            emitString(builder.body, webRoutes[index].path);
            builder.body << "))\n";
        }
    }
    for (const auto &[service, field] : fields) {
        static_cast<void>(service);
        builder.identifiers.insert(field);
    }
    for (const auto service : order) {
        if (plans.at(service).lifetime == Lifetime::Singleton) {
            emitConstruction(service, builder, fields.at(service), false);
        }
    }
    std::vector<std::string> builderParameters;
    builderParameters.reserve(builder.inputs.size());
    for (const auto &input : builder.inputs) {
        builderParameters.push_back(std::string(parameterMarker(input.mode)) + input.name + ' ' +
                                    sourceTypeName(program, baseType(input.type), rootPackage,
                                                   aliases));
    }
    out << "fn BuildFoundationApplication(" << join(builderParameters) << ") ";
    if (startupError.has_value()) {
        out << "Result<";
        if (!webRoutes.empty()) {
            out << "own ";
        }
        out << "FoundationApplication, "
            << sourceTypeName(program, *startupError, rootPackage, aliases) << ">";
    } else {
        if (!webRoutes.empty()) {
            out << "own ";
        }
        out << "FoundationApplication";
    }
    out << " {\n";
    out << builder.body.str();
    out << "    ";
    if (startupError.has_value()) {
        out << ".Ok(";
    }
    if (!webRoutes.empty()) {
        out << "own ";
    }
    out << "FoundationApplication {";
    auto hasApplicationField = false;
    if (!webRoutes.empty()) {
        out << " foundationRoutes = foundationRoutes";
        hasApplicationField = true;
    }
    for (const auto service : order) {
        if (plans.at(service).lifetime != Lifetime::Singleton) {
            continue;
        }
        out << ' ' << fields.at(service) << " = " << fields.at(service);
        hasApplicationField = true;
    }
    out << (hasApplicationField ? " }" : "}");
    if (startupError.has_value()) {
        out << ')';
    }
    out << "\n}\n";

    if (!webRoutes.empty()) {
        out << "\nfn foundationRequireWebRoute(\n"
            << "    $value Result<void, " << webBindingErrorType << ">\n"
            << ") void {\n"
            << "    const accepted = match value {\n"
            << "        Ok: true\n"
            << "        Err(error): foundationWebRouteInvariant($error)\n"
            << "    }\n"
            << "    discard accepted\n"
            << "}\n\n"
            << "fn foundationWebRouteInvariant(\n"
            << "    $error " << webBindingErrorType << "\n"
            << ") never {\n"
            << "    discard error\n"
            << "    panic(\"generated web route invariant failed\")\n"
            << "}\n\n"
            << "fn foundationRequireWebValue(\n"
            << "    $value Option<String>,\n"
            << "    source FoundationWebBindingSource,\n"
            << "    name String\n"
            << ") Result<String, FoundationWebBindingError> {\n"
            << "    match value {\n"
            << "        None: .Err(FoundationWebBindingError {\n"
            << "            Kind = .Missing\n"
            << "            Source = source\n"
            << "            Name = " << textCopyFunction << "(name)\n"
            << "        })\n"
            << "        Some(found): .Ok(found)\n"
            << "    }\n"
            << "}\n";
        if (usesWebBool) {
            out << "\nfn foundationParseWebBool(\n"
                << "    $value String,\n"
                << "    source FoundationWebBindingSource,\n"
                << "    name String\n"
                << ") Result<bool, FoundationWebBindingError> {\n"
                << "    const parsed = " << parseBoolFunction
                << "(value) else error {\n"
                << "        discard error\n"
                << "        discard value\n"
                << "        return .Err(FoundationWebBindingError {\n"
                << "            Kind = .Invalid\n"
                << "            Source = source\n"
                << "            Name = " << textCopyFunction << "(name)\n"
                << "        })\n"
                << "    }\n"
                << "    discard value\n"
                << "    .Ok(parsed)\n"
                << "}\n";
        }
        if (usesTypedWebBody) {
            out << "\nfn foundationRequireWebJSON(\n"
                << "    $value Result<void, " << bindErrorType << ">\n"
                << ") Result<bool, " << bindErrorType << "> {\n"
                << "    match value {\n"
                << "        Ok: .Ok(true)\n"
                << "        Err(error): .Err(error)\n"
                << "    }\n"
                << "}\n\n"
                << "fn foundationWebJSONErrorResponse(\n"
                << "    $error " << bindErrorType << "\n"
                << ") Result<" << webResponseType << ", FoundationWebError> {\n"
                << "    discard error\n"
                << "    .Ok(" << webTextFunction
                << "(400, \"invalid request body\"))\n"
                << "}\n";
        }
        if (usesWebValidation) {
            out << "\nfn foundationWebValidationErrorResponse(\n"
                << "    $errors own " << validationErrorsType << "\n"
                << ") Result<" << webResponseType << ", FoundationWebError> {\n"
                << "    discard errors\n"
                << "    .Ok(" << webTextFunction
                << "(422, \"request validation failed\"))\n"
                << "}\n";
        }
        out << "\nmethods FoundationApplication {\n";
        for (std::size_t routeIndex = 0; routeIndex < webRoutes.size(); ++routeIndex) {
            const auto &route = webRoutes[routeIndex];
            const auto &function = program.functions[route.function];
            const auto method = webMethods.at(route.function);
            out << "    fn foundationDispatchWeb" << method << "(\n"
                << "        &self,\n"
                << "        $foundationRequest " << webRequestType << "\n"
                << "    ) Result<" << webResponseType << ", " << webDispatchErrorType
                << "<FoundationWebError>> {\n"
                << "        var foundationActiveRequest = foundationRequest\n";
            ConstructionContext webActivation;
            webActivation.applicationReceiver = true;
            webActivation.indent = "        ";
            webActivation.identifiers.insert("foundationActiveRequest");
            for (std::size_t parameterIndex = 0;
                 parameterIndex < route.parameters.size(); ++parameterIndex) {
                webActivation.identifiers.insert(
                    "foundationWebParameter" + std::to_string(parameterIndex));
            }
            std::map<FirStructId, std::string> scopedWebServices;
            std::function<std::string(FirStructId)> emitWebActivation;
            emitWebActivation = [&](const auto service) {
                const auto &plan = plans.at(service);
                if (plan.lifetime == Lifetime::Singleton) {
                    return "self." + fields.at(service);
                }
                if (plan.lifetime == Lifetime::Scoped) {
                    if (const auto found = scopedWebServices.find(service);
                        found != scopedWebServices.end()) {
                        return found->second;
                    }
                }

                const auto &constructor = program.functions[plan.constructor];
                std::vector<std::string> constructorArguments;
                constructorArguments.reserve(plan.dependencies.size());
                for (const auto &dependency : plan.dependencies) {
                    if (dependency.input || !dependency.provider.has_value()) {
                        continue;
                    }
                    const auto value = emitWebActivation(*dependency.provider);
                    constructorArguments.push_back(
                        std::string(parameterMarker(dependency.mode)) + value);
                }

                const auto local = uniqueIdentifier(
                    webActivation, serviceFieldName(program, service));
                webActivation.body << webActivation.indent << "const " << local << " = "
                                   << functionName(constructor, rootPackage, aliases) << '('
                                   << join(constructorArguments) << ')';
                if (plan.fallible) {
                    webActivation.body
                        << " else error {\n"
                        << webActivation.indent
                        << "    return .Err(.Handler(error = ." << method
                        << "ActivationFailed(error = error)))\n"
                        << webActivation.indent << '}';
                }
                webActivation.body << '\n';
                if (plan.lifetime == Lifetime::Scoped) {
                    scopedWebServices.emplace(service, local);
                }
                return local;
            };
            std::vector<std::string> arguments;
            arguments.reserve(route.parameters.size());
            for (std::size_t parameterIndex = 0;
                 parameterIndex < route.parameters.size(); ++parameterIndex) {
                const auto &parameter = route.parameters[parameterIndex];
                const auto local = "foundationWebParameter" +
                                   std::to_string(parameterIndex);
                if (parameter.source == WebBindingSource::Inject) {
                    arguments.push_back(
                        std::string(parameterMarker(
                            parameterMode(function, parameter.index))) +
                        emitWebActivation(*parameter.provider));
                    continue;
                }
                const auto parameterLocal = function.parameters[parameter.index];
                const auto parameterType = baseType(function.locals[parameterLocal].type);
                arguments.push_back(std::string(parameterMarker(
                                        parameterMode(function, parameter.index))) +
                                    local);
                if (parameter.source == WebBindingSource::Body) {
                    if (parameterType == stringType) {
                        out << "        const " << local
                            << " = " << textCopyFunction
                            << "(foundationActiveRequest.Body)\n";
                    } else {
                        out << "        if !foundationActiveRequest.IsJSON() {\n"
                            << "            return .Err(.Handler(error = .Binding(error = "
                            << "FoundationWebBindingError { Kind = .UnsupportedMediaType "
                            << "Source = .Body Name = \"Content-Type\" })))\n"
                            << "        }\n"
                            << "        var " << local << " = "
                            << webBodyInitializer(parameterType.declaration) << "\n"
                            << "        const " << local
                            << "Bound = foundationRequireWebJSON($" << local
                            << ".BindJSON(foundationActiveRequest.Body)) else error {\n"
                            << "            return .Err(.Handler(error = .JSON(error = "
                            << "error)))\n"
                            << "        }\n"
                            << "        discard " << local << "Bound\n";
                        if (structValidationsByType.contains(parameterType.declaration)) {
                            out << "        var " << local << "ValidationErrors = "
                                << local << ".Validate()\n"
                                << "        if !" << local << "ValidationErrors.IsEmpty() {\n"
                                << "            return .Err(.Handler(error = .Validation("
                                << "errors = $" << local << "ValidationErrors)))\n"
                                << "        }\n"
                                << "        discard " << local << "ValidationErrors\n";
                        }
                    }
                    continue;
                }
                const auto accessor = parameter.source == WebBindingSource::Path
                                          ? "Param"
                                      : parameter.source == WebBindingSource::Query
                                          ? "Query"
                                      : parameter.source == WebBindingSource::Header
                                          ? "Header"
                                          : "Form";
                if (isBuiltinOption(program, parameterType)) {
                    out << "        const " << local << " = foundationActiveRequest."
                        << accessor << '(';
                    emitString(out, parameter.name);
                    out << ")\n";
                    continue;
                }
                const auto textLocal = parameterType == stringType ? local : local + "Text";
                out << "        const " << textLocal
                    << " = foundationRequireWebValue($foundationActiveRequest." << accessor
                    << '(';
                emitString(out, parameter.name);
                out << "), ." << webBindingSourceName(parameter.source) << ", ";
                emitString(out, parameter.name);
                out << ") else error {\n"
                    << "            return .Err(.Handler(error = .Binding(error = error)))\n"
                    << "        }\n";
                if (parameterType == stringType) {
                    continue;
                }
                if (parameterType == boolType) {
                    out << "        const " << local << " = foundationParseWebBool($"
                        << textLocal << ", ." << webBindingSourceName(parameter.source)
                        << ", ";
                    emitString(out, parameter.name);
                    out << ") else error {\n"
                        << "            return .Err(.Handler(error = .Binding(error = error)))\n"
                        << "        }\n";
                    continue;
                }
                const auto parser = webIntegerParser(parameterType);
                const auto parserName = qualifiedName(
                    std::string("std.parse.") + parser, rootPackage, aliases);
                out << "        const " << local << " = " << parserName << '(' << textLocal
                    << ") else error {\n"
                    << "            discard error\n"
                    << "            return .Err(.Handler(error = .Binding(error = "
                    << "FoundationWebBindingError { Kind = .Invalid Source = ."
                    << webBindingSourceName(parameter.source) << " Name = ";
                emitString(out, parameter.name);
                out << " })))\n"
                    << "        }\n"
                    << "        discard " << textLocal << "\n";
            }
            out << "        discard foundationActiveRequest\n"
                << webActivation.body.str();
            const auto invocation = functionName(function, rootPackage, aliases) + '(' +
                                    join(arguments) + ')';
            auto completed = invocation;
            if (route.task) {
                out << "        const foundationWebHandler = spawn " << invocation << "\n";
                completed = "$foundationWebHandler.wait()";
            }
            if (route.executionError.has_value()) {
                out << "        match " << completed << " {\n"
                    << "            Ok(response): .Ok(response)\n"
                    << "            Err(error): .Err(.Handler(error = ." << method
                    << "Failed(error = error)))\n"
                    << "        }\n";
            } else {
                out << "        .Ok(" << completed << ")\n";
            }
            out << "    }\n\n";
        }

        const auto emitMiddlewareMethod = [&](std::string_view methodName,
                                              const WebMiddlewarePlan &middleware,
                                              std::string_view nextMethod) {
            const auto &function = program.functions[middleware.function];
            out << "    fn " << methodName << "(\n"
                << "        &self,\n"
                << "        $foundationRequest " << webRequestType << "\n"
                << "    ) Result<" << webResponseType << ", " << webDispatchErrorType
                << "<FoundationWebError>> {\n"
                << "        " << functionName(function, rootPackage, aliases)
                << "<FoundationWebError>(\n"
                << "            $foundationRequest,\n"
                << "            fn(\n"
                << "                $foundationNextRequest " << webRequestType << "\n"
                << "            ) Result<" << webResponseType << ", "
                << webDispatchErrorType << "<FoundationWebError>> capture(&self) {\n"
                << "                self." << nextMethod
                << "($foundationNextRequest)\n"
                << "            }\n"
                << "        )\n"
                << "    }\n\n";
        };

        for (const auto &route : webRoutes) {
            std::vector<const WebMiddlewarePlan *> selected;
            for (const auto &middleware : webMiddlewares) {
                if ((middleware.scope == WebMiddlewareScope::Group &&
                     webRouteInGroup(route.path, middleware.path)) ||
                    (middleware.scope == WebMiddlewareScope::Route &&
                     middleware.method == route.method && middleware.path == route.path)) {
                    selected.push_back(&middleware);
                }
            }
            const auto method = webMethods.at(route.function);
            for (std::size_t index = selected.size(); index != 0; --index) {
                const auto position = index - 1;
                const auto name = "foundationDispatchWeb" + method + "Middleware" +
                                  std::to_string(position);
                const auto next = position + 1 == selected.size()
                                      ? "foundationDispatchWeb" + method
                                      : "foundationDispatchWeb" + method + "Middleware" +
                                            std::to_string(position + 1);
                emitMiddlewareMethod(name, *selected[position], next);
            }
        }

        std::vector<const WebMiddlewarePlan *> globalMiddlewares;
        for (const auto &middleware : webMiddlewares) {
            if (middleware.scope == WebMiddlewareScope::Global) {
                globalMiddlewares.push_back(&middleware);
            }
        }
        for (std::size_t index = globalMiddlewares.size(); index != 0; --index) {
            const auto position = index - 1;
            const auto name = "foundationDispatchWebMiddleware" +
                              std::to_string(position);
            const auto next = position + 1 == globalMiddlewares.size()
                                  ? "foundationDispatchWebRoutes"
                                  : "foundationDispatchWebMiddleware" +
                                        std::to_string(position + 1);
            emitMiddlewareMethod(name, *globalMiddlewares[position], next);
        }

        out << "    fn "
            << (webMiddlewares.empty() ? "Dispatch" : "foundationDispatchWebRoutes")
            << "(\n"
            << "        &self,\n"
            << "        $foundationRequest " << webRequestType << "\n"
            << "    ) Result<" << webResponseType << ", " << webDispatchErrorType
            << "<FoundationWebError>> {\n"
            << "        var foundationActiveRequest = foundationRequest\n"
            << "        const foundationMatched = self.foundationRoutes.Match(\n"
            << "            foundationActiveRequest.Method,\n"
            << "            foundationActiveRequest.Path\n"
            << "        ) else error {\n"
            << "            return match error {\n"
            << "                NotFound: .Err(.NotFound)\n"
            << "                MethodNotAllowed: .Err(.MethodNotAllowed)\n"
            << "            }\n"
            << "        }\n"
            << "        const " << webRouteMatchType
            << " { Id Params } = foundationMatched\n"
            << "        const foundationPreviousParams = replace "
            << "foundationActiveRequest.Params with Params\n"
            << "        discard foundationPreviousParams\n";
        for (std::size_t routeIndex = 0; routeIndex < webRoutes.size(); ++routeIndex) {
            const auto &route = webRoutes[routeIndex];
            const auto method = webMethods.at(route.function);
            const auto hasScopedMiddleware = std::any_of(
                webMiddlewares.begin(), webMiddlewares.end(), [&](const auto &middleware) {
                    return (middleware.scope == WebMiddlewareScope::Group &&
                            webRouteInGroup(route.path, middleware.path)) ||
                           (middleware.scope == WebMiddlewareScope::Route &&
                            middleware.method == route.method &&
                            middleware.path == route.path);
                });
            out << "        if Id == " << (routeIndex + 1)
                << " return self.foundationDispatchWeb" << method;
            if (hasScopedMiddleware) {
                out << "Middleware0";
            }
            out << "($foundationActiveRequest)\n";
        }
        out << "        discard foundationActiveRequest\n"
            << "        .Err(.NotFound)\n"
            << "    }\n\n";
        if (!webMiddlewares.empty()) {
            out << "    fn Dispatch(\n"
                << "        &self,\n"
                << "        $foundationRequest " << webRequestType << "\n"
                << "    ) Result<" << webResponseType << ", " << webDispatchErrorType
                << "<FoundationWebError>> {\n"
                << "        self.";
            if (globalMiddlewares.empty()) {
                out << "foundationDispatchWebRoutes";
            } else {
                out << "foundationDispatchWebMiddleware0";
            }
            out << "($foundationRequest)\n"
                << "    }\n\n";
        }
        out << "    fn ErrorResponse(\n"
            << "        self,\n"
            << "        $error FoundationWebError\n"
            << "    ) Result<" << webResponseType << ", FoundationWebError> {\n"
            << "        match error {\n"
            << "            Binding(error): match error.Kind {\n"
            << "                Missing: .Ok(" << webTextFunction
            << "(400, \"missing request value\"))\n"
            << "                Invalid: .Ok(" << webTextFunction
            << "(400, \"invalid request value\"))\n"
            << "                UnsupportedMediaType: .Ok(" << webTextFunction
            << "(415, \"unsupported media type\"))\n"
            << "            }\n";
        if (usesTypedWebBody) {
            out << "            JSON(error): foundationWebJSONErrorResponse($error)\n";
        }
        if (usesWebValidation) {
            out << "            Validation(errors): "
                << "foundationWebValidationErrorResponse($errors)\n";
        }
        for (const auto &route : webRoutes) {
            if (route.activationError.has_value()) {
                const auto method = webMethods.at(route.function);
                out << "            " << method << "ActivationFailed(error): .Err(."
                    << method << "ActivationFailed(error = error))\n";
            }
            if (!route.executionError.has_value()) {
                continue;
            }
            const auto method = webMethods.at(route.function);
            out << "            " << method << "Failed(error): .Err(." << method
                << "Failed(error = error))\n";
        }
        out << "        }\n"
            << "    }\n"
            << "}\n";
    }

    ConstructionContext scopeBuilder;
    if (hasScope) {
        scopeBuilder.applicationReceiver = true;
        scopeBuilder.indent = "        ";
        for (const auto &[service, field] : scopeFields) {
            static_cast<void>(service);
            scopeBuilder.identifiers.insert(field);
        }
        for (const auto service : order) {
            if (plans.at(service).lifetime == Lifetime::Scoped) {
                emitConstruction(service, scopeBuilder, scopeFields.at(service), false);
            }
        }
    }

    if (hasScope || !actions.empty()) {
        out << "\nmethods FoundationApplication {\n";
        if (hasScope) {
            std::vector<std::string> parameters{"self"};
            for (const auto &input : scopeBuilder.inputs) {
                parameters.push_back(std::string(parameterMarker(input.mode)) + input.name + ' ' +
                                     sourceTypeName(program, baseType(input.type), rootPackage,
                                                    aliases));
            }
            out << "    fn NewScope(" << join(parameters) << ") ";
            if (scopeError.has_value()) {
                out << "Result<FoundationScope, "
                    << sourceTypeName(program, *scopeError, rootPackage, aliases) << ">";
            } else {
                out << "FoundationScope";
            }
            out << " {\n" << scopeBuilder.body.str() << "        ";
            if (scopeError.has_value()) {
                out << ".Ok(";
            }
            out << "FoundationScope {";
            for (const auto service : order) {
                if (plans.at(service).lifetime == Lifetime::Scoped) {
                    out << ' ' << scopeFields.at(service) << " = " << scopeFields.at(service);
                }
            }
            out << " }";
            if (scopeError.has_value()) {
                out << ')';
            }
            out << "\n    }\n";
            if (!actions.empty()) {
                out << '\n';
            }
        }
        for (std::size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex) {
            const auto &action = actions[actionIndex];
            const auto &function = program.functions[action.function];
            const auto lifetime = plans.at(action.service).lifetime;
            const auto transient = lifetime == Lifetime::Transient;
            const auto scoped = lifetime == Lifetime::Scoped;
            const auto scopedAction = requiresScope(action.service);
            const auto receiver = lifetime == Lifetime::Singleton &&
                                          *function.receiver == FirReceiverKind::Edit
                                      ? "&self"
                                      : "self";
            std::vector<std::string> parameters{receiver};
            std::vector<std::string> arguments;
            std::vector<DispatchParameter> dispatchParameters;
            const auto first = function.receiver.has_value() ? 1U : 0U;
            ConstructionContext activation;
            activation.applicationReceiver = true;
            activation.indent = "        ";
            activation.identifiers.insert("self");
            for (std::size_t index = first; index < function.parameters.size(); ++index) {
                const auto local = function.parameters[index];
                const auto &value = function.locals[local];
                activation.identifiers.insert(value.name);
            }
            std::string scopeParameter;
            if (scopedAction) {
                scopeParameter = uniqueIdentifier(activation, "foundationScope");
                activation.scopeReceiver = true;
                activation.scopeName = scopeParameter;
                parameters.push_back(
                    std::string(scoped && *function.receiver == FirReceiverKind::Edit ? "&" : "") +
                    scopeParameter + " FoundationScope");
            }
            std::string target;
            if (transient) {
                target = emitConstruction(action.service, activation, std::nullopt,
                                          *function.receiver == FirReceiverKind::Edit);
            } else if (scoped) {
                target = scopeParameter + '.' + scopeFields.at(action.service);
            } else {
                target = "self." + fields.at(action.service);
            }
            for (const auto &input : activation.inputs) {
                parameters.push_back(std::string(parameterMarker(input.mode)) + input.name + ' ' +
                                     sourceTypeName(program, baseType(input.type), rootPackage,
                                                    aliases));
                dispatchParameters.push_back({input.name, input.type, input.mode});
            }
            for (std::size_t index = first; index < function.parameters.size(); ++index) {
                const auto local = function.parameters[index];
                const auto &value = function.locals[local];
                const auto mode = parameterMode(function, index);
                parameters.push_back(std::string(parameterMarker(mode)) + value.name + ' ' +
                                     sourceTypeName(program, baseType(value.type), rootPackage,
                                                    aliases));
                arguments.push_back(std::string(parameterMarker(mode)) + value.name);
                dispatchParameters.push_back({value.name, value.type, mode});
            }
            const auto &activationError = activationErrors.at(action.function);
            const auto resultAlreadyMatches =
                activationError.has_value() &&
                isResultWithError(program, function.returnType, *activationError);
            const auto wrapsResult = activationError.has_value() && !resultAlreadyMatches;
            out << "    fn " << actionMethods.at(action.function) << '(' << join(parameters)
                << ") ";
            if (wrapsResult) {
                out << "Result<"
                    << sourceTypeName(program, function.returnType, rootPackage, aliases) << ", "
                    << sourceTypeName(program, *activationError, rootPackage, aliases) << ">";
            } else {
                out << sourceTypeName(program, function.returnType, rootPackage, aliases);
            }
            out << " {\n" << activation.body.str() << "        ";
            std::string invocation;
            if (transient && *function.receiver == FirReceiverKind::Own) {
                invocation += '$';
            }
            invocation += target + '.' + std::string(shortName(function.name)) + '(' +
                          join(arguments) + ')';
            if (wrapsResult && function.returnType == voidType) {
                out << invocation << "\n        .Ok";
            } else if (wrapsResult && function.returnType != neverType) {
                out << ".Ok(" << invocation << ')';
            } else {
                out << invocation;
            }
            out << "\n    }\n";
            const auto actionReturnsResult = isResultType(program, function.returnType);
            const auto successType =
                actionReturnsResult ? function.returnType.arguments[0] : function.returnType;
            const auto executionError = actionReturnsResult
                                            ? std::optional<Type>{function.returnType.arguments[1]}
                                            : std::nullopt;
            dispatchActions.push_back(
                {action.function,
                 actionMethods.at(action.function),
                 action.name,
                 action.keys,
                 action.policies,
                 std::move(dispatchParameters),
                 successType,
                 executionError,
                 activationError,
                 scopedAction,
                 scoped && *function.receiver == FirReceiverKind::Edit,
                 lifetime == Lifetime::Singleton &&
                     *function.receiver == FirReceiverKind::Edit});
            if (actionIndex + 1 != actions.size()) {
                out << '\n';
            }
        }
        out << "}\n";
    }

    if (!dispatchActions.empty()) {
        const auto parameterStorageType = [&](const DispatchParameter &parameter) {
            const auto type = baseType(parameter.type);
            if (type.kind == TypeKind::Contract &&
                parameter.mode == ParameterMode::Transfer) {
                return "own " + sourceTypeName(program, type, rootPackage, aliases);
            }
            return sourceTypeName(program, type, rootPackage, aliases);
        };
        for (const auto &action : dispatchActions) {
            for (const auto &parameter : action.parameters) {
                const auto type = baseType(parameter.type);
                if (type.kind == TypeKind::Slice ||
                    (type.kind == TypeKind::Contract &&
                     parameter.mode != ParameterMode::Transfer)) {
                    diagnostics.error(
                        "FDN2341",
                        "dispatched action payload cannot store borrowed parameter " +
                            parameter.name,
                        program.functions[action.function].sourceSpan);
                }
            }
            if (action.successType.kind == TypeKind::View ||
                action.successType.kind == TypeKind::Edit ||
                action.successType.kind == TypeKind::Slice ||
                action.successType.kind == TypeKind::Contract ||
                (action.successType == neverType && action.executionError.has_value())) {
                diagnostics.error("FDN2342",
                                  "dispatched action result must be an owned or value type",
                                  program.functions[action.function].sourceSpan);
            }
        }
        if (diagnostics.hasErrors()) {
            return {};
        }

        std::vector<const DispatchAction *> globalActions;
        std::vector<const DispatchAction *> scopedActions;
        for (const auto &action : dispatchActions) {
            (action.scoped ? scopedActions : globalActions).push_back(&action);
        }

        const auto payloadType = [](const DispatchAction &action) {
            return "Foundation" + action.method + "Action";
        };
        for (const auto &action : dispatchActions) {
            if (action.parameters.empty()) {
                continue;
            }
            out << "\nstruct " << payloadType(action) << " {\n";
            for (const auto &parameter : action.parameters) {
                out << "    " << parameter.name << ' ' << parameterStorageType(parameter)
                    << "\n";
            }
            out << "}\n";
        }

        const auto emitRequestType = [&](std::string_view name,
                                         const std::vector<const DispatchAction *> &group) {
            if (group.empty()) {
                return;
            }
            out << "\nenum " << name << " {\n";
            for (const auto action : group) {
                out << "    " << action->method;
                if (!action->parameters.empty()) {
                    out << '(' << payloadType(*action) << ')';
                }
                out << "\n";
            }
            out << "}\n";
        };
        emitRequestType("FoundationAction", globalActions);
        emitRequestType("FoundationScopedAction", scopedActions);

        out << "\nstruct FoundationActionKeyBinding {\n"
            << "    key String\n"
            << "    actionName String\n"
            << "}\n";

        out << "\nenum FoundationActionResult {\n";
        for (const auto &action : dispatchActions) {
            out << "    " << action.method;
            if (action.successType != voidType && action.successType != neverType) {
                out << "(value "
                    << sourceTypeName(program, action.successType, rootPackage, aliases) << ')';
            }
            out << "\n";
        }
        out << "}\n\n"
            << "enum FoundationDispatchError {\n"
            << "    UnknownName\n"
            << "    UnknownKey\n"
            << "    ActionMismatch\n";
        const auto hasPolicies = std::any_of(
            dispatchActions.begin(), dispatchActions.end(),
            [](const auto &action) { return !action.policies.empty(); });
        if (hasPolicies) {
            out << "    Denied(policy String)\n";
        }
        for (const auto &action : dispatchActions) {
            const auto sameError = action.activationError.has_value() &&
                                   action.executionError.has_value() &&
                                   *action.activationError == *action.executionError;
            if (action.activationError.has_value() && !sameError) {
                out << "    " << action.method << "Activation(error "
                    << sourceTypeName(program, *action.activationError, rootPackage, aliases)
                    << ")\n";
            }
            if (action.executionError.has_value()) {
                out << "    " << action.method << "Failed(error "
                    << sourceTypeName(program, *action.executionError, rootPackage, aliases)
                    << ")\n";
            }
        }
        out << "}\n";

        auto actionsByName = dispatchActions;
        std::sort(actionsByName.begin(), actionsByName.end(),
                  [](const auto &left, const auto &right) {
                      return left.name < right.name;
                  });
        std::vector<std::pair<std::string, std::string>> actionKeyBindings;
        for (const auto &action : dispatchActions) {
            for (const auto &key : action.keys) {
                actionKeyBindings.emplace_back(key, action.name);
            }
        }
        std::sort(actionKeyBindings.begin(), actionKeyBindings.end());

        out << "\nmethods FoundationApplication {\n"
            << "    fn HasAction(self, name String) bool {\n        ";
        for (std::size_t index = 0; index < actionsByName.size(); ++index) {
            if (index != 0) {
                out << " || ";
            }
            out << "name == ";
            emitString(out, actionsByName[index].name);
        }
        out << "\n    }\n\n"
            << "    fn ActionNames(self) [" << actionsByName.size()
            << "]String {\n        [";
        for (std::size_t index = 0; index < actionsByName.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            emitString(out, actionsByName[index].name);
        }
        out << "]\n    }\n\n"
            << "    fn ActionKeyBindings(self) [" << actionKeyBindings.size()
            << "]FoundationActionKeyBinding {\n        [";
        for (std::size_t index = 0; index < actionKeyBindings.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << "FoundationActionKeyBinding { key = ";
            emitString(out, actionKeyBindings[index].first);
            out << " actionName = ";
            emitString(out, actionKeyBindings[index].second);
            out << " }";
        }
        out << "]\n    }\n"
            << "}\n";

        const auto authorizerType =
            qualifiedName("foundation.actions.Authorizer", rootPackage, aliases);
        const auto successExpression = [](const DispatchAction &action,
                                          std::string_view value) {
            if (action.successType == voidType || action.successType == neverType) {
                return std::string(".Ok(.") + action.method + ')';
            }
            return std::string(".Ok(.") + action.method + "(value = " +
                   std::string(value) + "))";
        };
        const auto failureExpression = [](const DispatchAction &action,
                                          std::string_view suffix) {
            return std::string(".Err(.") + action.method + std::string(suffix) +
                   "(error = error))";
        };
        const auto emitExecutionMatch = [&](const DispatchAction &action,
                                            std::string_view expression,
                                            std::string_view indent) {
            out << indent << "match " << expression << " {\n";
            if (action.successType == voidType) {
                out << indent << "    Ok: " << successExpression(action, {}) << "\n";
            } else {
                out << indent << "    Ok(value): " << successExpression(action, "value")
                    << "\n";
            }
            out << indent << "    Err(error): " << failureExpression(action, "Failed")
                << "\n"
                << indent << '}';
        };
        out << "\nmethods FoundationApplication {\n";
        for (std::size_t index = 0; index < dispatchActions.size(); ++index) {
            const auto &action = dispatchActions[index];
            std::vector<std::string> parameters{action.receiverEdit ? "&self" : "self"};
            if (action.scoped) {
                parameters.push_back(std::string(action.scopeEdit ? "&" : "") +
                                     "foundationScope FoundationScope");
            }
            if (!action.parameters.empty()) {
                parameters.push_back("$payload " + payloadType(action));
            }
            if (!action.policies.empty()) {
                parameters.push_back("authorizer " + authorizerType);
            }
            out << "    fn foundationDispatch" << action.method << '(' << join(parameters)
                << ") Result<FoundationActionResult, FoundationDispatchError> {\n";
            if (!action.parameters.empty()) {
                out << "        const " << payloadType(action) << " {";
                for (const auto &parameter : action.parameters) {
                    out << ' ' << parameter.name;
                }
                out << " } = payload\n";
            }
            std::vector<std::string> arguments;
            for (std::size_t parameterIndex = 0;
                 parameterIndex < action.parameters.size(); ++parameterIndex) {
                const auto &parameter = action.parameters[parameterIndex];
                if (parameter.mode == ParameterMode::Edit) {
                    const auto local =
                        "foundationDispatchEdit" + std::to_string(parameterIndex);
                    out << "        var " << local << " = " << parameter.name << "\n";
                    arguments.push_back('&' + local);
                } else {
                    arguments.push_back(std::string(parameterMarker(parameter.mode)) +
                                        parameter.name);
                }
            }
            for (const auto &policy : action.policies) {
                out << "        if !authorizer.Allows(";
                emitString(out, action.name);
                out << ", ";
                emitString(out, policy);
                out << ") {\n            return .Err(.Denied(policy = ";
                emitString(out, policy);
                out << "))\n        }\n";
            }
            if (action.scoped) {
                arguments.insert(arguments.begin(),
                                 std::string(action.scopeEdit ? "&" : "") +
                                     "foundationScope");
            }
            auto invocation = "self." + action.method + '(' + join(arguments) + ')';
            const auto sameError = action.activationError.has_value() &&
                                   action.executionError.has_value() &&
                                   *action.activationError == *action.executionError;
            if (sameError) {
                emitExecutionMatch(action, invocation, "        ");
            } else if (action.activationError.has_value()) {
                out << "        match " << invocation << " {\n";
                if (action.executionError.has_value()) {
                    out << "            Ok(result): ";
                    emitExecutionMatch(action, "result", "            ");
                    out << "\n";
                } else if (action.successType == voidType) {
                    out << "            Ok: " << successExpression(action, {}) << "\n";
                } else {
                    out << "            Ok(value): " << successExpression(action, "value")
                        << "\n";
                }
                out << "            Err(error): "
                    << failureExpression(action, "Activation") << "\n"
                    << "        }";
            } else if (action.executionError.has_value()) {
                emitExecutionMatch(action, invocation, "        ");
            } else if (action.successType == voidType) {
                out << "        " << invocation << "\n"
                    << "        " << successExpression(action, {});
            } else if (action.successType == neverType) {
                out << "        " << invocation;
            } else {
                out << "        " << successExpression(action, invocation);
            }
            out << "\n    }\n";

            const auto emitCheckedHelper = [&](std::string_view kind) {
                auto checkedParameters = std::vector<std::string>{
                    action.receiverEdit ? "&self" : "self"};
                if (action.scoped) {
                    checkedParameters.push_back(
                        std::string(action.scopeEdit ? "&" : "") +
                        "foundationScope FoundationScope");
                }
                const auto checkedName = kind == "Name" ? "foundationDispatchName"
                                                        : "foundationDispatchKey";
                checkedParameters.push_back(std::string(checkedName) + " String");
                if (!action.parameters.empty()) {
                    checkedParameters.push_back("$payload " + payloadType(action));
                }
                if (!action.policies.empty()) {
                    checkedParameters.push_back("authorizer " + authorizerType);
                }
                out << "\n    fn foundationDispatch" << kind << action.method << '('
                    << join(checkedParameters)
                    << ") Result<FoundationActionResult, FoundationDispatchError> {\n"
                    << "        if ";
                if (kind == "Name") {
                    out << checkedName << " != ";
                    emitString(out, action.name);
                } else if (action.keys.empty()) {
                    out << "true";
                } else {
                    for (std::size_t key = 0; key < action.keys.size(); ++key) {
                        if (key != 0) {
                            out << " && ";
                        }
                        out << checkedName << " != ";
                        emitString(out, action.keys[key]);
                    }
                }
                out << " return .Err(.ActionMismatch)\n";
                std::vector<std::string> checkedArguments;
                if (action.scoped) {
                    checkedArguments.push_back(
                        std::string(action.scopeEdit ? "&" : "") + "foundationScope");
                }
                if (!action.parameters.empty()) {
                    checkedArguments.push_back("$payload");
                }
                if (!action.policies.empty()) {
                    checkedArguments.push_back("authorizer");
                }
                out << "        self.foundationDispatch" << action.method << '('
                    << join(checkedArguments) << ")\n"
                    << "    }\n";
            };
            emitCheckedHelper("Name");
            emitCheckedHelper("Key");
            if (index + 1 != dispatchActions.size()) {
                out << '\n';
            }
        }
        out << "}\n";

        const auto emitGroupMethods = [&](const std::vector<const DispatchAction *> &group,
                                          std::string_view requestType,
                                          std::string_view suffix) {
            if (group.empty()) {
                return;
            }
            const auto groupHasPolicies = std::any_of(
                group.begin(), group.end(),
                [](const auto action) { return !action->policies.empty(); });
            const auto receiverEdit = std::any_of(
                group.begin(), group.end(),
                [](const auto action) { return action->receiverEdit; });
            const auto scopeEdit = std::any_of(
                group.begin(), group.end(), [](const auto action) { return action->scopeEdit; });
            std::vector<std::string> common{receiverEdit ? "&self" : "self"};
            if (!suffix.empty()) {
                common.push_back(std::string(scopeEdit ? "&" : "") +
                                 "foundationScope FoundationScope");
            }
            if (groupHasPolicies) {
                common.push_back("authorizer " + authorizerType);
            }

            const auto emitArm = [&](const DispatchAction &action,
                                     std::string_view checkedKind) {
                out << "            " << action.method;
                if (action.parameters.empty()) {
                    out << ": ";
                } else {
                    out << "(payload): ";
                }
                std::vector<std::string> arguments;
                if (action.scoped) {
                    arguments.push_back(std::string(action.scopeEdit ? "&" : "") +
                                        "foundationScope");
                }
                if (!checkedKind.empty()) {
                    arguments.push_back(checkedKind == "Name" ? "foundationDispatchName"
                                                               : "foundationDispatchKey");
                }
                if (!action.parameters.empty()) {
                    arguments.push_back("$payload");
                }
                if (!action.policies.empty()) {
                    arguments.push_back("authorizer");
                }
                out << "self.foundationDispatch" << checkedKind << action.method << '('
                    << join(arguments) << ")\n";
            };
            const auto emitMethod = [&](std::string_view prefix, std::string_view extra,
                std::string_view checkedKind) {
                auto parameters = common;
                if (!extra.empty()) {
                    parameters.push_back(std::string(extra == "name" ? "foundationDispatchName"
                                                                       : "foundationDispatchKey") +
                                         " String");
                }
                parameters.push_back("$request " + std::string(requestType));
                out << "\nmethods FoundationApplication {\n"
                    << "    fn " << prefix << suffix << '(' << join(parameters)
                    << ") Result<FoundationActionResult, FoundationDispatchError> {\n";
                if (extra == "name") {
                    out << "        if ";
                    for (std::size_t index = 0; index < group.size(); ++index) {
                        if (index != 0) {
                            out << " && ";
                        }
                        out << "foundationDispatchName != ";
                        emitString(out, group[index]->name);
                    }
                    out << " return .Err(.UnknownName)\n";
                } else if (extra == "key") {
                    std::vector<std::string> keys;
                    for (const auto action : group) {
                        keys.insert(keys.end(), action->keys.begin(), action->keys.end());
                    }
                    out << "        if ";
                    for (std::size_t index = 0; index < keys.size(); ++index) {
                        if (index != 0) {
                            out << " && ";
                        }
                        out << "foundationDispatchKey != ";
                        emitString(out, keys[index]);
                    }
                    out << " return .Err(.UnknownKey)\n";
                }
                out << "        match request {\n";
                for (const auto action : group) {
                    emitArm(*action, checkedKind);
                }
                out << "        }\n"
                    << "    }\n"
                    << "}\n";
            };
            emitMethod("Dispatch", {}, {});
            emitMethod("DispatchName", "name", "Name");
            const auto hasKeys = std::any_of(group.begin(), group.end(),
                                             [](const auto action) {
                                                 return !action->keys.empty();
                                             });
            if (hasKeys) {
                emitMethod("DispatchKey", "key", "Key");
            }
        };
        emitGroupMethods(globalActions, "FoundationAction", {});
        emitGroupMethods(scopedActions, "FoundationScopedAction", "Scoped");
    }
    out << "\n// foundation:generated application/v1\n";
    return out.str();
}

std::string normalizedOpenAPIPath(std::string_view path) {
    std::string result = "/";
    const auto segments = webRouteSegments(path);
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0) {
            result += '/';
        }
        const auto segment = segments[index];
        if (!webParameterSegment(segment)) {
            result += segment;
            continue;
        }
        auto [name, constraints] = webParameterParts(segment);
        static_cast<void>(constraints);
        result += '{';
        result += name;
        result += '}';
    }
    return result;
}

const char *openAPIParameterLocation(WebBindingSource source) {
    switch (source) {
    case WebBindingSource::Path:
        return "path";
    case WebBindingSource::Query:
        return "query";
    case WebBindingSource::Header:
        return "header";
    case WebBindingSource::Form:
    case WebBindingSource::Body:
    case WebBindingSource::Inject:
        return "";
    }
    return "";
}

std::string lowerASCII(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const auto character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

void emitOpenAPIParameter(std::ostringstream &out, const FirProgram &program,
                          const FirFunction &function,
                          const WebParameterPlan &parameter) {
    const auto local = function.parameters[parameter.index];
    const auto &type = function.locals[local].type;
    bool optional{};
    static_cast<void>(openAPIParameterType(program, type, optional));
    out << "          {\n"
        << "            \"name\": ";
    emitString(out, parameter.name);
    out << ",\n            \"in\": ";
    emitString(out, openAPIParameterLocation(parameter.source));
    if (!parameter.description.empty()) {
        out << ",\n            \"description\": ";
        emitString(out, parameter.description);
    }
    out << ",\n            \"required\": "
        << (parameter.source == WebBindingSource::Path || !optional ? "true" : "false")
        << ",\n            \"schema\": {\n"
        << "              \"type\": ";
    emitString(out, openAPISchemaType(program, type));
    if (parameter.minimum.has_value()) {
        out << ",\n              \"minimum\": " << *parameter.minimum;
    }
    if (!parameter.enumValues.empty()) {
        out << ",\n              \"enum\": [";
        for (std::size_t index = 0; index < parameter.enumValues.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            emitString(out, parameter.enumValues[index]);
        }
        out << ']';
    }
    out << "\n            }\n          }";
}

std::string emitOpenAPIDocument(const FirProgram &program,
                                const std::vector<WebRoutePlan> &routes,
                                std::string_view title, std::string_view version) {
    std::map<std::string, std::vector<const WebRoutePlan *>> paths;
    for (const auto &route : routes) {
        paths[normalizedOpenAPIPath(route.path)].push_back(&route);
    }

    std::ostringstream out;
    out << "{\n  \"openapi\": \"3.0.3\",\n  \"info\": {\n    \"title\": ";
    emitString(out, title);
    out << ",\n    \"version\": ";
    emitString(out, version);
    out << "\n  },\n  \"paths\": {";
    auto firstPath = true;
    for (const auto &[path, operations] : paths) {
        out << (firstPath ? "\n" : ",\n") << "    ";
        firstPath = false;
        emitString(out, path);
        out << ": {";
        for (std::size_t operationIndex = 0; operationIndex < operations.size();
             ++operationIndex) {
            const auto &route = *operations[operationIndex];
            const auto &function = program.functions[route.function];
            out << (operationIndex == 0 ? "\n" : ",\n") << "      ";
            emitString(out, lowerASCII(route.method));
            out << ": {";
            auto firstField = true;
            const auto emitTextField = [&](std::string_view name,
                                           std::string_view value) {
                if (value.empty()) {
                    return;
                }
                out << (firstField ? "\n" : ",\n") << "        ";
                firstField = false;
                emitString(out, name);
                out << ": ";
                emitString(out, value);
            };
            emitTextField("summary", route.summary);
            emitTextField("description", route.description);

            std::vector<const WebParameterPlan *> parameters;
            for (const auto &parameter : route.parameters) {
                if (*openAPIParameterLocation(parameter.source) != '\0') {
                    parameters.push_back(&parameter);
                }
            }
            if (!parameters.empty()) {
                out << (firstField ? "\n" : ",\n") << "        \"parameters\": [\n";
                firstField = false;
                for (std::size_t index = 0; index < parameters.size(); ++index) {
                    if (index != 0) {
                        out << ",\n";
                    }
                    emitOpenAPIParameter(out, program, function, *parameters[index]);
                }
                out << "\n        ]";
            }

            out << (firstField ? "\n" : ",\n") << "        \"responses\": {\n";
            if (route.responses.empty()) {
                out << "          \"200\": {\n"
                    << "            \"description\": \"OK\"\n"
                    << "          }\n";
            } else {
                auto firstResponse = true;
                for (const auto &[status, description] : route.responses) {
                    if (!firstResponse) {
                        out << ",\n";
                    }
                    firstResponse = false;
                    out << "          ";
                    emitString(out, std::to_string(status));
                    out << ": {\n            \"description\": ";
                    emitString(out, description);
                    out << "\n          }";
                }
                out << '\n';
            }
            out << "        }\n      }";
        }
        out << "\n    }";
    }
    if (!firstPath) {
        out << '\n';
    }
    out << "  }\n}\n";
    return out.str();
}

enum class ApplicationArtifact {
    Plan,
    Host,
    OpenAPI,
};

} // namespace

std::string emitApplicationArtifact(const FirProgram &program, Diagnostics &diagnostics,
                                    ApplicationArtifact artifact,
                                    std::string_view generatedSourcePath,
                                    std::string_view title = {},
                                    std::string_view version = {}) {
    std::map<std::string, FirStructId> servicesByName;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        if (program.structs[index].service) {
            servicesByName.emplace(program.structs[index].name, index);
        }
    }

    std::vector<WebRoutePlan> webRoutes;
    std::vector<WebMiddlewarePlan> webMiddlewares;
    std::map<std::pair<std::string, std::string>, FirFunctionId> webRouteKeys;
    std::vector<std::pair<std::string, std::string>> webRoutePatterns;
    std::vector<std::vector<FirFunctionId>> constructors(program.structs.size());
    std::set<FirStructId> pending;
    for (FirFunctionId index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        const auto owner = servicesByName.find(ownerName(function));
        const auto serviceConstructor = function.constructor && owner != servicesByName.end();
        if (serviceConstructor) {
            constructors[owner->second].push_back(index);
            pending.insert(owner->second);
        }
        for (std::size_t parameter = 0;
             parameter < function.parameterAttributes.size(); ++parameter) {
            const auto &attributes = function.parameterAttributes[parameter];
            if (!serviceConstructor && hasAttribute(program, attributes, fromAttribute)) {
                diagnostics.error("FDN2319", "@di.From requires a service constructor parameter",
                                  function.sourceSpan);
            }
        }
        if (function.action && owner != servicesByName.end()) {
            pending.insert(owner->second);
        }

        for (const auto &attribute : function.attributes) {
            const auto declaration = attributeDeclaration(program, attribute);
            if (declaration == nullptr ||
                (declaration->name != webGlobalMiddlewareAttribute &&
                 declaration->name != webGroupMiddlewareAttribute &&
                 declaration->name != webRouteMiddlewareAttribute)) {
                continue;
            }
            if (function.method || function.task || !webMiddlewareSignature(program, function)) {
                diagnostics.error(
                    "FDN2397",
                    "web middleware requires a generic free function with signature "
                    "fn<E>($web.Request, fn($web.Request) "
                    "Result<web.Response, web.DispatchError<E>>) "
                    "Result<web.Response, web.DispatchError<E>>",
                    function.sourceSpan);
                continue;
            }
            const auto orderIndex = declaration->name == webGlobalMiddlewareAttribute
                                        ? 0
                                        : declaration->name == webGroupMiddlewareAttribute ? 1
                                                                                           : 2;
            const auto orderValue = signedIntegerArgument(&attribute, orderIndex);
            if (!orderValue.has_value()) {
                diagnostics.error("FDN2398", "invalid web middleware order",
                                  function.sourceSpan);
                continue;
            }
            WebMiddlewarePlan middleware;
            middleware.function = index;
            middleware.order = *orderValue;
            if (declaration->name == webGlobalMiddlewareAttribute) {
                middleware.scope = WebMiddlewareScope::Global;
            } else if (declaration->name == webGroupMiddlewareAttribute) {
                middleware.scope = WebMiddlewareScope::Group;
                middleware.path = stringArgument(&attribute, 0).value_or("");
                if (!validateWebGroupPrefix(middleware.path)) {
                    diagnostics.error(
                        "FDN2398",
                        "web middleware group prefix must be an absolute static path without "
                        "a trailing slash",
                        function.sourceSpan);
                    continue;
                }
            } else {
                middleware.scope = WebMiddlewareScope::Route;
                middleware.method = enumCase(program, &attribute, 0);
                middleware.path = stringArgument(&attribute, 1).value_or("");
                std::string reason;
                if (middleware.method.empty() ||
                    !validateWebRoutePath(middleware.path, reason)) {
                    diagnostics.error("FDN2398", "invalid web middleware route scope",
                                      function.sourceSpan);
                    continue;
                }
            }
            webMiddlewares.push_back(std::move(middleware));
        }

        const auto routeUse = findAttribute(program, function.attributes, webRouteAttribute);
        if (routeUse == nullptr) {
            if (hasAttribute(program, function.attributes, openAPISummaryAttribute) ||
                hasAttribute(program, function.attributes, openAPIDescriptionAttribute) ||
                hasAttribute(program, function.attributes, openAPIResponseAttribute)) {
                diagnostics.error("FDN2401",
                                  "OpenAPI operation metadata requires an @web.Route function",
                                  function.sourceSpan);
            }
            continue;
        }
        WebRoutePlan route;
        route.function = index;
        route.method = enumCase(program, routeUse, 0);
        route.path = stringArgument(routeUse, 1).value_or("");
        route.task = function.task;
        route.summary =
            stringArgument(findAttribute(program, function.attributes,
                                         openAPISummaryAttribute))
                .value_or("");
        route.description =
            stringArgument(findAttribute(program, function.attributes,
                                         openAPIDescriptionAttribute))
                .value_or("");
        for (const auto &attribute : function.attributes) {
            const auto declaration = attributeDeclaration(program, attribute);
            if (declaration == nullptr || declaration->name != openAPIResponseAttribute) {
                continue;
            }
            const auto status = signedIntegerArgument(&attribute, 0);
            const auto description = stringArgument(&attribute, 1);
            if (!status.has_value() || *status < 100 || *status > 599 ||
                !description.has_value() || description->empty()) {
                diagnostics.error("FDN2402",
                                  "@openapi.Response requires status 100 through 599 and a "
                                  "non-empty description",
                                  function.sourceSpan);
                continue;
            }
            if (!route.responses
                     .emplace(static_cast<std::uint16_t>(*status), *description)
                     .second) {
                diagnostics.error("FDN2402", "duplicate OpenAPI response status " +
                                                   std::to_string(*status),
                                  function.sourceSpan);
            }
        }
        if (function.method) {
            diagnostics.error("FDN2350", "@web.Route requires a free function",
                              function.sourceSpan);
        }
        if (function.generic) {
            diagnostics.error("FDN2351", "@web.Route function cannot be generic",
                              function.sourceSpan);
        }
        if (route.method.empty() || route.path.empty()) {
            diagnostics.error("FDN2353", "@web.Route requires a method and non-empty path",
                              function.sourceSpan);
        } else {
            std::string reason;
            if (!validateWebRoutePath(route.path, reason)) {
                diagnostics.error("FDN2364", "invalid generated web route: " + reason,
                                  function.sourceSpan);
            } else {
                const auto ambiguous = std::find_if(
                    webRoutePatterns.begin(), webRoutePatterns.end(), [&](const auto &other) {
                        return webRoutePatternsAmbiguous(other.second, route.path);
                    });
                if (ambiguous != webRoutePatterns.end()) {
                    diagnostics.error(
                        "FDN2369",
                        "ambiguous generated web routes " + ambiguous->first + " " +
                            ambiguous->second + " and " + route.method + " " + route.path,
                        function.sourceSpan);
                }
                webRoutePatterns.emplace_back(route.method, route.path);
            }
            if (!webRouteKeys.emplace(std::make_pair(route.method, route.path), index)
                     .second) {
                diagnostics.error("FDN2354", "duplicate generated web route " +
                                                  route.method + " " + route.path,
                                  function.sourceSpan);
            }
        }

        std::size_t bodyBindings{};
        std::size_t formBindings{};
        std::set<std::pair<WebBindingSource, std::string>> sourceNames;
        for (std::size_t parameter = 0; parameter < function.parameters.size(); ++parameter) {
            const auto local = function.parameters[parameter];
            const auto &value = function.locals[local];
            const auto &attributes = function.parameterAttributes[parameter];
            std::vector<std::pair<WebBindingSource, const FirAttributeUse *>> bindings;
            for (const auto &[source, name] : {
                     std::pair{WebBindingSource::Path, webPathAttribute},
                     std::pair{WebBindingSource::Query, webQueryAttribute},
                     std::pair{WebBindingSource::Header, webHeaderAttribute},
                     std::pair{WebBindingSource::Form, webFormAttribute},
                     std::pair{WebBindingSource::Body, webBodyAttribute},
                     std::pair{WebBindingSource::Inject, webInjectAttribute},
                 }) {
                if (const auto use = findAttribute(program, attributes, name); use != nullptr) {
                    bindings.emplace_back(source, use);
                }
            }
            if (bindings.size() != 1) {
                diagnostics.error(
                    "FDN2355",
                    "web route parameter requires exactly one binding attribute",
                    function.sourceSpan);
                continue;
            }
            WebParameterPlan binding;
            binding.index = parameter;
            binding.source = bindings.front().first;
            if (binding.source == WebBindingSource::Inject) {
                const auto mode = parameterMode(function, parameter);
                if (mode == ParameterMode::Edit ||
                    (!function.task && mode != ParameterMode::Read)) {
                    diagnostics.error(
                        "FDN2356",
                        function.task
                            ? "task web injection requires read or transfer access"
                            : "injected web services require read access",
                        function.sourceSpan);
                }
                const auto candidates = providerCandidates(program, value.type, std::nullopt);
                if (candidates.empty()) {
                    diagnostics.error("FDN2357", "no service provides " +
                                                       typeName(program, baseType(value.type)) +
                                                       " for web route parameter " + value.name,
                                      function.sourceSpan);
                } else if (candidates.size() != 1) {
                    diagnostics.error("FDN2358", "ambiguous providers for web route parameter " +
                                                       value.name + ": " +
                                                       joinServiceNames(program, candidates),
                                      function.sourceSpan);
                } else {
                    binding.provider = candidates.front();
                    pending.insert(candidates.front());
                }
            } else {
                if (parameterMode(function, parameter) == ParameterMode::Edit) {
                    diagnostics.error("FDN2356", "web source binding cannot use edit access",
                                      function.sourceSpan);
                }
                if (binding.source != WebBindingSource::Body &&
                    !webSourceTypeSupported(program, value.type)) {
                    diagnostics.error(
                        "FDN2359",
                        "web source binding requires String, Option<String>, bool, or integer",
                        function.sourceSpan);
                }
                if (binding.source == WebBindingSource::Body &&
                    !webBodyTypeSupported(program, value.type, function.packageName)) {
                    diagnostics.error(
                        "FDN2368",
                        "body binding requires String or a local concrete @bind.Bindable struct",
                        function.sourceSpan);
                }
                if (binding.source != WebBindingSource::Body) {
                    binding.name = stringArgument(bindings.front().second).value_or("");
                    if (binding.name.empty()) {
                        diagnostics.error("FDN2360", "web source binding name cannot be empty",
                                          function.sourceSpan);
                    }
                    if (binding.source == WebBindingSource::Path &&
                        !webRouteHasParameter(route.path, binding.name)) {
                        diagnostics.error("FDN2367", "path binding is not declared by route: " +
                                                          binding.name,
                                          function.sourceSpan);
                    }
                    if (!sourceNames.emplace(binding.source, binding.name).second) {
                        diagnostics.error("FDN2365", "duplicate web source binding " +
                                                          binding.name,
                                          function.sourceSpan);
                    }
                } else {
                    ++bodyBindings;
                }
                if (binding.source == WebBindingSource::Form) {
                    ++formBindings;
                }
            }
            const auto description =
                findAttribute(program, attributes, openAPIDescriptionAttribute);
            const auto minimum = findAttribute(program, attributes, openAPIMinimumAttribute);
            const auto hasEnumValues =
                hasAttribute(program, attributes, openAPIEnumValueAttribute);
            const auto exposedParameter = binding.source == WebBindingSource::Path ||
                                          binding.source == WebBindingSource::Query ||
                                          binding.source == WebBindingSource::Header;
            if (!exposedParameter &&
                (description != nullptr || minimum != nullptr || hasEnumValues)) {
                diagnostics.error(
                    "FDN2403",
                    "OpenAPI parameter metadata requires a path, query, or header binding",
                    function.sourceSpan);
            } else if (exposedParameter) {
                binding.description = stringArgument(description).value_or("");
                if (description != nullptr && binding.description.empty()) {
                    diagnostics.error("FDN2403",
                                      "@openapi.Description requires non-empty text",
                                      function.sourceSpan);
                }
                if (minimum != nullptr) {
                    const auto schema = openAPISchemaType(program, value.type);
                    const auto argument = numericArgument(minimum);
                    if ((schema != "integer" && schema != "number") ||
                        !argument.has_value()) {
                        diagnostics.error(
                            "FDN2403",
                            "@openapi.Minimum requires a numeric web parameter",
                            function.sourceSpan);
                    } else {
                        binding.minimum = argument->literal;
                    }
                }
                std::set<std::string> values;
                for (const auto &attribute : attributes) {
                    const auto declaration = attributeDeclaration(program, attribute);
                    if (declaration == nullptr ||
                        declaration->name != openAPIEnumValueAttribute) {
                        continue;
                    }
                    const auto enumValue = stringArgument(&attribute);
                    if (openAPISchemaType(program, value.type) != "string" ||
                        !enumValue.has_value() || enumValue->empty()) {
                        diagnostics.error(
                            "FDN2403",
                            "@openapi.EnumValue requires a non-empty String web parameter value",
                            function.sourceSpan);
                        continue;
                    }
                    if (!values.insert(*enumValue).second) {
                        diagnostics.error("FDN2403", "duplicate OpenAPI enum value " +
                                                           *enumValue,
                                          function.sourceSpan);
                        continue;
                    }
                    binding.enumValues.push_back(*enumValue);
                }
            }
            route.parameters.push_back(std::move(binding));
        }
        if (bodyBindings > 1 || (bodyBindings != 0 && formBindings != 0)) {
            diagnostics.error("FDN2366",
                              "web route cannot combine repeated body binding with form binding",
                              function.sourceSpan);
        }
        std::map<FirStructId, std::size_t> injectedProviders;
        for (const auto &parameter : route.parameters) {
            if (parameter.source == WebBindingSource::Inject &&
                parameter.provider.has_value()) {
                ++injectedProviders[*parameter.provider];
            }
        }
        for (const auto &parameter : route.parameters) {
            if (parameter.source != WebBindingSource::Inject ||
                !parameter.provider.has_value() ||
                parameterMode(function, parameter.index) != ParameterMode::Transfer) {
                continue;
            }
            const auto provider = *parameter.provider;
            if (serviceLifetime(program, program.structs[provider]) == Lifetime::Singleton) {
                diagnostics.error(
                    "FDN2395",
                    "task web route cannot take ownership of singleton service " +
                        program.structs[provider].name,
                    function.sourceSpan);
            } else if (injectedProviders[provider] != 1) {
                diagnostics.error(
                    "FDN2396",
                    "owned task web service must be injected exactly once: " +
                        program.structs[provider].name,
                    function.sourceSpan);
            }
        }
        webRoutes.push_back(std::move(route));
    }
    std::map<std::tuple<WebMiddlewareScope, std::string, std::string, std::int64_t>,
             FirFunctionId>
        webMiddlewareOrders;
    for (const auto &middleware : webMiddlewares) {
        const auto key = std::make_tuple(middleware.scope, middleware.method, middleware.path,
                                         middleware.order);
        if (!webMiddlewareOrders.emplace(key, middleware.function).second) {
            diagnostics.error("FDN2399", "duplicate web middleware order in one scope",
                              program.functions[middleware.function].sourceSpan);
        }
        if (middleware.scope == WebMiddlewareScope::Route &&
            !webRouteKeys.contains({middleware.method, middleware.path})) {
            diagnostics.error("FDN2400", "web middleware targets an unknown route " +
                                               middleware.method + " " + middleware.path,
                              program.functions[middleware.function].sourceSpan);
        }
        if (middleware.scope == WebMiddlewareScope::Group &&
            std::none_of(webRoutes.begin(), webRoutes.end(), [&](const auto &route) {
                return webRouteInGroup(route.path, middleware.path);
            })) {
            diagnostics.error("FDN2400", "web middleware group matches no route " +
                                               middleware.path,
                              program.functions[middleware.function].sourceSpan);
        }
    }
    std::sort(webMiddlewares.begin(), webMiddlewares.end(), [&](const auto &left,
                                                               const auto &right) {
        if (left.scope != right.scope) {
            return left.scope < right.scope;
        }
        if (left.scope == WebMiddlewareScope::Group &&
            left.path.size() != right.path.size()) {
            return left.path.size() < right.path.size();
        }
        if (left.path != right.path) {
            return left.path < right.path;
        }
        if (left.method != right.method) {
            return left.method < right.method;
        }
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return program.functions[left.function].name < program.functions[right.function].name;
    });
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        if (program.structs[index].service &&
            (hasAttribute(program, program.structs[index].attributes, scopeAttribute) ||
             hasAttribute(program, program.structs[index].attributes, nameAttribute))) {
            pending.insert(index);
        }
    }

    std::map<FirStructId, ServicePlan> plans;
    while (!pending.empty()) {
        const auto service = *pending.begin();
        pending.erase(pending.begin());
        if (plans.contains(service)) {
            continue;
        }
        const auto &type = program.structs[service];
        if (type.typeParameterCount != 0) {
            diagnostics.error("FDN2300", "generic service requires an explicit provider binding",
                              type.sourceSpan);
            continue;
        }
        const auto providerName =
            stringArgument(findAttribute(program, type.attributes, nameAttribute));
        if (providerName.has_value() && providerName->empty()) {
            diagnostics.error("FDN2345", "DI provider name cannot be empty", type.sourceSpan);
        }
        if (constructors[service].empty()) {
            diagnostics.error("FDN2301", "service requires a ctor declaration",
                              type.sourceSpan);
            continue;
        }
        auto constructor = constructors[service].front();
        if (constructors[service].size() != 1) {
            const auto canonical =
                std::find_if(constructors[service].begin(), constructors[service].end(),
                             [&](const auto candidate) {
                                 return shortName(program.functions[candidate].name) == "New";
                             });
            if (canonical == constructors[service].end()) {
                diagnostics.error(
                    "FDN2302",
                    "service with multiple constructors requires one canonical ctor New",
                    program.functions[constructors[service].back()].sourceSpan);
                continue;
            }
            constructor = *canonical;
        }

        const auto &function = program.functions[constructor];
        auto fallible = false;
        if (function.receiver.has_value()) {
            diagnostics.error("FDN2303", "service constructor cannot declare a receiver",
                              function.sourceSpan);
        }
        if (function.generic) {
            diagnostics.error("FDN2304", "service constructor cannot be generic",
                              function.sourceSpan);
        }
        if (!returnsService(program, function, service, fallible)) {
            diagnostics.error("FDN2305", "constructor must produce its service or Result of it",
                              function.sourceSpan);
        }

        ServicePlan plan{service, constructor, serviceLifetime(program, type), fallible, {}};
        for (std::size_t parameter = 0; parameter < function.parameters.size(); ++parameter) {
            const auto local = function.parameters[parameter];
            const auto &localValue = function.locals[local];
            const auto &attributes = function.parameterAttributes[parameter];
            const auto requestedName =
                stringArgument(findAttribute(program, attributes, fromAttribute));
            const auto input = !serviceDependencyType(program, localValue.type);
            Dependency dependency{localValue.name, localValue.type,
                                  parameterMode(function, parameter), std::nullopt, input};

            if (requestedName.has_value() && requestedName->empty()) {
                diagnostics.error("FDN2346", "DI provider selector cannot be empty",
                                  parameterSpan(function));
            } else if (input && requestedName.has_value()) {
                diagnostics.error("FDN2306",
                                  "application input cannot select a named service provider",
                                  parameterSpan(function));
            } else if (!input) {
                const auto candidates = providerCandidates(program, localValue.type,
                                                           requestedName);
                if (candidates.empty()) {
                    diagnostics.error("FDN2307",
                                      "no service provides " +
                                          typeName(program, baseType(localValue.type)) +
                                          " for " + function.name + "." + localValue.name,
                                      parameterSpan(function));
                } else if (candidates.size() != 1) {
                    diagnostics.error("FDN2308",
                                      "ambiguous providers for " +
                                          typeName(program, baseType(localValue.type)) + ": " +
                                          joinServiceNames(program, candidates),
                                      parameterSpan(function));
                } else {
                    dependency.provider = candidates.front();
                    pending.insert(candidates.front());
                }
            }
            if (dependency.mode == ParameterMode::Edit) {
                diagnostics.error("FDN2309", "DI constructor dependency cannot use edit access",
                                  parameterSpan(function));
            }
            plan.dependencies.push_back(std::move(dependency));
        }
        plans.emplace(service, std::move(plan));
    }

    for (const auto &[service, plan] : plans) {
        for (const auto &dependency : plan.dependencies) {
            if (!dependency.provider.has_value() ||
                dependency.mode != ParameterMode::Transfer ||
                !plans.contains(*dependency.provider)) {
                continue;
            }
            const auto providerLifetime = plans.at(*dependency.provider).lifetime;
            if (lifetimeRank(providerLifetime) < lifetimeRank(plan.lifetime)) {
                diagnostics.error(
                    "FDN2310",
                    "lifetime capture: " + program.structs[service].name + " (" +
                        lifetimeName(plan.lifetime) + ") owns " +
                        program.structs[*dependency.provider].name + " (" +
                        lifetimeName(providerLifetime) + ")",
                    program.functions[plan.constructor].sourceSpan);
            }
        }
    }

    std::optional<FirStructId> webResponse;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        if (program.structs[index].name == "foundation.web.Response") {
            webResponse = index;
            break;
        }
    }
    if (!webRoutes.empty() && !webResponse.has_value()) {
        diagnostics.error("FDN2361", "generated web routes require foundation.web.Response",
                          program.functions[webRoutes.front().function].sourceSpan);
    }
    for (auto &route : webRoutes) {
        const auto &function = program.functions[route.function];
        if (webResponse.has_value()) {
            const Type responseType{TypeKind::Struct, *webResponse};
            if (function.returnType == responseType) {
                route.executionError = std::nullopt;
            } else if (isResultType(program, function.returnType) &&
                       function.returnType.arguments.front() == responseType) {
                route.executionError = function.returnType.arguments[1];
            } else {
                diagnostics.error("FDN2362",
                                  "web route must return web.Response or Result of it",
                                  function.sourceSpan);
            }
        }
        std::set<FirStructId> visited;
        std::optional<Type> activationError;
        std::function<void(FirStructId)> inspectWebActivation = [&](const auto service) {
            const auto &plan = plans.at(service);
            if (plan.lifetime == Lifetime::Singleton || !visited.insert(service).second) {
                return;
            }
            const auto &constructor = program.functions[plan.constructor];
            if (plan.fallible) {
                const auto &candidate = constructor.returnType.arguments[1];
                if (activationError.has_value() && *activationError != candidate) {
                    diagnostics.error(
                        "FDN2394",
                        "fallible constructors in one web activation must use the same error type",
                        constructor.sourceSpan);
                } else {
                    activationError = candidate;
                }
            }
            for (const auto &dependency : plan.dependencies) {
                if (dependency.input) {
                    diagnostics.error(
                        "FDN2393",
                        "web activation cannot supply DI input " + dependency.parameter +
                            " for " + program.structs[service].name,
                        constructor.sourceSpan);
                    continue;
                }
                if (dependency.provider.has_value() && plans.contains(*dependency.provider)) {
                    inspectWebActivation(*dependency.provider);
                }
            }
        };
        for (const auto &parameter : route.parameters) {
            if (parameter.provider.has_value() && plans.contains(*parameter.provider)) {
                inspectWebActivation(*parameter.provider);
            }
        }
        route.activationError = std::move(activationError);
    }
    std::sort(webRoutes.begin(), webRoutes.end(), [](const auto &left, const auto &right) {
        if (left.path != right.path) {
            return left.path < right.path;
        }
        if (left.method != right.method) {
            return left.method < right.method;
        }
        return left.function < right.function;
    });

    std::vector<FirStructId> order;
    std::map<FirStructId, int> state;
    std::vector<FirStructId> stack;
    std::set<std::pair<FirStructId, FirStructId>> reportedCycles;
    std::function<void(FirStructId)> visit = [&](const auto service) {
        if (state[service] == 2) {
            return;
        }
        if (state[service] == 1) {
            return;
        }
        state[service] = 1;
        stack.push_back(service);
        std::vector<FirStructId> dependencies;
        for (const auto &dependency : plans.at(service).dependencies) {
            if (dependency.provider.has_value() && plans.contains(*dependency.provider)) {
                dependencies.push_back(*dependency.provider);
            }
        }
        std::sort(dependencies.begin(), dependencies.end(), [&](const auto left, const auto right) {
            return program.structs[left].name < program.structs[right].name;
        });
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                           dependencies.end());
        for (const auto dependency : dependencies) {
            if (state[dependency] == 1) {
                if (reportedCycles.emplace(service, dependency).second) {
                    auto start = std::find(stack.begin(), stack.end(), dependency);
                    std::string cycle;
                    for (auto current = start; current != stack.end(); ++current) {
                        if (!cycle.empty()) {
                            cycle += " -> ";
                        }
                        cycle += program.structs[*current].name;
                    }
                    cycle += " -> " + program.structs[dependency].name;
                    diagnostics.error("FDN2311", "service dependency cycle: " + cycle,
                                      program.functions[plans.at(service).constructor].sourceSpan);
                }
                continue;
            }
            visit(dependency);
        }
        stack.pop_back();
        state[service] = 2;
        order.push_back(service);
    };
    std::vector<FirStructId> roots;
    roots.reserve(plans.size());
    for (const auto &[service, plan] : plans) {
        static_cast<void>(plan);
        roots.push_back(service);
    }
    std::sort(roots.begin(), roots.end(), [&](const auto left, const auto right) {
        return program.structs[left].name < program.structs[right].name;
    });
    for (const auto service : roots) {
        visit(service);
    }

    std::vector<ActionPlan> actions;
    std::map<std::string, FirFunctionId> actionNames;
    std::map<std::string, FirFunctionId> actionKeys;
    for (FirFunctionId index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        if (!function.action) {
            continue;
        }
        const auto owner = servicesByName.find(ownerName(function));
        if (owner == servicesByName.end()) {
            continue;
        }
        if (function.generic) {
            diagnostics.error("FDN2312", "action cannot be generic", function.sourceSpan);
        }
        if (!function.receiver.has_value()) {
            diagnostics.error("FDN2313", "action requires a receiver", function.sourceSpan);
        } else if (*function.receiver == FirReceiverKind::Own && plans.contains(owner->second) &&
                   plans.at(owner->second).lifetime != Lifetime::Transient) {
            diagnostics.error("FDN2320",
                              "consuming action requires a transient service lifetime",
                              function.sourceSpan);
        }

        auto dispatchName = stringArgument(findAttribute(program, function.attributes,
                                                         actionNameAttribute))
                                .value_or(function.name);
        if (dispatchName.empty()) {
            diagnostics.error("FDN2314", "action dispatch name cannot be empty",
                              function.sourceSpan);
        } else if (!actionNames.emplace(dispatchName, index).second) {
            diagnostics.error("FDN2315", "duplicate action dispatch name " + dispatchName,
                              function.sourceSpan);
        }

        ActionPlan action{index, owner->second, std::move(dispatchName), {}, {}};
        std::set<std::string> actionPolicies;
        for (const auto &attribute : function.attributes) {
            const auto declaration = attributeDeclaration(program, attribute);
            if (declaration == nullptr) {
                continue;
            }
            if (declaration->name == actionKeyAttribute) {
                if (const auto key = stringArgument(&attribute); key.has_value()) {
                    if (key->empty()) {
                        diagnostics.error("FDN2316", "action key cannot be empty",
                                          function.sourceSpan);
                    } else if (!actionKeys.emplace(*key, index).second) {
                        diagnostics.error("FDN2317", "duplicate action key " + *key,
                                          function.sourceSpan);
                    }
                    action.keys.push_back(*key);
                }
            } else if (declaration->name == actionPolicyAttribute) {
                if (const auto policy = stringArgument(&attribute); policy.has_value()) {
                    if (policy->empty()) {
                        diagnostics.error("FDN2343", "action policy cannot be empty",
                                          function.sourceSpan);
                    } else if (!actionPolicies.insert(*policy).second) {
                        diagnostics.error("FDN2344", "duplicate action policy " + *policy,
                                          function.sourceSpan);
                    } else {
                        action.policies.push_back(*policy);
                    }
                }
            }
        }
        std::sort(action.keys.begin(), action.keys.end());
        std::sort(action.policies.begin(), action.policies.end());
        actions.push_back(std::move(action));
    }
    std::sort(actions.begin(), actions.end(), [](const auto &left, const auto &right) {
        return left.name < right.name;
    });

    if (diagnostics.hasErrors()) {
        return {};
    }
    if (artifact == ApplicationArtifact::OpenAPI) {
        return emitOpenAPIDocument(program, webRoutes, title, version);
    }
    if (artifact == ApplicationArtifact::Host) {
        if (program.main >= program.functions.size() ||
            program.functions[program.main].packageName.empty()) {
            diagnostics.error("FDN2330", "application host requires a project package",
                              program.main < program.functions.size()
                                  ? program.functions[program.main].sourceSpan
                                  : SourceSpan{});
            return {};
        }
        return emitApplicationHostSource(program, plans, order, actions, webRoutes,
                                         webMiddlewares, diagnostics,
                                         program.functions[program.main].packageName,
                                         generatedSourcePath, true);
    }

    std::ostringstream out;
    out << "{\"schema\":\"foundation.application/v1\",\"construction\":[";
    for (std::size_t position = 0; position < order.size(); ++position) {
        if (position != 0) {
            out << ',';
        }
        const auto service = order[position];
        const auto &type = program.structs[service];
        const auto &plan = plans.at(service);
        const auto &constructor = program.functions[plan.constructor];
        out << "{\"type\":";
        emitString(out, type.name);
        out << ",\"lifetime\":";
        emitString(out, lifetimeName(plan.lifetime));
        out << ",\"constructor\":";
        emitString(out, constructor.name);
        out << ",\"fallible\":" << (plan.fallible ? "true" : "false")
            << ",\"dependencies\":[";
        for (std::size_t index = 0; index < plan.dependencies.size(); ++index) {
            if (index != 0) {
                out << ',';
            }
            const auto &dependency = plan.dependencies[index];
            out << "{\"parameter\":";
            emitString(out, dependency.parameter);
            out << ",\"type\":";
            emitString(out, typeName(program, baseType(dependency.type)));
            out << ",\"mode\":";
            emitString(out, parameterModeName(dependency.mode));
            if (dependency.input) {
                out << ",\"input\":true";
            } else if (dependency.provider.has_value()) {
                out << ",\"provider\":";
                emitString(out, program.structs[*dependency.provider].name);
            }
            out << '}';
        }
        out << "]}";
    }
    out << "],\"actions\":[";
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto &action = actions[index];
        const auto &function = program.functions[action.function];
        out << "{\"name\":";
        emitString(out, action.name);
        out << ",\"handler\":";
        emitString(out, function.name);
        out << ",\"service\":";
        emitString(out, program.structs[action.service].name);
        out << ",\"receiver\":";
        emitString(out, receiverName(*function.receiver));
        out << ",\"parameters\":[";
        const auto first = function.receiver.has_value() ? 1U : 0U;
        for (std::size_t parameter = first; parameter < function.parameters.size(); ++parameter) {
            if (parameter != first) {
                out << ',';
            }
            const auto local = function.parameters[parameter];
            out << "{\"name\":";
            emitString(out, function.locals[local].name);
            out << ",\"type\":";
            emitString(out, typeName(program, baseType(function.locals[local].type)));
            out << ",\"mode\":";
            emitString(out, parameterModeName(parameterMode(function, parameter)));
            out << '}';
        }
        out << "],\"result\":";
        emitString(out, typeName(program, function.returnType));
        out << ",\"keys\":[";
        for (std::size_t key = 0; key < action.keys.size(); ++key) {
            if (key != 0) {
                out << ',';
            }
            emitString(out, action.keys[key]);
        }
        out << "],\"policies\":[";
        for (std::size_t policy = 0; policy < action.policies.size(); ++policy) {
            if (policy != 0) {
                out << ',';
            }
            emitString(out, action.policies[policy]);
        }
        out << "]}";
    }
    out << "],\"routes\":[";
    for (std::size_t index = 0; index < webRoutes.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto &route = webRoutes[index];
        out << "{\"method\":";
        emitString(out, route.method);
        out << ",\"path\":";
        emitString(out, route.path);
        out << ",\"handler\":";
        emitString(out, program.functions[route.function].name);
        out << ",\"task\":" << (route.task ? "true" : "false") << '}';
    }
    out << "],\"middleware\":[";
    for (std::size_t index = 0; index < webMiddlewares.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto &middleware = webMiddlewares[index];
        out << "{\"scope\":";
        emitString(out, middleware.scope == WebMiddlewareScope::Global
                            ? "global"
                            : middleware.scope == WebMiddlewareScope::Group ? "group"
                                                                           : "route");
        if (!middleware.method.empty()) {
            out << ",\"method\":";
            emitString(out, middleware.method);
        }
        if (!middleware.path.empty()) {
            out << ",\"path\":";
            emitString(out, middleware.path);
        }
        out << ",\"order\":" << middleware.order << ",\"handler\":";
        emitString(out, program.functions[middleware.function].name);
        out << '}';
    }
    out << "]}\n";
    return out.str();
}

std::string emitApplicationPlan(const FirProgram &program, Diagnostics &diagnostics) {
    return emitApplicationArtifact(program, diagnostics, ApplicationArtifact::Plan, {});
}

std::string emitOpenAPI(const FirProgram &program, Diagnostics &diagnostics,
                        std::string_view title, std::string_view version) {
    return emitApplicationArtifact(program, diagnostics, ApplicationArtifact::OpenAPI, {},
                                   title, version);
}

std::string emitPackageSource(const FirProgram &program, Diagnostics &diagnostics,
                              std::string_view rootPackage,
                              std::string_view generatedSourcePath) {
    if (rootPackage.empty()) {
        diagnostics.error("FDN2389", "package generation requires a project package", {});
        return {};
    }
    return emitApplicationHostSource(program, {}, {}, {}, {}, {}, diagnostics,
                                     std::string(rootPackage),
                                     generatedSourcePath, false);
}

std::string emitApplicationHost(const FirProgram &program, Diagnostics &diagnostics,
                                std::string_view generatedSourcePath) {
    return emitApplicationArtifact(program, diagnostics, ApplicationArtifact::Host,
                                   generatedSourcePath);
}

} // namespace foundation
