#include "foundation/lsp.hpp"

#include "foundation/driver.hpp"
#include "foundation/language_service.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace foundation {

namespace {

constexpr std::size_t maxMessageBytes = 16U * 1024U * 1024U;
constexpr std::size_t maxJsonDepth = 256;

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : value_(value) {}
    Json(double value) : value_(value) {}
    Json(int value) : value_(static_cast<double>(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(const char *value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}

    [[nodiscard]] static Json object(std::initializer_list<Object::value_type> values) {
        return Json(Object(values));
    }

    [[nodiscard]] static Json array(std::initializer_list<Json> values) {
        return Json(Array(values));
    }

    [[nodiscard]] const Object *asObject() const { return std::get_if<Object>(&value_); }
    [[nodiscard]] const Array *asArray() const { return std::get_if<Array>(&value_); }
    [[nodiscard]] const std::string *asString() const {
        return std::get_if<std::string>(&value_);
    }
    [[nodiscard]] const double *asNumber() const { return std::get_if<double>(&value_); }
    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    [[nodiscard]] const Json *find(std::string_view name) const {
        const auto *objectValue = asObject();
        if (objectValue == nullptr) {
            return nullptr;
        }
        const auto item = objectValue->find(std::string(name));
        return item == objectValue->end() ? nullptr : &item->second;
    }

    [[nodiscard]] const auto &value() const { return value_; }

  private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_{nullptr};
};

void appendUtf8(std::string &output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

class JsonParser {
  public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    [[nodiscard]] std::optional<Json> parse() {
        auto result = parseValue(0);
        skipWhitespace();
        if (!result.has_value() || offset_ != source_.size()) {
            return std::nullopt;
        }
        return result;
    }

  private:
    void skipWhitespace() {
        while (offset_ < source_.size() &&
               (source_[offset_] == ' ' || source_[offset_] == '\t' ||
                source_[offset_] == '\r' || source_[offset_] == '\n')) {
            ++offset_;
        }
    }

    [[nodiscard]] std::optional<Json> parseValue(std::size_t depth) {
        if (depth > maxJsonDepth) {
            return std::nullopt;
        }
        skipWhitespace();
        if (offset_ >= source_.size()) {
            return std::nullopt;
        }
        switch (source_[offset_]) {
        case 'n':
            return parseLiteral("null", Json(nullptr));
        case 't':
            return parseLiteral("true", Json(true));
        case 'f':
            return parseLiteral("false", Json(false));
        case '"': {
            auto value = parseString();
            return value.has_value() ? std::optional<Json>(Json(std::move(*value)))
                                     : std::nullopt;
        }
        case '[':
            return parseArray(depth + 1);
        case '{':
            return parseObject(depth + 1);
        default:
            return parseNumber();
        }
    }

    [[nodiscard]] std::optional<Json> parseLiteral(std::string_view text, Json value) {
        if (source_.substr(offset_, text.size()) != text) {
            return std::nullopt;
        }
        offset_ += text.size();
        return value;
    }

    [[nodiscard]] std::optional<std::uint32_t> parseHexUnit() {
        if (offset_ + 4 > source_.size()) {
            return std::nullopt;
        }
        std::uint32_t value{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto current = source_[offset_++];
            value <<= 4U;
            if (current >= '0' && current <= '9') {
                value |= static_cast<std::uint32_t>(current - '0');
            } else if (current >= 'a' && current <= 'f') {
                value |= static_cast<std::uint32_t>(current - 'a' + 10);
            } else if (current >= 'A' && current <= 'F') {
                value |= static_cast<std::uint32_t>(current - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return value;
    }

    [[nodiscard]] std::optional<std::string> parseString() {
        if (offset_ >= source_.size() || source_[offset_++] != '"') {
            return std::nullopt;
        }
        std::string result;
        while (offset_ < source_.size()) {
            const auto current = static_cast<unsigned char>(source_[offset_++]);
            if (current == '"') {
                return result;
            }
            if (current < 0x20U) {
                return std::nullopt;
            }
            if (current != '\\') {
                result.push_back(static_cast<char>(current));
                continue;
            }
            if (offset_ >= source_.size()) {
                return std::nullopt;
            }
            switch (source_[offset_++]) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                auto first = parseHexUnit();
                if (!first.has_value()) {
                    return std::nullopt;
                }
                std::uint32_t codePoint = *first;
                if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
                    if (source_.substr(offset_, 2) != "\\u") {
                        return std::nullopt;
                    }
                    offset_ += 2;
                    auto second = parseHexUnit();
                    if (!second.has_value() || *second < 0xdc00U || *second > 0xdfffU) {
                        return std::nullopt;
                    }
                    codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) +
                                (*second - 0xdc00U);
                } else if (codePoint >= 0xdc00U && codePoint <= 0xdfffU) {
                    return std::nullopt;
                }
                appendUtf8(result, codePoint);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Json> parseArray(std::size_t depth) {
        ++offset_;
        Json::Array result;
        skipWhitespace();
        if (offset_ < source_.size() && source_[offset_] == ']') {
            ++offset_;
            return Json(std::move(result));
        }
        while (offset_ < source_.size()) {
            auto value = parseValue(depth);
            if (!value.has_value()) {
                return std::nullopt;
            }
            result.push_back(std::move(*value));
            skipWhitespace();
            if (offset_ < source_.size() && source_[offset_] == ']') {
                ++offset_;
                return Json(std::move(result));
            }
            if (offset_ >= source_.size() || source_[offset_++] != ',') {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Json> parseObject(std::size_t depth) {
        ++offset_;
        Json::Object result;
        skipWhitespace();
        if (offset_ < source_.size() && source_[offset_] == '}') {
            ++offset_;
            return Json(std::move(result));
        }
        while (offset_ < source_.size()) {
            skipWhitespace();
            auto name = parseString();
            skipWhitespace();
            if (!name.has_value() || offset_ >= source_.size() || source_[offset_++] != ':') {
                return std::nullopt;
            }
            auto value = parseValue(depth);
            if (!value.has_value() || !result.emplace(std::move(*name), std::move(*value)).second) {
                return std::nullopt;
            }
            skipWhitespace();
            if (offset_ < source_.size() && source_[offset_] == '}') {
                ++offset_;
                return Json(std::move(result));
            }
            if (offset_ >= source_.size() || source_[offset_++] != ',') {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Json> parseNumber() {
        const auto start = offset_;
        if (offset_ < source_.size() && source_[offset_] == '-') {
            ++offset_;
        }
        if (offset_ >= source_.size()) {
            return std::nullopt;
        }
        if (source_[offset_] == '0') {
            ++offset_;
        } else if (source_[offset_] >= '1' && source_[offset_] <= '9') {
            while (offset_ < source_.size() && source_[offset_] >= '0' &&
                   source_[offset_] <= '9') {
                ++offset_;
            }
        } else {
            return std::nullopt;
        }
        if (offset_ < source_.size() && source_[offset_] == '.') {
            ++offset_;
            const auto fraction = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' &&
                   source_[offset_] <= '9') {
                ++offset_;
            }
            if (fraction == offset_) {
                return std::nullopt;
            }
        }
        if (offset_ < source_.size() &&
            (source_[offset_] == 'e' || source_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < source_.size() &&
                (source_[offset_] == '+' || source_[offset_] == '-')) {
                ++offset_;
            }
            const auto exponent = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' &&
                   source_[offset_] <= '9') {
                ++offset_;
            }
            if (exponent == offset_) {
                return std::nullopt;
            }
        }
        double value{};
        const auto number = source_.substr(start, offset_ - start);
        const auto conversion = std::from_chars(number.data(), number.data() + number.size(), value);
        if (conversion.ec != std::errc{} || conversion.ptr != number.data() + number.size() ||
            !std::isfinite(value)) {
            return std::nullopt;
        }
        return Json(value);
    }

    std::string_view source_;
    std::size_t offset_{};
};

void writeJsonString(std::ostream &output, std::string_view value) {
    output << '"';
    for (const auto byte : value) {
        const auto current = static_cast<unsigned char>(byte);
        switch (current) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (current < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(current) << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(current);
            }
            break;
        }
    }
    output << '"';
}

void writeJson(std::ostream &output, const Json &value) {
    std::visit(
        [&output](const auto &current) {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                output << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                output << (current ? "true" : "false");
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::floor(current) == current &&
                    current >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
                    current <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                    output << static_cast<std::int64_t>(current);
                } else {
                    output << std::setprecision(17) << current;
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                writeJsonString(output, current);
            } else if constexpr (std::is_same_v<T, Json::Array>) {
                output << '[';
                for (std::size_t index = 0; index < current.size(); ++index) {
                    if (index != 0) {
                        output << ',';
                    }
                    writeJson(output, current[index]);
                }
                output << ']';
            } else {
                output << '{';
                bool first = true;
                for (const auto &[name, item] : current) {
                    if (!first) {
                        output << ',';
                    }
                    first = false;
                    writeJsonString(output, name);
                    output << ':';
                    writeJson(output, item);
                }
                output << '}';
            }
        },
        value.value());
}

std::string serialize(const Json &value) {
    std::ostringstream output;
    writeJson(output, value);
    return output.str();
}

enum class ReadStatus {
    Message,
    End,
    Invalid,
};

ReadStatus readMessage(std::istream &input, std::string &body) {
    std::optional<std::size_t> contentLength;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            return ReadStatus::Invalid;
        }
        auto name = line.substr(0, separator);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (name != "content-length") {
            continue;
        }
        auto value = std::string_view(line).substr(separator + 1);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        std::size_t length{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            length > maxMessageBytes || contentLength.has_value()) {
            return ReadStatus::Invalid;
        }
        contentLength = length;
    }
    if (!input && line.empty() && !contentLength.has_value()) {
        return input.eof() ? ReadStatus::End : ReadStatus::Invalid;
    }
    if (!contentLength.has_value()) {
        return ReadStatus::Invalid;
    }
    body.resize(*contentLength);
    input.read(body.data(), static_cast<std::streamsize>(body.size()));
    return input.gcount() == static_cast<std::streamsize>(body.size()) ? ReadStatus::Message
                                                                      : ReadStatus::Invalid;
}

void sendMessage(std::ostream &output, const Json &message) {
    const auto body = serialize(message);
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    output.flush();
}

Json response(const Json &id, Json result) {
    return Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

Json errorResponse(const Json &id, int code, std::string message) {
    return Json::object(
        {{"jsonrpc", "2.0"},
         {"id", id},
         {"error", Json::object({{"code", code}, {"message", std::move(message)}})}});
}

std::optional<std::string> stringField(const Json *value, std::string_view name) {
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto *field = value->find(name);
    if (field == nullptr || field->asString() == nullptr) {
        return std::nullopt;
    }
    return *field->asString();
}

std::optional<std::filesystem::path> fileUriToPath(std::string_view uri) {
    constexpr std::string_view prefix = "file://";
    if (!uri.starts_with(prefix)) {
        return std::nullopt;
    }
    uri.remove_prefix(prefix.size());
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t index = 0; index < uri.size(); ++index) {
        if (uri[index] != '%') {
            decoded.push_back(uri[index]);
            continue;
        }
        if (index + 2 >= uri.size()) {
            return std::nullopt;
        }
        unsigned int value{};
        const auto first = uri[index + 1];
        const auto second = uri[index + 2];
        const auto hex = [](char current) -> std::optional<unsigned int> {
            if (current >= '0' && current <= '9') {
                return static_cast<unsigned int>(current - '0');
            }
            if (current >= 'a' && current <= 'f') {
                return static_cast<unsigned int>(current - 'a' + 10);
            }
            if (current >= 'A' && current <= 'F') {
                return static_cast<unsigned int>(current - 'A' + 10);
            }
            return std::nullopt;
        };
        const auto high = hex(first);
        const auto low = hex(second);
        if (!high.has_value() || !low.has_value()) {
            return std::nullopt;
        }
        value = (*high << 4U) | *low;
        decoded.push_back(static_cast<char>(value));
        index += 2;
    }
#ifdef _WIN32
    if (decoded.size() >= 3 && decoded.front() == '/' && decoded[2] == ':') {
        decoded.erase(decoded.begin());
    }
#endif
    return std::filesystem::path(decoded);
}

bool unreservedUriByte(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '.' || value == '_' ||
           value == '~' || value == '/' || value == ':';
}

std::string pathToFileUri(const std::filesystem::path &path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    auto value = absolute.lexically_normal().generic_string();
#ifdef _WIN32
    if (!value.empty() && value.front() != '/') {
        value.insert(value.begin(), '/');
    }
#endif
    std::ostringstream output;
    output << "file://";
    constexpr char digits[] = "0123456789ABCDEF";
    for (const auto byte : value) {
        const auto current = static_cast<unsigned char>(byte);
        if (unreservedUriByte(current)) {
            output << static_cast<char>(current);
        } else {
            output << '%' << digits[current >> 4U] << digits[current & 0x0fU];
        }
    }
    return output.str();
}

std::filesystem::path normalizedPath(const std::filesystem::path &path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

bool containsPath(const std::filesystem::path &root, const std::filesystem::path &path) {
    const auto relative = normalizedPath(path).lexically_relative(normalizedPath(root));
    return !relative.empty() && *relative.begin() != "..";
}

struct Position {
    std::size_t line{};
    std::size_t character{};
};

std::size_t utf8SequenceLength(unsigned char lead) {
    if ((lead & 0x80U) == 0) {
        return 1;
    }
    if ((lead & 0xe0U) == 0xc0U) {
        return 2;
    }
    if ((lead & 0xf0U) == 0xe0U) {
        return 3;
    }
    if ((lead & 0xf8U) == 0xf0U) {
        return 4;
    }
    return 1;
}

Position positionAt(std::string_view source, std::size_t target) {
    target = std::min(target, source.size());
    Position result;
    std::size_t offset{};
    while (offset < target) {
        const auto current = static_cast<unsigned char>(source[offset]);
        if (current == '\n') {
            ++result.line;
            result.character = 0;
            ++offset;
            continue;
        }
        const auto length = std::min(utf8SequenceLength(current), target - offset);
        result.character += length == 4 ? 2 : 1;
        offset += length;
    }
    return result;
}

Json lspPosition(Position position) {
    return Json::object({{"line", static_cast<double>(position.line)},
                         {"character", static_cast<double>(position.character)}});
}

Json lspRange(std::string_view source, SourceSpan span) {
    const auto start = positionAt(source, span.offset);
    const auto end = positionAt(source, span.offset + std::max<std::size_t>(span.length, 1));
    return Json::object({{"start", lspPosition(start)}, {"end", lspPosition(end)}});
}

std::string shortName(std::string_view name) {
    const auto separator = name.rfind('.');
    return std::string(name.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

bool identifierByte(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

SourceSpan nameSpan(std::string_view source, SourceSpan declaration, std::string_view name) {
    const auto start = std::min(declaration.offset, source.size());
    const auto limit = std::min(source.size(), start + 512);
    auto offset = source.find(name, start);
    while (offset != std::string_view::npos && offset < limit) {
        const auto left = offset == 0 ||
                          !identifierByte(static_cast<unsigned char>(source[offset - 1]));
        const auto end = offset + name.size();
        const auto right = end == source.size() ||
                           !identifierByte(static_cast<unsigned char>(source[end]));
        if (left && right) {
            return {offset, name.size(), declaration.line, declaration.column,
                    declaration.source};
        }
        offset = source.find(name, offset + 1);
    }
    return declaration;
}

struct SymbolItem {
    std::string name;
    std::string detail;
    int kind{};
    SourceSpan span;
    std::vector<SymbolItem> children;
};

std::string displayTypeSyntax(const TypeSyntax &type) {
    if (type.name == "[array]" && type.arguments.size() == 1) {
        return '[' + std::to_string(type.arrayLength) + ']' + displayTypeSyntax(type.arguments[0]);
    }
    if (type.name == "[slice]" && type.arguments.size() == 1) {
        return '[' + displayTypeSyntax(type.arguments[0]) + ']';
    }
    if (type.name == "[function]" && !type.arguments.empty()) {
        std::string result = "fn(";
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            if (index != 1) {
                result += ", ";
            }
            result += displayTypeSyntax(type.arguments[index]);
        }
        result += ") " + displayTypeSyntax(type.arguments[0]);
        return result;
    }
    if ((type.name == "own" || type.name == "view" || type.name == "edit") &&
        type.arguments.size() == 1) {
        return type.name + ' ' + displayTypeSyntax(type.arguments[0]);
    }
    std::string result = shortName(type.name);
    if (type.arguments.empty()) {
        return result;
    }
    result += '<';
    for (std::size_t index = 0; index < type.arguments.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += displayTypeSyntax(type.arguments[index]);
    }
    result += '>';
    return result;
}

std::string typeParametersSuffix(const std::vector<std::string> &parameters) {
    if (parameters.empty()) {
        return {};
    }
    std::string result = "<";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += parameters[index];
    }
    result += '>';
    return result;
}

std::string functionDetail(const Function &function) {
    std::string result = "fn " + shortName(function.name) +
                         typeParametersSuffix(function.typeParameters) + '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        const auto &parameter = function.parameters[index];
        if (function.receiver.has_value() && index == 0) {
            result += function.receiver == ReceiverKind::View ? "view"
                      : function.receiver == ReceiverKind::Edit ? "edit"
                                                                : "own";
        } else {
            result += parameter.name + ' ' + displayTypeSyntax(parameter.type);
        }
    }
    result += ") " + displayTypeSyntax(function.returnType);
    return result;
}

std::string contractMethodDetail(const ContractMethod &method) {
    std::string result = "fn " + method.name + '(';
    result += method.receiver == ReceiverKind::View ? "view"
              : method.receiver == ReceiverKind::Edit ? "edit"
                                                      : "own";
    for (const auto &parameter : method.parameters) {
        result += ", " + parameter.name + ' ' + displayTypeSyntax(parameter.type);
    }
    result += ") " + displayTypeSyntax(method.returnType);
    return result;
}

std::vector<SymbolItem> documentSymbols(const ProjectAnalysis &analysis,
                                        std::size_t sourceId) {
    std::vector<SymbolItem> result;
    const auto addFields = [&analysis](SymbolItem &owner,
                                      const std::vector<StructField> &fields) {
        for (const auto &field : fields) {
            owner.children.push_back(
                {field.name, field.name + ' ' + displayTypeSyntax(field.type), 8, field.span, {}});
        }
        for (const auto &function : analysis.program.functions) {
            if (!function.receiver.has_value() || function.ownerType.empty() ||
                function.ownerType != owner.name) {
                continue;
            }
            owner.children.push_back(
                {shortName(function.name), functionDetail(function), 6, function.span, {}});
        }
    };
    for (const auto &declaration : analysis.program.structs) {
        if (declaration.span.source != sourceId) {
            continue;
        }
        SymbolItem item{declaration.name,
                        "struct " + shortName(declaration.name) +
                            typeParametersSuffix(declaration.typeParameters),
                        23, declaration.span, {}};
        addFields(item, declaration.fields);
        item.name = shortName(item.name);
        result.push_back(std::move(item));
    }
    for (const auto &declaration : analysis.program.enums) {
        if (declaration.builtin != BuiltinEnumKind::None ||
            declaration.span.source != sourceId) {
            continue;
        }
        SymbolItem item{shortName(declaration.name),
                        "enum " + shortName(declaration.name) +
                            typeParametersSuffix(declaration.typeParameters),
                        10, declaration.span, {}};
        for (const auto &variant : declaration.variants) {
            auto detail = variant.name;
            if (variant.payloadType.has_value()) {
                detail += '(' + displayTypeSyntax(*variant.payloadType) + ')';
            }
            item.children.push_back({variant.name, std::move(detail), 22, variant.span, {}});
        }
        result.push_back(std::move(item));
    }
    for (const auto &declaration : analysis.program.contracts) {
        if (declaration.span.source != sourceId) {
            continue;
        }
        SymbolItem item{shortName(declaration.name),
                        "contract " + shortName(declaration.name) +
                            typeParametersSuffix(declaration.typeParameters),
                        11, declaration.span, {}};
        for (const auto &method : declaration.methods) {
            item.children.push_back(
                {method.name, contractMethodDetail(method), 6, method.span, {}});
        }
        result.push_back(std::move(item));
    }
    for (const auto &declaration : analysis.program.attributeDeclarations) {
        if (declaration.span.source == sourceId) {
            std::string detail = "attribute " + shortName(declaration.name) + '(';
            for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
                if (index != 0) {
                    detail += ", ";
                }
                const auto &parameter = declaration.parameters[index];
                detail += parameter.name + ' ' + displayTypeSyntax(parameter.type);
            }
            detail += ')';
            result.push_back(
                {shortName(declaration.name), std::move(detail), 12, declaration.span, {}});
        }
    }
    for (const auto &function : analysis.program.functions) {
        if (function.span.source != sourceId || function.receiver.has_value() ||
            function.closure) {
            continue;
        }
        result.push_back(
            {shortName(function.name), functionDetail(function), 12, function.span, {}});
    }
    const auto order = [](const SymbolItem &left, const SymbolItem &right) {
        return left.span.offset < right.span.offset;
    };
    std::sort(result.begin(), result.end(), order);
    for (auto &item : result) {
        std::sort(item.children.begin(), item.children.end(), order);
    }
    return result;
}

Json symbolJson(const SymbolItem &symbol, const DiagnosticSource &source) {
    const auto selection = nameSpan(source.contents, symbol.span, symbol.name);
    Json::Array children;
    children.reserve(symbol.children.size());
    for (const auto &child : symbol.children) {
        children.push_back(symbolJson(child, source));
    }
    return Json::object({{"name", symbol.name},
                         {"detail", symbol.detail},
                         {"kind", symbol.kind},
                         {"range", lspRange(source.contents, selection)},
                         {"selectionRange", lspRange(source.contents, selection)},
                         {"children", Json(std::move(children))}});
}

std::optional<std::size_t> sourceIdForUri(const ProjectAnalysis &analysis,
                                          std::string_view uri) {
    const auto requested = fileUriToPath(uri);
    if (!requested.has_value()) {
        return std::nullopt;
    }
    const auto requestedPath = normalizedPath(*requested);
    for (std::size_t index = 0; index < analysis.sources.size(); ++index) {
        if (!analysis.sources[index].identity.empty() &&
            normalizedPath(analysis.sources[index].identity) == requestedPath) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> nonNegativeInteger(const Json *value) {
    if (value == nullptr || value->asNumber() == nullptr || *value->asNumber() < 0 ||
        std::floor(*value->asNumber()) != *value->asNumber() ||
        *value->asNumber() > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value->asNumber());
}

std::optional<Position> jsonPosition(const Json *position) {
    if (position == nullptr) {
        return std::nullopt;
    }
    const auto line = nonNegativeInteger(position->find("line"));
    const auto character = nonNegativeInteger(position->find("character"));
    if (!line.has_value() || !character.has_value()) {
        return std::nullopt;
    }
    return Position{*line, *character};
}

std::optional<Position> requestPosition(const Json *params) {
    return jsonPosition(params == nullptr ? nullptr : params->find("position"));
}

std::optional<std::size_t> offsetAt(std::string_view source, Position target) {
    Position current;
    std::size_t offset{};
    while (offset < source.size()) {
        if (current.line == target.line && current.character >= target.character) {
            return offset;
        }
        const auto byte = static_cast<unsigned char>(source[offset]);
        if (byte == '\n') {
            if (current.line == target.line) {
                return offset;
            }
            ++current.line;
            current.character = 0;
            ++offset;
            continue;
        }
        const auto length = std::min(utf8SequenceLength(byte), source.size() - offset);
        if (current.line == target.line && current.character + (length == 4 ? 2 : 1) >
                                               target.character) {
            return offset;
        }
        current.character += length == 4 ? 2 : 1;
        offset += length;
    }
    return current.line == target.line && current.character == target.character
               ? std::optional<std::size_t>(offset)
               : std::nullopt;
}

std::optional<SourceSpan> wordAt(std::string_view source, Position position,
                                 std::size_t sourceId) {
    const auto requested = offsetAt(source, position);
    if (!requested.has_value() || source.empty()) {
        return std::nullopt;
    }
    auto offset = std::min(*requested, source.size() - 1);
    if (!identifierByte(static_cast<unsigned char>(source[offset])) && offset != 0 &&
        identifierByte(static_cast<unsigned char>(source[offset - 1]))) {
        --offset;
    }
    if (!identifierByte(static_cast<unsigned char>(source[offset]))) {
        return std::nullopt;
    }
    auto start = offset;
    while (start != 0 && identifierByte(static_cast<unsigned char>(source[start - 1]))) {
        --start;
    }
    auto end = offset + 1;
    while (end < source.size() && identifierByte(static_cast<unsigned char>(source[end]))) {
        ++end;
    }
    return SourceSpan{start, end - start, position.line + 1, position.character + 1, sourceId};
}

struct OpenDocument {
    std::string uri;
    std::filesystem::path path;
    std::string contents;
    double version{};
};

struct CachedAnalysis {
    ProjectAnalysis project;
    std::optional<LanguageIndex> languageIndex;
};

class LanguageServer {
  public:
    LanguageServer(std::ostream &output, std::ostream &errors)
        : output_(output), errors_(errors) {}

    [[nodiscard]] bool handle(const Json &message) {
        const auto method = stringField(&message, "method");
        const auto version = stringField(&message, "jsonrpc");
        const auto *id = message.find("id");
        const auto validId = id == nullptr || id->isNull() || id->asString() != nullptr ||
                             id->asNumber() != nullptr;
        if (!method.has_value() || version != "2.0" || !validId) {
            sendMessage(output_, errorResponse(id == nullptr ? Json(nullptr) : *id, -32600,
                                               "invalid JSON-RPC request"));
            return true;
        }
        if (*method == "initialize") {
            if (initialized_) {
                if (id != nullptr) {
                    sendMessage(output_, errorResponse(*id, -32600,
                                                       "server is already initialized"));
                }
                return true;
            }
            initialize(message.find("params"));
            if (id != nullptr) {
                sendMessage(output_, response(*id, initializeResult()));
            }
            return true;
        }
        if (*method == "exit") {
            cleanExit_ = shutdown_;
            return false;
        }
        if (!initialized_) {
            if (id != nullptr) {
                sendMessage(output_, errorResponse(*id, -32002, "server is not initialized"));
            }
            return true;
        }
        if (shutdown_) {
            if (id != nullptr) {
                sendMessage(output_, errorResponse(*id, -32600, "server is shutting down"));
            }
            return true;
        }
        if (*method == "shutdown") {
            shutdown_ = true;
            if (id != nullptr) {
                sendMessage(output_, response(*id, Json(nullptr)));
            }
            return true;
        }
        if (*method == "textDocument/didOpen") {
            didOpen(message.find("params"));
        } else if (*method == "textDocument/didChange") {
            didChange(message.find("params"));
        } else if (*method == "textDocument/didClose") {
            didClose(message.find("params"));
        } else if (*method == "workspace/didChangeWorkspaceFolders") {
            didChangeWorkspaceFolders(message.find("params"));
        } else if (*method == "workspace/didChangeWatchedFiles") {
            didChangeWatchedFiles();
        } else if (*method == "textDocument/documentSymbol" && id != nullptr) {
            sendMessage(output_, response(*id, provideDocumentSymbols(message.find("params"))));
        } else if (*method == "workspace/symbol" && id != nullptr) {
            sendMessage(output_, response(*id, provideWorkspaceSymbols(message.find("params"))));
        } else if (*method == "textDocument/hover" && id != nullptr) {
            sendMessage(output_, response(*id, provideHover(message.find("params"))));
        } else if (*method == "textDocument/definition" && id != nullptr) {
            sendMessage(output_, response(*id, provideDefinition(message.find("params"))));
        } else if (*method == "textDocument/references" && id != nullptr) {
            sendMessage(output_, response(*id, provideReferences(message.find("params"))));
        } else if (*method == "textDocument/prepareRename" && id != nullptr) {
            sendMessage(output_, response(*id, providePrepareRename(message.find("params"))));
        } else if (*method == "textDocument/rename" && id != nullptr) {
            sendMessage(output_, response(*id, provideRename(message.find("params"))));
        } else if (*method == "textDocument/completion" && id != nullptr) {
            sendMessage(output_, response(*id, provideCompletions(message.find("params"))));
        } else if (*method == "textDocument/signatureHelp" && id != nullptr) {
            sendMessage(output_, response(*id, provideSignatureHelp(message.find("params"))));
        } else if (*method == "textDocument/semanticTokens/full" && id != nullptr) {
            sendMessage(output_, response(*id, provideSemanticTokens(message.find("params"))));
        } else if (*method == "textDocument/inlayHint" && id != nullptr) {
            sendMessage(output_, response(*id, provideInlayHints(message.find("params"))));
        } else if (*method == "$/cancelRequest" || *method == "initialized" ||
                   *method == "workspace/didChangeConfiguration") {
        } else if (id != nullptr) {
            sendMessage(output_, errorResponse(*id, -32601, "method not found"));
        }
        return true;
    }

    [[nodiscard]] int exitCode() const { return cleanExit_ ? 0 : 1; }

  private:
    void initialize(const Json *params) {
        initialized_ = true;
        const auto *folders = params == nullptr ? nullptr : params->find("workspaceFolders");
        if (folders != nullptr && folders->asArray() != nullptr) {
            for (const auto &folder : *folders->asArray()) {
                addWorkspaceRoot(stringField(&folder, "uri"));
            }
        }
        if (workspaceRoots_.empty()) {
            addWorkspaceRoot(stringField(params, "rootUri"));
        }
        if (workspaceRoots_.empty() && params != nullptr) {
            if (const auto rootPath = stringField(params, "rootPath"); rootPath.has_value()) {
                addWorkspaceRoot(normalizedPath(*rootPath));
            }
        }
    }

    [[nodiscard]] Json initializeResult() const {
        return Json::object(
            {{"capabilities",
              Json::object({{"positionEncoding", "utf-16"},
                            {"textDocumentSync", 1},
                            {"hoverProvider", true},
                            {"definitionProvider", true},
                            {"referencesProvider", true},
                            {"renameProvider", Json::object({{"prepareProvider", true}})},
                            {"completionProvider",
                             Json::object({{"triggerCharacters", Json::array({".", "@"})}})},
                            {"signatureHelpProvider",
                             Json::object({{"triggerCharacters", Json::array({"(", ","})}})},
                            {"semanticTokensProvider",
                             Json::object(
                                 {{"legend",
                                   Json::object(
                                       {{"tokenTypes",
                                         Json::array({"function", "method", "struct",
                                                      "property", "enum", "enumMember",
                                                      "interface", "decorator", "parameter",
                                                      "variable"})},
                                        {"tokenModifiers", Json::array({"declaration"})}})},
                                  {"full", true}})},
                            {"inlayHintProvider", true},
                            {"documentSymbolProvider", true},
                            {"workspaceSymbolProvider", true},
                            {"workspace",
                             Json::object(
                                 {{"workspaceFolders",
                                   Json::object({{"supported", true},
                                                 {"changeNotifications", true}})}})}})},
             {"serverInfo",
              Json::object({{"name", "foundation-ls"}, {"version", "0.1.0-stage0"}})}});
    }

    void didOpen(const Json *params) {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto contents = stringField(textDocument, "text");
        if (!uri.has_value() || !contents.has_value()) {
            return;
        }
        const auto path = fileUriToPath(*uri);
        if (!path.has_value()) {
            return;
        }
        double version{};
        if (const auto *value = textDocument->find("version");
            value != nullptr && value->asNumber() != nullptr) {
            version = *value->asNumber();
        }
        documents_[*uri] = {*uri, normalizedPath(*path), *contents, version};
        invalidateAnalyses();
        publishDiagnostics(*uri);
    }

    void didChange(const Json *params) {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto *changes = params == nullptr ? nullptr : params->find("contentChanges");
        if (!uri.has_value() || changes == nullptr || changes->asArray() == nullptr ||
            changes->asArray()->empty()) {
            return;
        }
        const auto document = documents_.find(*uri);
        if (document == documents_.end()) {
            return;
        }
        const auto *versionValue = textDocument->find("version");
        const auto incomingVersion = versionValue == nullptr ? nullptr : versionValue->asNumber();
        if (incomingVersion != nullptr && *incomingVersion <= document->second.version) {
            return;
        }
        const auto &change = changes->asArray()->back();
        const auto contents = stringField(&change, "text");
        if (!contents.has_value() || change.find("range") != nullptr) {
            errors_ << "foundation-ls: only full document changes are accepted\n";
            return;
        }
        document->second.contents = *contents;
        if (incomingVersion != nullptr) {
            document->second.version = *incomingVersion;
        }
        invalidateAnalyses();
        publishDiagnostics(*uri);
    }

    void didClose(const Json *params) {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return;
        }
        documents_.erase(*uri);
        invalidateAnalyses();
        publishDiagnostics(*uri);
    }

    void addWorkspaceRoot(const std::optional<std::string> &uri) {
        if (!uri.has_value()) {
            return;
        }
        const auto path = fileUriToPath(*uri);
        if (path.has_value()) {
            addWorkspaceRoot(normalizedPath(*path));
        }
    }

    void addWorkspaceRoot(std::filesystem::path path) {
        if (std::find(workspaceRoots_.begin(), workspaceRoots_.end(), path) ==
            workspaceRoots_.end()) {
            workspaceRoots_.push_back(std::move(path));
        }
    }

    void didChangeWorkspaceFolders(const Json *params) {
        const auto *event = params == nullptr ? nullptr : params->find("event");
        const auto *removed = event == nullptr ? nullptr : event->find("removed");
        if (removed != nullptr && removed->asArray() != nullptr) {
            for (const auto &folder : *removed->asArray()) {
                const auto uri = stringField(&folder, "uri");
                const auto path = uri.has_value() ? fileUriToPath(*uri) : std::nullopt;
                if (path.has_value()) {
                    const auto normalized = normalizedPath(*path);
                    std::erase(workspaceRoots_, normalized);
                }
            }
        }
        const auto *added = event == nullptr ? nullptr : event->find("added");
        if (added != nullptr && added->asArray() != nullptr) {
            for (const auto &folder : *added->asArray()) {
                addWorkspaceRoot(stringField(&folder, "uri"));
            }
        }
        invalidateAnalyses();
    }

    void didChangeWatchedFiles() {
        invalidateAnalyses();
        std::set<std::filesystem::path> publishedRoots;
        for (const auto &[uri, document] : documents_) {
            const auto root = analysisRoot(document);
            if (publishedRoots.insert(root).second) {
                publishDiagnostics(uri);
            }
        }
    }

    [[nodiscard]] std::filesystem::path analysisRoot(const OpenDocument &document) const {
        const std::filesystem::path *best{};
        for (const auto &root : workspaceRoots_) {
            if (containsPath(root, document.path) &&
                (best == nullptr || root.generic_string().size() > best->generic_string().size())) {
                best = &root;
            }
        }
        if (best != nullptr) {
            return *best;
        }
        return document.path.parent_path();
    }

    [[nodiscard]] std::vector<SourceOverlay> overlays() const {
        std::vector<SourceOverlay> result;
        result.reserve(documents_.size());
        for (const auto &[uri, document] : documents_) {
            static_cast<void>(uri);
            result.push_back({document.path, document.contents});
        }
        return result;
    }

    [[nodiscard]] std::string sourceUri(const std::filesystem::path &path) const {
        const auto normalized = normalizedPath(path);
        for (const auto &[uri, document] : documents_) {
            if (document.path == normalized) {
                return uri;
            }
        }
        return pathToFileUri(path);
    }

    void invalidateAnalyses() { analysisCache_.clear(); }

    [[nodiscard]] const ProjectAnalysis &analyzeRoot(
        const std::filesystem::path &root) const {
        const auto key = normalizedPath(root).generic_string();
        const auto found = analysisCache_.find(key);
        if (found != analysisCache_.end()) {
            return found->second.project;
        }
        auto analysis = analyzeProject(root, overlays(), AnalyzeOptions{.requireMain = false});
        const auto inserted = analysisCache_.emplace(
            key, CachedAnalysis{std::move(analysis), std::nullopt});
        return inserted.first->second.project;
    }

    [[nodiscard]] const LanguageIndex &languageIndex(
        const ProjectAnalysis &analysis) const {
        for (auto &[key, cached] : analysisCache_) {
            static_cast<void>(key);
            if (&cached.project != &analysis) {
                continue;
            }
            if (!cached.languageIndex.has_value()) {
                cached.languageIndex = buildLanguageIndex(cached.project);
            }
            return *cached.languageIndex;
        }
        std::terminate();
    }

    [[nodiscard]] const ProjectAnalysis *analyzeUri(std::string_view uri) const {
        const auto document = documents_.find(std::string(uri));
        if (document == documents_.end()) {
            return nullptr;
        }
        return &analyzeRoot(analysisRoot(document->second));
    }

    [[nodiscard]] Json provideDocumentSymbols(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(Json::Array{});
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(Json::Array{});
        }
        const auto sourceId = sourceIdForUri(*analysis, *uri);
        if (!sourceId.has_value()) {
            return Json(Json::Array{});
        }
        Json::Array result;
        for (const auto &symbol : documentSymbols(*analysis, *sourceId)) {
            result.push_back(symbolJson(symbol, analysis->sources[*sourceId]));
        }
        return Json(std::move(result));
    }

    static bool matchesQuery(std::string_view name, std::string_view query) {
        std::string lowerName(name);
        std::string lowerQuery(query);
        const auto lower = [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        };
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), lower);
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), lower);
        return lowerName.find(lowerQuery) != std::string::npos;
    }

    static void appendWorkspaceSymbol(Json::Array &result, const SymbolItem &symbol,
                                      const DiagnosticSource &source,
                                      std::string_view query,
                                      std::optional<std::string> container = std::nullopt) {
        if (matchesQuery(symbol.name, query)) {
            auto value = Json::object(
                {{"name", symbol.name},
                 {"kind", symbol.kind},
                 {"location",
                  Json::object({{"uri", pathToFileUri(source.identity)},
                                {"range", lspRange(source.contents,
                                                   nameSpan(source.contents, symbol.span,
                                                            symbol.name))}})}});
            if (container.has_value()) {
                auto object = *value.asObject();
                object.emplace("containerName", *container);
                value = Json(std::move(object));
            }
            result.push_back(std::move(value));
        }
        for (const auto &child : symbol.children) {
            appendWorkspaceSymbol(result, child, source, query, symbol.name);
        }
    }

    [[nodiscard]] Json provideWorkspaceSymbols(const Json *params) const {
        const auto query = stringField(params, "query").value_or(std::string{});
        std::vector<std::filesystem::path> roots = workspaceRoots_;
        if (roots.empty()) {
            for (const auto &[uri, document] : documents_) {
                static_cast<void>(uri);
                const auto root = analysisRoot(document);
                if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
                    roots.push_back(root);
                }
            }
        }
        Json::Array result;
        for (const auto &root : roots) {
            const auto &analysis = analyzeRoot(root);
            for (std::size_t sourceId = 0; sourceId < analysis.sources.size(); ++sourceId) {
                const auto &source = analysis.sources[sourceId];
                if (source.identity.empty()) {
                    continue;
                }
                for (const auto &symbol : documentSymbols(analysis, sourceId)) {
                    appendWorkspaceSymbol(result, symbol, source, query);
                }
            }
        }
        return Json(std::move(result));
    }

    [[nodiscard]] std::optional<LanguageSymbolId> semanticSymbolAt(
        const ProjectAnalysis &analysis, std::string_view uri, const Json *params,
        SourceSpan &word, const LanguageIndex &index) const {
        const auto sourceId = sourceIdForUri(analysis, uri);
        const auto position = requestPosition(params);
        if (!sourceId.has_value() || !position.has_value()) {
            return std::nullopt;
        }
        const auto foundWord = wordAt(analysis.sources[*sourceId].contents, *position, *sourceId);
        if (!foundWord.has_value()) {
            return std::nullopt;
        }
        word = *foundWord;
        const auto *occurrence = index.occurrenceAt(*sourceId, word.offset);
        return occurrence == nullptr ? std::nullopt
                                     : std::optional<LanguageSymbolId>{occurrence->symbol};
    }

    [[nodiscard]] Json provideHover(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(nullptr);
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(nullptr);
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
        if (symbol == nullptr) {
            return Json(nullptr);
        }
        const auto sourceId = sourceIdForUri(*analysis, *uri);
        if (!sourceId.has_value()) {
            return Json(nullptr);
        }
        return Json::object(
            {{"contents",
              Json::object({{"kind", "markdown"},
                            {"value", "```foundation\n" + symbol->detail + "\n```"}})},
             {"range", lspRange(analysis->sources[*sourceId].contents, word)}});
    }

    [[nodiscard]] Json provideDefinition(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(nullptr);
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(nullptr);
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
        if (symbol == nullptr || symbol->definition.source >= analysis->sources.size()) {
            return Json(nullptr);
        }
        const auto &source = analysis->sources[symbol->definition.source];
        return Json::object({{"uri", pathToFileUri(source.identity)},
                             {"range", lspRange(source.contents, symbol->definition)}});
    }

    [[nodiscard]] Json provideReferences(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(Json::Array{});
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(Json::Array{});
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbol = semanticSymbolAt(*analysis, *uri, params, word, index);
        if (!symbol.has_value()) {
            return Json(Json::Array{});
        }
        auto includeDefinition = true;
        if (const auto *context = params == nullptr ? nullptr : params->find("context");
            context != nullptr) {
            if (const auto *value = context->find("includeDeclaration");
                value != nullptr && std::get_if<bool>(&value->value()) != nullptr) {
                includeDefinition = *std::get_if<bool>(&value->value());
            }
        }
        Json::Array result;
        for (const auto &reference : index.references(*symbol, includeDefinition)) {
            if (reference.span.source >= analysis->sources.size()) {
                continue;
            }
            const auto &source = analysis->sources[reference.span.source];
            result.push_back(Json::object({{"uri", pathToFileUri(source.identity)},
                                           {"range", lspRange(source.contents,
                                                              reference.span)}}));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json providePrepareRename(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(nullptr);
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(nullptr);
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
        if (symbol == nullptr || !symbol->renameable) {
            return Json(nullptr);
        }
        const auto sourceId = sourceIdForUri(*analysis, *uri);
        if (!sourceId.has_value()) {
            return Json(nullptr);
        }
        return Json::object({{"range", lspRange(analysis->sources[*sourceId].contents, word)},
                             {"placeholder", symbol->name}});
    }

    [[nodiscard]] Json provideRename(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto newName = stringField(params, "newName");
        if (!uri.has_value() || !newName.has_value()) {
            return Json(nullptr);
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(nullptr);
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbol = semanticSymbolAt(*analysis, *uri, params, word, index);
        if (!symbol.has_value() || !index.canRename(*symbol, *newName)) {
            return Json(nullptr);
        }
        std::map<std::string, Json::Array> changes;
        for (const auto &occurrence : index.references(*symbol, true)) {
            if (occurrence.span.source >= analysis->sources.size()) {
                continue;
            }
            const auto &source = analysis->sources[occurrence.span.source];
            changes[pathToFileUri(source.identity)].push_back(
                Json::object({{"range", lspRange(source.contents, occurrence.span)},
                              {"newText", *newName}}));
        }
        Json::Object edits;
        for (auto &[sourceUri, sourceEdits] : changes) {
            edits.emplace(std::move(sourceUri), Json(std::move(sourceEdits)));
        }
        return Json::object({{"changes", Json(std::move(edits))}});
    }

    static int completionKind(int symbolKind) {
        switch (symbolKind) {
        case 6:
            return 3;
        case 8:
            return 5;
        case 10:
            return 13;
        case 11:
            return 8;
        case 12:
            return 3;
        case 22:
            return 20;
        case 23:
            return 22;
        default:
            return 6;
        }
    }

    static void addCompletion(std::map<std::string, Json> &items, std::string label,
                              int kind, std::string detail = {},
                              std::optional<std::string> insertText = std::nullopt) {
        Json::Object item{{"label", label}, {"kind", kind}};
        if (!detail.empty()) {
            item.emplace("detail", std::move(detail));
        }
        if (insertText.has_value()) {
            item.emplace("insertText", std::move(*insertText));
            item.emplace("insertTextFormat", 2);
        }
        items.emplace(std::move(label), Json(std::move(item)));
    }

    static std::string packageAlias(std::string_view packageName) {
        return shortName(packageName);
    }

    [[nodiscard]] Json provideCompletions(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(Json::Array{});
        }
        auto analysis = analyzeUri(*uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, *uri);
        if (analysis == nullptr || !sourceId.has_value()) {
            return Json(Json::Array{});
        }
        const auto &source = analysis->sources[*sourceId];
        auto attributeContext = false;
        if (const auto position = requestPosition(params); position.has_value()) {
            if (const auto offset = offsetAt(source.contents, *position); offset.has_value()) {
                auto previous = *offset;
                while (previous != 0 &&
                       (source.contents[previous - 1] == ' ' ||
                        source.contents[previous - 1] == '\t')) {
                    --previous;
                }
                attributeContext = previous != 0 && source.contents[previous - 1] == '@';
            }
        }

        std::map<std::string, Json> items;
        if (!attributeContext) {
            constexpr std::string_view keywords[] = {
                "package", "import", "as",      "extern", "struct", "enum",  "contract",
                "attribute", "implements", "extends", "by",     "fn",    "let",   "var",
                "return",  "discard", "if",      "else",   "while",  "match", "capture",
                "replace", "with",    "own",     "view",   "edit",   "true",  "false",
            };
            for (const auto keyword : keywords) {
                addCompletion(items, std::string(keyword), 14);
            }
            for (const auto type : {"i32", "u64", "bool", "String", "void", "Option",
                                    "Result"}) {
                addCompletion(items, type, 25);
            }
            for (const auto builtin : {"print", "panic", "len"}) {
                addCompletion(items, builtin, 3, "Foundation builtin",
                              std::string(builtin) + "(${1:value})");
            }
        }

        std::map<std::string, std::string> aliases;
        for (const auto &imported : analysis->program.imports) {
            if (imported.span.source == *sourceId) {
                aliases[imported.packageName] = imported.alias.empty()
                                                    ? packageAlias(imported.packageName)
                                                    : imported.alias;
            }
        }
        const auto qualified = [&aliases, &source](std::string_view packageName,
                                                   std::string_view name, bool exported)
            -> std::optional<std::string> {
            if (packageName == source.packageName) {
                return std::string(name);
            }
            const auto alias = aliases.find(std::string(packageName));
            if (!exported || alias == aliases.end()) {
                return std::nullopt;
            }
            return alias->second + '.' + std::string(name);
        };

        for (const auto &declaration : analysis->program.attributeDeclarations) {
            const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                         declaration.exported);
            if (!label.has_value()) {
                continue;
            }
            auto application = '@' + *label;
            addCompletion(items, application, 10, "typed Foundation attribute",
                          application + "($0)");
        }
        if (!attributeContext) {
            for (const auto &function : analysis->program.functions) {
                if (function.receiver.has_value() || function.closure) {
                    continue;
                }
                const auto label = qualified(function.packageName, shortName(function.name),
                                             function.exported || function.name == "main");
                if (label.has_value()) {
                    addCompletion(items, *label, 3, functionDetail(function), *label + "($0)");
                }
            }
            for (const auto &declaration : analysis->program.structs) {
                const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                             declaration.exported);
                if (label.has_value()) {
                    addCompletion(items, *label, 22, "Foundation struct");
                }
            }
            for (const auto &declaration : analysis->program.enums) {
                if (declaration.builtin == BuiltinEnumKind::None) {
                    const auto label = qualified(declaration.packageName,
                                                 shortName(declaration.name),
                                                 declaration.exported);
                    if (label.has_value()) {
                        addCompletion(items, *label, 13, "Foundation enum");
                    }
                }
                for (const auto &variant : declaration.variants) {
                    addCompletion(items, variant.name, 20, "Foundation enum variant");
                }
            }
            for (const auto &declaration : analysis->program.contracts) {
                const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                             declaration.exported);
                if (label.has_value()) {
                    addCompletion(items, *label, 8, "Foundation contract");
                }
            }
            for (const auto &statement : analysis->program.statements) {
                if (statement.span.source != *sourceId) {
                    continue;
                }
                if (const auto *variable = std::get_if<VariableStatement>(&statement.value)) {
                    addCompletion(items, variable->name, 6, "Foundation local binding");
                } else if (const auto *destructure =
                               std::get_if<StructDestructureStatement>(&statement.value)) {
                    for (const auto &field : destructure->fields) {
                        addCompletion(items, field.binding, 6, "Foundation local binding");
                    }
                }
            }
            for (const auto &function : analysis->program.functions) {
                if (function.span.source != *sourceId) {
                    continue;
                }
                for (const auto &parameter : function.parameters) {
                    if (parameter.name != "self") {
                        addCompletion(items, parameter.name, 6,
                                      parameter.name + ' ' + displayTypeSyntax(parameter.type));
                    }
                }
            }
        }
        Json::Array result;
        result.reserve(items.size());
        for (auto &[label, item] : items) {
            static_cast<void>(label);
            result.push_back(std::move(item));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideSignatureHelp(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto position = requestPosition(params);
        if (!uri.has_value() || !position.has_value()) {
            return Json(nullptr);
        }
        auto analysis = analyzeUri(*uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, *uri);
        if (analysis == nullptr || !sourceId.has_value()) {
            return Json(nullptr);
        }
        const auto &source = analysis->sources[*sourceId].contents;
        const auto requested = offsetAt(source, *position);
        if (!requested.has_value()) {
            return Json(nullptr);
        }
        std::size_t open = *requested;
        std::size_t depth{};
        while (open != 0) {
            --open;
            if (source[open] == ')') {
                ++depth;
            } else if (source[open] == '(') {
                if (depth == 0) {
                    break;
                }
                --depth;
            }
        }
        if (open == 0 && (source.empty() || source[open] != '(')) {
            return Json(nullptr);
        }
        auto end = open;
        while (end != 0 && (source[end - 1] == ' ' || source[end - 1] == '\t' ||
                            source[end - 1] == '\r' || source[end - 1] == '\n')) {
            --end;
        }
        auto start = end;
        while (start != 0 && identifierByte(static_cast<unsigned char>(source[start - 1]))) {
            --start;
        }
        if (start == end) {
            return Json(nullptr);
        }
        const auto &index = languageIndex(*analysis);
        const auto *occurrence = index.occurrenceAt(*sourceId, start);
        const auto *symbol = occurrence == nullptr ? nullptr : index.symbol(occurrence->symbol);
        if (symbol == nullptr) {
            return Json(nullptr);
        }
        std::size_t activeParameter{};
        depth = 0;
        for (auto offset = open + 1; offset < *requested; ++offset) {
            if (source[offset] == '(' || source[offset] == '[' || source[offset] == '{') {
                ++depth;
            } else if (source[offset] == ')' || source[offset] == ']' || source[offset] == '}') {
                if (depth != 0) {
                    --depth;
                }
            } else if (source[offset] == ',' && depth == 0) {
                ++activeParameter;
            }
        }
        return Json::object(
            {{"signatures", Json::array({Json::object({{"label", symbol->detail}})})},
             {"activeSignature", 0},
             {"activeParameter", static_cast<double>(activeParameter)}});
    }

    static std::optional<int> semanticTokenType(LanguageSymbolKind kind) {
        switch (kind) {
        case LanguageSymbolKind::Function:
            return 0;
        case LanguageSymbolKind::Method:
        case LanguageSymbolKind::ContractMethod:
            return 1;
        case LanguageSymbolKind::Struct:
            return 2;
        case LanguageSymbolKind::Field:
            return 3;
        case LanguageSymbolKind::Enum:
            return 4;
        case LanguageSymbolKind::EnumVariant:
            return 5;
        case LanguageSymbolKind::Contract:
            return 6;
        case LanguageSymbolKind::Attribute:
            return 7;
        case LanguageSymbolKind::Parameter:
            return 8;
        case LanguageSymbolKind::Local:
            return 9;
        }
        return std::nullopt;
    }

    [[nodiscard]] Json provideSemanticTokens(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json::object({{"data", Json(Json::Array{})}});
        }
        auto analysis = analyzeUri(*uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, *uri);
        if (analysis == nullptr || !sourceId.has_value()) {
            return Json::object({{"data", Json(Json::Array{})}});
        }
        const auto &index = languageIndex(*analysis);
        const auto &source = analysis->sources[*sourceId].contents;
        Json::Array data;
        Position previous;
        auto first = true;
        std::set<std::pair<std::size_t, std::size_t>> emitted;
        for (const auto &occurrence : index.occurrences()) {
            if (occurrence.span.source != *sourceId ||
                !emitted.emplace(occurrence.span.offset, occurrence.span.length).second) {
                continue;
            }
            const auto *symbol = index.symbol(occurrence.symbol);
            const auto tokenType = symbol == nullptr
                                       ? std::nullopt
                                       : semanticTokenType(symbol->id.kind);
            if (!tokenType.has_value()) {
                continue;
            }
            const auto position = positionAt(source, occurrence.span.offset);
            const auto deltaLine = first ? position.line : position.line - previous.line;
            const auto deltaStart = first || deltaLine != 0
                                        ? position.character
                                        : position.character - previous.character;
            data.push_back(static_cast<double>(deltaLine));
            data.push_back(static_cast<double>(deltaStart));
            data.push_back(static_cast<double>(occurrence.span.length));
            data.push_back(*tokenType);
            data.push_back(occurrence.definition ? 1 : 0);
            previous = position;
            first = false;
        }
        return Json::object({{"data", Json(std::move(data))}});
    }

    static std::vector<std::string> parameterNames(const ProjectAnalysis &analysis,
                                                   const CallTarget &target) {
        std::vector<std::string> result;
        if (target.kind == CallTargetKind::Function || target.kind == CallTargetKind::Method) {
            if (target.function >= analysis.program.functions.size()) {
                return result;
            }
            const auto &function = analysis.program.functions[target.function];
            const auto start = target.kind == CallTargetKind::Method ? std::size_t{1}
                                                                      : std::size_t{};
            for (std::size_t index = start; index < function.parameters.size(); ++index) {
                result.push_back(function.parameters[index].name);
            }
        } else if (target.kind == CallTargetKind::ContractMethod && analysis.semantic.has_value() &&
                   target.contract < analysis.semantic->contracts.size() &&
                   target.method < analysis.semantic->contracts[target.contract].methods.size()) {
            result = analysis.semantic->contracts[target.contract].methods[target.method]
                         .parameterNames;
        }
        return result;
    }

    static bool obviousArgument(const ProjectAnalysis &analysis, AstExpressionId argument,
                                std::string_view parameter) {
        if (argument >= analysis.program.expressions.size()) {
            return false;
        }
        const auto *name =
            std::get_if<NameExpression>(&analysis.program.expressions[argument].value);
        return name != nullptr && shortName(name->name) == parameter;
    }

    [[nodiscard]] Json provideInlayHints(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(Json::Array{});
        }
        auto analysis = analyzeUri(*uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, *uri);
        if (analysis == nullptr || !analysis->semantic.has_value() || !sourceId.has_value()) {
            return Json(Json::Array{});
        }
        const auto *range = params == nullptr ? nullptr : params->find("range");
        const auto startPosition = jsonPosition(range == nullptr ? nullptr : range->find("start"));
        const auto endPosition = jsonPosition(range == nullptr ? nullptr : range->find("end"));
        const auto &source = analysis->sources[*sourceId].contents;
        const auto start = startPosition.has_value() ? offsetAt(source, *startPosition)
                                                     : std::optional<std::size_t>{0};
        const auto end = endPosition.has_value() ? offsetAt(source, *endPosition)
                                                 : std::optional<std::size_t>{source.size()};
        if (!start.has_value() || !end.has_value()) {
            return Json(Json::Array{});
        }
        Json::Array result;
        for (std::size_t id = 0; id < analysis->program.expressions.size(); ++id) {
            if (!analysis->semantic->callTargets[id].has_value()) {
                continue;
            }
            const auto &target = *analysis->semantic->callTargets[id];
            const auto names = parameterNames(*analysis, target);
            const std::vector<AstExpressionId> *arguments{};
            const auto &value = analysis->program.expressions[id].value;
            if (const auto *call = std::get_if<CallExpression>(&value)) {
                arguments = &call->arguments;
            } else if (const auto *member = std::get_if<MemberExpression>(&value)) {
                arguments = &member->arguments;
            }
            if (arguments == nullptr) {
                continue;
            }
            const auto count = std::min(arguments->size(), names.size());
            for (std::size_t argument = 0; argument < count; ++argument) {
                const auto expression = (*arguments)[argument];
                if (expression >= analysis->program.expressions.size()) {
                    continue;
                }
                const auto span = analysis->program.expressions[expression].span;
                if (span.source != *sourceId || span.offset < *start || span.offset >= *end ||
                    names[argument] == "self" ||
                    obviousArgument(*analysis, expression, names[argument])) {
                    continue;
                }
                result.push_back(Json::object(
                    {{"position", lspPosition(positionAt(source, span.offset))},
                     {"label", names[argument] + ':'}, {"kind", 2}, {"paddingRight", true}}));
            }
        }
        return Json(std::move(result));
    }

