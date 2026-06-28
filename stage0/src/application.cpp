#include "foundation/application.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace foundation {

namespace {

constexpr std::string_view injectAttribute = "foundation.di.Inject";
constexpr std::string_view scopeAttribute = "foundation.di.Scope";
constexpr std::string_view inputAttribute = "foundation.di.Input";
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
constexpr std::string_view bindableAttribute = "foundation.bind.Bindable";
constexpr std::string_view bindNameAttribute = "foundation.bind.Name";
constexpr std::string_view bindIgnoreAttribute = "foundation.bind.Ignore";
constexpr std::string_view bindFromAttribute = "foundation.bind.From";
constexpr std::string_view bindDefaultAttribute = "foundation.bind.Default";
constexpr std::string_view bindJsonNameAttribute = "foundation.bind.JsonName";
constexpr std::string_view bindJsonAttribute = "foundation.bind.JSON";

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
};

struct WebRoutePlan {
    FirFunctionId function{};
    std::string method;
    std::string path;
    std::vector<WebParameterPlan> parameters;
    std::optional<Type> executionError;
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

bool validWebConstraints(std::string_view value) {
    if (value.empty()) {
        return true;
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

bool isNamedStruct(const FirProgram &program, const Type &type, std::string_view name) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           program.structs[value.declaration].name == name;
}

bool isStringList(const FirProgram &program, const Type &type) {
    const auto value = baseType(type);
    return value.kind == TypeKind::Struct && value.declaration < program.structs.size() &&
           program.structs[value.declaration].name == "std.collections.List" &&
           value.arguments.size() == 1 && value.arguments.front() == stringType;
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
        std::string result = "fn(";
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
                                      Diagnostics &diagnostics,
                                      std::string_view generatedSourcePath) {
    if (program.main >= program.functions.size() ||
        program.functions[program.main].packageName.empty()) {
        diagnostics.error("FDN2330", "application host requires a project package",
                          program.main < program.functions.size()
                              ? program.functions[program.main].sourceSpan
                              : SourceSpan{});
        return {};
    }
    const auto &rootPackage = program.functions[program.main].packageName;
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
    std::map<FirStructId, const StructBindingPlan *> structBindingsByType;
    for (const auto &binding : structBindings) {
        structBindingsByType.emplace(binding.type, &binding);
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
                        "fallible constructors in one action activation must use the same error type",
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
        for (const auto &route : webRoutes) {
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
            std::vector<std::string> arguments;
            arguments.reserve(route.parameters.size());
            for (std::size_t parameterIndex = 0;
                 parameterIndex < route.parameters.size(); ++parameterIndex) {
                const auto &parameter = route.parameters[parameterIndex];
                const auto local = "foundationWebParameter" +
                                   std::to_string(parameterIndex);
                if (parameter.source == WebBindingSource::Inject) {
                    arguments.push_back("self." + fields.at(*parameter.provider));
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
            out << "        discard foundationActiveRequest\n";
            const auto invocation = functionName(function, rootPackage, aliases) + '(' +
                                    join(arguments) + ')';
            if (route.executionError.has_value()) {
                out << "        match " << invocation << " {\n"
                    << "            Ok(response): .Ok(response)\n"
                    << "            Err(error): .Err(.Handler(error = ." << method
                    << "Failed(error = error)))\n"
                    << "        }\n";
            } else {
                out << "        .Ok(" << invocation << ")\n";
            }
            out << "    }\n\n";
        }

        out << "    fn Dispatch(\n"
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
            out << "        if Id == " << (routeIndex + 1)
                << " return self.foundationDispatchWeb"
                << webMethods.at(webRoutes[routeIndex].function)
                << "($foundationActiveRequest)\n";
        }
        out << "        discard foundationActiveRequest\n"
            << "        .Err(.NotFound)\n"
            << "    }\n\n"
            << "    fn ErrorResponse(\n"
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
        for (const auto &route : webRoutes) {
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

} // namespace

std::string emitApplicationArtifact(const FirProgram &program, Diagnostics &diagnostics,
                                    bool host, std::string_view generatedSourcePath) {
    std::map<std::string, FirStructId> servicesByName;
    for (FirStructId index = 0; index < program.structs.size(); ++index) {
        if (program.structs[index].service) {
            servicesByName.emplace(program.structs[index].name, index);
        }
    }

    std::vector<WebRoutePlan> webRoutes;
    std::map<std::pair<std::string, std::string>, FirFunctionId> webRouteKeys;
    std::vector<std::pair<std::string, std::string>> webRoutePatterns;
    std::vector<std::vector<FirFunctionId>> constructors(program.structs.size());
    std::set<FirStructId> pending;
    for (FirFunctionId index = 0; index < program.functions.size(); ++index) {
        const auto &function = program.functions[index];
        const auto owner = servicesByName.find(ownerName(function));
        const auto injected = hasAttribute(program, function.attributes, injectAttribute);
        if (injected) {
            if (owner == servicesByName.end()) {
                diagnostics.error("FDN2318", "@di.Inject owner must be a service",
                                  function.sourceSpan);
            } else {
                constructors[owner->second].push_back(index);
                pending.insert(owner->second);
            }
        }
        for (std::size_t parameter = 0;
             parameter < function.parameterAttributes.size(); ++parameter) {
            const auto &attributes = function.parameterAttributes[parameter];
            if (!injected &&
                (hasAttribute(program, attributes, inputAttribute) ||
                 hasAttribute(program, attributes, fromAttribute))) {
                diagnostics.error("FDN2319",
                                  "DI parameter metadata requires an @di.Inject constructor",
                                  function.sourceSpan);
            }
        }
        if (function.action && owner != servicesByName.end()) {
            pending.insert(owner->second);
        }

        const auto routeUse = findAttribute(program, function.attributes, webRouteAttribute);
        if (routeUse == nullptr) {
            continue;
        }
        WebRoutePlan route;
        route.function = index;
        route.method = enumCase(program, routeUse, 0);
        route.path = stringArgument(routeUse, 1).value_or("");
        if (function.method) {
            diagnostics.error("FDN2350", "@web.Route requires a free function",
                              function.sourceSpan);
        }
        if (function.generic) {
            diagnostics.error("FDN2351", "@web.Route function cannot be generic",
                              function.sourceSpan);
        }
        if (function.task) {
            diagnostics.error("FDN2352", "task web routes are not supported yet",
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
                if (parameterMode(function, parameter) != ParameterMode::Read) {
                    diagnostics.error("FDN2356", "injected web services require read access",
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
            route.parameters.push_back(std::move(binding));
        }
        if (bodyBindings > 1 || (bodyBindings != 0 && formBindings != 0)) {
            diagnostics.error("FDN2366",
                              "web route cannot combine repeated body binding with form binding",
                              function.sourceSpan);
        }
        webRoutes.push_back(std::move(route));
    }
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
            diagnostics.error("FDN2301", "service requires one @di.Inject constructor",
                              type.sourceSpan);
            continue;
        }
        if (constructors[service].size() != 1) {
            diagnostics.error("FDN2302", "service has more than one @di.Inject constructor",
                              program.functions[constructors[service].back()].sourceSpan);
            continue;
        }

        const auto constructor = constructors[service].front();
        const auto &function = program.functions[constructor];
        auto fallible = false;
        if (function.receiver.has_value()) {
            diagnostics.error("FDN2303", "@di.Inject constructor must be an associated function",
                              function.sourceSpan);
        }
        if (function.generic) {
            diagnostics.error("FDN2304", "@di.Inject constructor cannot be generic",
                              function.sourceSpan);
        }
        if (!returnsService(program, function, service, fallible)) {
            diagnostics.error("FDN2305",
                              "@di.Inject constructor must return its service or Result of it",
                              function.sourceSpan);
        }

        ServicePlan plan{service, constructor, serviceLifetime(program, type), fallible, {}};
        for (std::size_t parameter = 0; parameter < function.parameters.size(); ++parameter) {
            const auto local = function.parameters[parameter];
            const auto &localValue = function.locals[local];
            const auto &attributes = function.parameterAttributes[parameter];
            const auto input = hasAttribute(program, attributes, inputAttribute);
            const auto requestedName =
                stringArgument(findAttribute(program, attributes, fromAttribute));
            Dependency dependency{localValue.name, localValue.type,
                                  parameterMode(function, parameter), std::nullopt, input};

            if (requestedName.has_value() && requestedName->empty()) {
                diagnostics.error("FDN2346", "DI provider selector cannot be empty",
                                  parameterSpan(function));
            } else if (input && requestedName.has_value()) {
                diagnostics.error("FDN2306", "DI input cannot also select a named provider",
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
        for (const auto &parameter : route.parameters) {
            if (!parameter.provider.has_value() || !plans.contains(*parameter.provider)) {
                continue;
            }
            if (plans.at(*parameter.provider).lifetime != Lifetime::Singleton) {
                diagnostics.error(
                    "FDN2363",
                    "generated web injection currently requires a singleton service",
                    function.sourceSpan);
            }
        }
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
    if (host) {
        return emitApplicationHostSource(program, plans, order, actions, webRoutes, diagnostics,
                                         generatedSourcePath);
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
    out << "]}\n";
    return out.str();
}

std::string emitApplicationPlan(const FirProgram &program, Diagnostics &diagnostics) {
    return emitApplicationArtifact(program, diagnostics, false, {});
}

std::string emitApplicationHost(const FirProgram &program, Diagnostics &diagnostics,
                                std::string_view generatedSourcePath) {
    return emitApplicationArtifact(program, diagnostics, true, generatedSourcePath);
}

} // namespace foundation