    void sendDiagnostics(std::string uri, Json::Array diagnostics) {
        sendMessage(output_, Json::object(
                                 {{"jsonrpc", "2.0"},
                                  {"method", "textDocument/publishDiagnostics"},
                                  {"params", Json::object({{"uri", std::move(uri)},
                                                            {"diagnostics",
                                                             Json(std::move(diagnostics))}})}}));
    }

    void publishDiagnostics(const std::string &requestedUri) {
        const auto requested = documents_.find(requestedUri);
        if (requested == documents_.end()) {
            sendDiagnostics(requestedUri, {});
            publishedUris_.erase(requestedUri);
            return;
        }
        const auto root = analysisRoot(requested->second);
        const auto &analysis = analyzeRoot(root);
        std::map<std::string, Json::Array> grouped;
        grouped[requestedUri];
        for (const auto &[uri, document] : documents_) {
            if (analysisRoot(document) == root) {
                grouped[uri];
            }
        }
        for (const auto &diagnostic : analysis.diagnostics.all()) {
            if (diagnostic.span.source >= analysis.sources.size()) {
                grouped[requestedUri].push_back(
                    Json::object({{"range", lspRange(requested->second.contents, diagnostic.span)},
                                  {"severity", 1},
                                  {"code", diagnostic.code},
                                  {"source", "foundation"},
                                  {"message", diagnostic.message}}));
                continue;
            }
            const auto &source = analysis.sources[diagnostic.span.source];
            const auto uri = source.identity.empty() ? requestedUri : sourceUri(source.identity);
            grouped[uri].push_back(
                Json::object({{"range", lspRange(source.contents, diagnostic.span)},
                              {"severity", 1},
                              {"code", diagnostic.code},
                              {"source", "foundation"},
                              {"message", diagnostic.message}}));
        }
        std::vector<std::string> stale;
        for (const auto &uri : publishedUris_) {
            const auto path = fileUriToPath(uri);
            if (!grouped.contains(uri) && path.has_value() && containsPath(root, *path)) {
                stale.push_back(uri);
            }
        }
        for (const auto &uri : stale) {
            sendDiagnostics(uri, {});
            publishedUris_.erase(uri);
        }
        for (auto &[uri, diagnostics] : grouped) {
            sendDiagnostics(uri, std::move(diagnostics));
            publishedUris_.insert(uri);
        }
    }

    std::ostream &output_;
    std::ostream &errors_;
    std::vector<std::filesystem::path> workspaceRoots_;
    std::map<std::string, OpenDocument> documents_;
    mutable std::map<std::string, CachedAnalysis> analysisCache_;
    std::set<std::string> publishedUris_;
    bool initialized_{};
    bool shutdown_{};
    bool cleanExit_{};
};

} // namespace

int runLanguageServer(std::istream &input, std::ostream &output, std::ostream &errors) {
    LanguageServer server(output, errors);
    while (true) {
        std::string body;
        const auto status = readMessage(input, body);
        if (status == ReadStatus::End) {
            break;
        }
        if (status == ReadStatus::Invalid) {
            errors << "foundation-ls: invalid LSP message framing\n";
            return 1;
        }
        auto message = JsonParser(body).parse();
        if (!message.has_value()) {
            sendMessage(output, errorResponse(Json(nullptr), -32700, "parse error"));
            continue;
        }
        if (!server.handle(*message)) {
            break;
        }
    }
    return server.exitCode();
}

} // namespace foundation
