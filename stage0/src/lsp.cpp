#include "foundation/lsp.hpp"

#include "foundation/driver.hpp"
#include "foundation/formatter.hpp"
#include "foundation/language_service.hpp"
#include "foundation/lexer.hpp"
#include "foundation/package.hpp"

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
#include <tuple>
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

Json lspRange(Position start, Position end) {
    return Json::object({{"start", lspPosition(start)}, {"end", lspPosition(end)}});
}

Json lspRange(std::string_view source, SourceSpan span) {
    const auto start = positionAt(source, span.offset);
    const auto end = positionAt(source, span.offset + std::max<std::size_t>(span.length, 1));
    return Json::object({{"start", lspPosition(start)}, {"end", lspPosition(end)}});
}

struct TextLine {
    std::size_t offset{};
    std::size_t length{};
};

std::vector<TextLine> textLines(std::string_view source) {
    std::vector<TextLine> result;
    std::size_t start{};
    while (start <= source.size()) {
        const auto newline = source.find('\n', start);
        auto end = newline == std::string_view::npos ? source.size() : newline;
        if (end != start && source[end - 1] == '\r') {
            --end;
        }
        result.push_back({start, end - start});
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return result;
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
    if ((type.name == "[raw]" || type.name == "[raw-const]") &&
        type.arguments.size() == 1) {
        return std::string(type.name == "[raw]" ? "*" : "*const ") +
               displayTypeSyntax(type.arguments[0]);
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

std::string enumVariantDetail(const EnumVariant &variant) {
    auto result = variant.name;
    if (!variant.payloadType.has_value()) {
        return result;
    }
    result += '(';
    if (variant.payloadName.has_value()) {
        result += *variant.payloadName + ' ';
    }
    result += displayTypeSyntax(*variant.payloadType) + ')';
    return result;
}

std::string displaySemanticType(const ProjectAnalysis &analysis, const Type &type) {
    if (type.kind == TypeKind::Parameter) {
        return "T" + std::to_string(type.declaration);
    }
    if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
         type.kind == TypeKind::Edit) && type.arguments.size() == 1) {
        return std::string(typeName(type)) + ' ' +
               displaySemanticType(analysis, type.arguments.front());
    }
    if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
        type.arguments.size() == 1) {
        return std::string(type.kind == TypeKind::Raw ? "*" : "*const ") +
               displaySemanticType(analysis, type.arguments.front());
    }
    if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
        return '[' + std::to_string(type.declaration) + ']' +
               displaySemanticType(analysis, type.arguments.front());
    }
    if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
        return '[' + displaySemanticType(analysis, type.arguments.front()) + ']';
    }
    if (type.kind == TypeKind::Function && !type.arguments.empty()) {
        std::string result = "fn(";
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            if (index != 1) {
                result += ", ";
            }
            result += displaySemanticType(analysis, type.arguments[index]);
        }
        result += ") " + displaySemanticType(analysis, type.arguments.front());
        return result;
    }
    std::string result;
    if (type.kind == TypeKind::Struct && type.declaration < analysis.program.structs.size()) {
        result = shortName(analysis.program.structs[type.declaration].name);
    } else if (type.kind == TypeKind::Enum &&
               type.declaration < analysis.program.enums.size()) {
        result = shortName(analysis.program.enums[type.declaration].name);
    } else if (type.kind == TypeKind::Contract &&
               type.declaration < analysis.program.contracts.size()) {
        result = shortName(analysis.program.contracts[type.declaration].name);
    } else {
        result = typeName(type);
    }
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                result += ", ";
            }
            result += displaySemanticType(analysis, type.arguments[index]);
        }
        result += '>';
    }
    return result;
}

bool machineScalarName(std::string_view name) {
    return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
           name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "isize" || name == "usize" || name == "f32" || name == "f64";
}

std::optional<std::pair<AstExpressionId, SourceSpan>>
channelOperationAt(const ProjectAnalysis &analysis, std::size_t sourceId, std::size_t offset) {
    if (!analysis.semantic.has_value() || sourceId >= analysis.sources.size()) {
        return std::nullopt;
    }
    for (AstExpressionId id = 0;
         id < analysis.program.expressions.size() &&
         id < analysis.semantic->channelOperationTargets.size();
         ++id) {
        if (!analysis.semantic->channelOperationTargets[id].has_value()) {
            continue;
        }
        const auto &expression = analysis.program.expressions[id];
        const auto *member = std::get_if<MemberExpression>(&expression.value);
        if (member == nullptr || expression.span.source != sourceId) {
            continue;
        }
        const auto span = nameSpan(analysis.sources[sourceId].contents, expression.span,
                                   member->member);
        if (offset >= span.offset && offset <= span.offset + span.length) {
            return std::pair{id, span};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<AstExpressionId, SourceSpan>>
channelSenderCloneAt(const ProjectAnalysis &analysis, std::size_t sourceId,
                     std::size_t offset) {
    if (!analysis.semantic.has_value() || sourceId >= analysis.sources.size()) {
        return std::nullopt;
    }
    for (AstExpressionId id = 0;
         id < analysis.program.expressions.size() &&
         id < analysis.semantic->channelSenderClones.size();
         ++id) {
        if (!analysis.semantic->channelSenderClones[id]) {
            continue;
        }
        const auto &expression = analysis.program.expressions[id];
        const auto *member = std::get_if<MemberExpression>(&expression.value);
        if (member == nullptr || expression.span.source != sourceId) {
            continue;
        }
        const auto span = nameSpan(analysis.sources[sourceId].contents, expression.span,
                                   member->member);
        if (offset >= span.offset && offset <= span.offset + span.length) {
            return std::pair{id, span};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<AstExpressionId, SourceSpan>>
numericConversionAt(const ProjectAnalysis &analysis, std::size_t sourceId,
                    std::size_t offset) {
    if (!analysis.semantic.has_value() || sourceId >= analysis.sources.size()) {
        return std::nullopt;
    }
    for (AstExpressionId id = 0;
         id < analysis.program.expressions.size() && id < analysis.semantic->callTargets.size();
         ++id) {
        if (!analysis.semantic->callTargets[id].has_value() ||
            analysis.semantic->callTargets[id]->kind != CallTargetKind::NumericConversion) {
            continue;
        }
        const auto &expression = analysis.program.expressions[id];
        const auto *member = std::get_if<MemberExpression>(&expression.value);
        if (member == nullptr || expression.span.source != sourceId) {
            continue;
        }
        const auto span = nameSpan(analysis.sources[sourceId].contents, expression.span,
                                   member->member);
        if (offset >= span.offset && offset <= span.offset + span.length) {
            return std::pair{id, span};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<AstExpressionId, SourceSpan>>
emptyTestAt(const ProjectAnalysis &analysis, std::size_t sourceId, std::size_t offset) {
    if (!analysis.semantic.has_value() || sourceId >= analysis.sources.size()) {
        return std::nullopt;
    }
    for (AstExpressionId id = 0;
         id < analysis.program.expressions.size() &&
         id < analysis.semantic->emptyTests.size(); ++id) {
        if (!analysis.semantic->emptyTests[id]) {
            continue;
        }
        const auto &expression = analysis.program.expressions[id];
        const auto *unary = std::get_if<UnaryExpression>(&expression.value);
        if (unary == nullptr || unary->operation != UnaryOperator::Not ||
            expression.span.source != sourceId) {
            continue;
        }
        const SourceSpan span{sourceId, expression.span.offset, 1};
        if (offset >= span.offset && offset <= span.offset + span.length) {
            return std::pair{id, span};
        }
    }
    return std::nullopt;
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

std::string attributeDetail(const AttributeApplication &attribute) {
    auto result = '@' + shortName(attribute.name);
    if (attribute.parenthesized) {
        result += attribute.arguments.empty() ? "()" : "(...)";
    }
    return result;
}

std::string parameterDetail(const Parameter &parameter) {
    std::string result;
    for (const auto &attribute : parameter.attributes) {
        result += attributeDetail(attribute) + ' ';
    }
    if (parameter.mode == ParameterMode::Edit) {
        result += '&';
    } else if (parameter.mode == ParameterMode::Transfer) {
        result += '$';
    }
    result += parameter.name + ' ' + displayTypeSyntax(parameter.type);
    return result;
}

std::string receiverDetail(ReceiverKind receiver) {
    return receiver == ReceiverKind::View ? "self"
           : receiver == ReceiverKind::Edit ? "&self"
                                             : "$self";
}

std::string functionDetail(const Function &function) {
    auto prefix = function.task ? std::string("task ")
                                : function.cSymbol.has_value() ? std::string("extern c fn ")
                                                               : std::string("fn ");
    if (function.blocking) {
        prefix = "@blocking " + prefix;
    } else if (function.callback) {
        prefix = "@callback " + prefix;
    }
    for (auto attribute = function.attributes.rbegin();
         attribute != function.attributes.rend(); ++attribute) {
        if (attribute->name != "blocking" && attribute->name != "callback") {
            prefix = attributeDetail(*attribute) + ' ' + prefix;
        }
    }
    std::string result = prefix + shortName(function.name) +
                         typeParametersSuffix(function.typeParameters) + '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        const auto &parameter = function.parameters[index];
        if (function.receiver.has_value() && index == 0) {
            result += receiverDetail(*function.receiver);
        } else {
            result += parameterDetail(parameter);
        }
    }
    result += ") " + displayTypeSyntax(function.returnType);
    return result;
}

std::string callSnippet(std::string_view name, const std::vector<Parameter> &parameters,
                        std::size_t first = 0) {
    std::string result(name);
    result += '(';
    auto placeholder = std::size_t{1};
    for (std::size_t index = first; index < parameters.size(); ++index) {
        if (index != first) {
            result += ", ";
        }
        const auto &parameter = parameters[index];
        if (parameter.mode == ParameterMode::Edit) {
            result += '&';
        } else if (parameter.mode == ParameterMode::Transfer) {
            result += '$';
        } else if ((parameter.type.name == "own" || parameter.type.name == "view" ||
                    parameter.type.name == "edit") &&
            parameter.type.arguments.size() == 1) {
            result += parameter.type.name + ' ';
        }
        result += "${" + std::to_string(placeholder++) + ':' + parameter.name + '}';
    }
    result += ")$0";
    return result;
}

std::string functionCallSnippet(const Function &function, std::string_view name) {
    return callSnippet(name, function.parameters, function.receiver.has_value() ? 1U : 0U);
}

std::string contractMethodDetail(const ContractMethod &method) {
    std::string result = "fn " + method.name + '(';
    result += receiverDetail(method.receiver);
    for (const auto &parameter : method.parameters) {
        result += ", " + parameterDetail(parameter);
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
            if (function.ownerType.empty() || function.ownerType != owner.name) {
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
            item.children.push_back(
                {variant.name, enumVariantDetail(variant), 22, variant.span, {}});
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
        if (function.span.source != sourceId || !function.ownerType.empty() ||
            function.closure) {
            continue;
        }
        if (function.testName.has_value()) {
            result.push_back({*function.testName, "test \"" + *function.testName + "\"", 6,
                              function.testNameSpan.value_or(function.span), {}});
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

struct DelimiterRange {
    TokenKind opening{TokenKind::LeftBrace};
    SourceSpan span;
};

bool closes(TokenKind opening, TokenKind closing) {
    return (opening == TokenKind::LeftBrace && closing == TokenKind::RightBrace) ||
           (opening == TokenKind::LeftParen && closing == TokenKind::RightParen) ||
           (opening == TokenKind::LeftBracket && closing == TokenKind::RightBracket);
}

std::vector<DelimiterRange> delimiterRanges(std::string_view source,
                                            std::size_t sourceId) {
    Diagnostics diagnostics;
    Lexer lexer(source, diagnostics, sourceId);
    std::vector<Token> stack;
    std::vector<DelimiterRange> result;
    for (const auto &token : lexer.scan()) {
        if (token.kind == TokenKind::LeftBrace || token.kind == TokenKind::LeftParen ||
            token.kind == TokenKind::LeftBracket) {
            stack.push_back(token);
            continue;
        }
        if (token.kind != TokenKind::RightBrace && token.kind != TokenKind::RightParen &&
            token.kind != TokenKind::RightBracket) {
            continue;
        }
        if (stack.empty() || !closes(stack.back().kind, token.kind)) {
            continue;
        }
        const auto opening = stack.back();
        stack.pop_back();
        result.push_back({opening.kind,
                          {opening.span.offset,
                           token.span.offset + token.span.length - opening.span.offset,
                           opening.span.line, opening.span.column, sourceId}});
    }
    return result;
}

std::size_t declarationPrefixStart(std::string_view source, std::size_t offset) {
    auto start = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    start = start == std::string_view::npos ? 0 : start + 1;
    while (start != 0) {
        const auto previousEnd = start - 1;
        const auto previousStart = source.rfind(
            '\n', previousEnd == 0 ? 0 : previousEnd - 1);
        const auto lineStart = previousStart == std::string_view::npos ? 0 : previousStart + 1;
        auto line = source.substr(lineStart, previousEnd - lineStart);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (line.starts_with("//") || line.starts_with('@')) {
            start = lineStart;
            continue;
        }
        break;
    }
    return start;
}

SourceSpan codeLensAnchor(std::string_view source, SourceSpan declaration) {
    const auto start = declarationPrefixStart(source, declaration.offset);
    const auto position = positionAt(source, start);
    return {start, 0, position.line + 1, position.character + 1, declaration.source};
}

std::optional<SourceSpan> declarationExtent(std::string_view source,
                                            SourceSpan declaration) {
    const auto delimiters = delimiterRanges(source, declaration.source);
    const DelimiterRange *body = nullptr;
    for (const auto &candidate : delimiters) {
        if (candidate.opening != TokenKind::LeftBrace ||
            candidate.span.offset < declaration.offset) {
            continue;
        }
        if (body == nullptr || candidate.span.offset < body->span.offset) {
            body = &candidate;
        }
    }
    if (body == nullptr) {
        return std::nullopt;
    }
    const auto start = declarationPrefixStart(source, declaration.offset);
    const auto end = body->span.offset + body->span.length;
    const auto position = positionAt(source, start);
    return SourceSpan{start, end - start, position.line + 1, position.character + 1,
                      declaration.source};
}

std::optional<SourceSpan> tokenAt(std::string_view source, std::size_t offset,
                                  std::size_t sourceId) {
    Diagnostics diagnostics;
    Lexer lexer(source, diagnostics, sourceId);
    for (const auto &token : lexer.scan()) {
        if (token.kind == TokenKind::Eof) {
            break;
        }
        if (token.span.offset <= offset && offset < token.span.offset + token.span.length) {
            return token.span;
        }
    }
    return std::nullopt;
}

struct CompletionAccess {
    std::size_t dot{};
    std::size_t receiverEnd{};
    std::size_t suffixEnd{};
};

std::optional<CompletionAccess> completionAccess(std::string_view source,
                                                 std::size_t offset) {
    if (offset > source.size()) {
        return std::nullopt;
    }
    auto prefix = offset;
    while (prefix != 0 &&
           identifierByte(static_cast<unsigned char>(source[prefix - 1]))) {
        --prefix;
    }
    if (prefix == 0 || source[prefix - 1] != '.') {
        return std::nullopt;
    }
    auto receiverEnd = prefix - 1;
    while (receiverEnd != 0 &&
           (source[receiverEnd - 1] == ' ' || source[receiverEnd - 1] == '\t')) {
        --receiverEnd;
    }
    if (receiverEnd == 0) {
        return std::nullopt;
    }
    auto suffixEnd = offset;
    while (suffixEnd < source.size() &&
           identifierByte(static_cast<unsigned char>(source[suffixEnd]))) {
        ++suffixEnd;
    }
    return CompletionAccess{prefix - 1, receiverEnd, suffixEnd};
}

const DelimiterRange *completionBlockRange(const Program &program, AstBlockId id,
                                           const std::vector<DelimiterRange> &delimiters) {
    if (id >= program.blocks.size()) {
        return nullptr;
    }
    const auto opening = program.blocks[id].span.offset;
    const auto source = program.blocks[id].span.source;
    const auto found = std::find_if(delimiters.begin(), delimiters.end(),
                                    [opening, source](const auto &delimiter) {
                                        return delimiter.opening == TokenKind::LeftBrace &&
                                               delimiter.span.offset == opening &&
                                               delimiter.span.source == source;
                                    });
    return found == delimiters.end() ? nullptr : &*found;
}

std::optional<std::size_t> completionFunction(
    const Program &program, std::size_t sourceId, std::size_t offset,
    const std::vector<DelimiterRange> &delimiters) {
    std::optional<std::size_t> result;
    for (std::size_t function = 0; function < program.functions.size(); ++function) {
        const auto &candidate = program.functions[function];
        const auto *range = candidate.hasBody
                                ? completionBlockRange(program, candidate.body, delimiters)
                                : nullptr;
        if (range == nullptr || range->span.source != sourceId ||
            offset < range->span.offset ||
            offset >= range->span.offset + range->span.length) {
            continue;
        }
        const auto *selected = result.has_value()
                                   ? completionBlockRange(program,
                                                          program.functions[*result].body,
                                                          delimiters)
                                   : nullptr;
        if (selected == nullptr || range->span.length < selected->span.length) {
            result = function;
        }
    }
    return result;
}

bool completionBlockPath(const Program &program, AstBlockId id,
                         std::size_t sourceId, std::size_t offset,
                         const std::vector<DelimiterRange> &delimiters,
                         std::vector<AstBlockId> &path) {
    const auto *range = completionBlockRange(program, id, delimiters);
    if (range == nullptr || range->span.source != sourceId ||
        offset < range->span.offset || offset >= range->span.offset + range->span.length) {
        return false;
    }
    path.push_back(id);
    for (const auto statementId : program.blocks[id].statements) {
        if (statementId >= program.statements.size()) {
            continue;
        }
        const auto &value = program.statements[statementId].value;
        if (const auto *variable = std::get_if<VariableStatement>(&value);
            variable != nullptr && variable->elseBlock.has_value() &&
            completionBlockPath(program, *variable->elseBlock, sourceId, offset,
                                delimiters, path)) {
            return true;
        }
        if (const auto *resultElse = std::get_if<ResultElseStatement>(&value);
            resultElse != nullptr &&
            completionBlockPath(program, resultElse->elseBlock, sourceId, offset,
                                delimiters, path)) {
            return true;
        }
        if (const auto *branch = std::get_if<IfStatement>(&value); branch != nullptr) {
            if (completionBlockPath(program, branch->thenBlock, sourceId, offset,
                                    delimiters, path) ||
                (branch->elseBlock.has_value() &&
                 completionBlockPath(program, *branch->elseBlock, sourceId, offset,
                                     delimiters, path))) {
                return true;
            }
        }
        if (const auto *loop = std::get_if<WhileStatement>(&value);
            loop != nullptr &&
            completionBlockPath(program, loop->body, sourceId, offset, delimiters, path)) {
            return true;
        }
        if (const auto *loop = std::get_if<ForStatement>(&value);
            loop != nullptr &&
            completionBlockPath(program, loop->body, sourceId, offset, delimiters, path)) {
            return true;
        }
        if (const auto *selection = std::get_if<SelectStatement>(&value);
            selection != nullptr) {
            for (const auto &operation : selection->operations) {
                if (completionBlockPath(program, operation.body, sourceId, offset,
                                        delimiters, path)) {
                    return true;
                }
            }
            if (selection->timeout.has_value() &&
                completionBlockPath(program, selection->timeout->body, sourceId, offset,
                                    delimiters, path)) {
                return true;
            }
            if (completionBlockPath(program, selection->errorBlock, sourceId, offset,
                                    delimiters, path)) {
                return true;
            }
        }
    }
    return true;
}

std::size_t completionExpressionEnd(
    const Program &program, AstExpressionId id,
    const std::vector<DelimiterRange> &delimiters) {
    if (id >= program.expressions.size()) {
        return 0;
    }
    const auto &expression = program.expressions[id];
    auto end = expression.span.offset + expression.span.length;
    const auto include = [&](AstExpressionId child) {
        end = std::max(end, completionExpressionEnd(program, child, delimiters));
    };
    std::visit(
        [&](const auto &value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, ArrayExpression>) {
                for (const auto child : value.elements) {
                    include(child);
                }
            } else if constexpr (std::is_same_v<Value, UnaryExpression> ||
                                 std::is_same_v<Value, OwnershipExpression>) {
                include(value.operand);
            } else if constexpr (std::is_same_v<Value, BinaryExpression>) {
                include(value.left);
                include(value.right);
            } else if constexpr (std::is_same_v<Value, CallExpression>) {
                for (const auto child : value.arguments) {
                    include(child);
                }
            } else if constexpr (std::is_same_v<Value, StructExpression>) {
                for (const auto &field : value.fields) {
                    include(field.value);
                }
            } else if constexpr (std::is_same_v<Value, MemberExpression>) {
                if (value.base.has_value()) {
                    include(*value.base);
                }
                for (const auto child : value.arguments) {
                    include(child);
                }
            } else if constexpr (std::is_same_v<Value, IndexExpression>) {
                include(value.base);
                include(value.index);
            } else if constexpr (std::is_same_v<Value, ReplaceExpression>) {
                include(value.target);
                include(value.value);
            } else if constexpr (std::is_same_v<Value, MatchExpression>) {
                include(value.value);
                for (const auto &arm : value.arms) {
                    include(arm.expression);
                }
            } else if constexpr (std::is_same_v<Value, ConditionalExpression>) {
                include(value.condition);
                include(value.thenValue);
                include(value.elseValue);
                if (const auto *range = completionBlockRange(
                        program, value.thenBlock, delimiters)) {
                    end = std::max(end, range->span.offset + range->span.length);
                }
                if (const auto *range = completionBlockRange(
                        program, value.elseBlock, delimiters)) {
                    end = std::max(end, range->span.offset + range->span.length);
                }
            } else if constexpr (std::is_same_v<Value, FunctionExpression>) {
                if (value.function < program.functions.size()) {
                    const auto &function = program.functions[value.function];
                    if (function.hasBody) {
                        if (const auto *range = completionBlockRange(
                                program, function.body, delimiters)) {
                            end = std::max(end,
                                           range->span.offset + range->span.length);
                        }
                    }
                }
            }
        },
        expression.value);
    return end;
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
        } else if (*method == "textDocument/declaration" && id != nullptr) {
            sendMessage(output_, response(*id, provideDefinition(message.find("params"))));
        } else if (*method == "textDocument/definition" && id != nullptr) {
            sendMessage(output_, response(*id, provideDefinition(message.find("params"))));
        } else if (*method == "textDocument/typeDefinition" && id != nullptr) {
            sendMessage(output_, response(*id, provideTypeDefinition(message.find("params"))));
        } else if (*method == "textDocument/implementation" && id != nullptr) {
            sendMessage(output_, response(*id, provideImplementations(message.find("params"))));
        } else if (*method == "textDocument/documentHighlight" && id != nullptr) {
            sendMessage(output_, response(*id, provideDocumentHighlights(message.find("params"))));
        } else if (*method == "textDocument/codeLens" && id != nullptr) {
            sendMessage(output_, response(*id, provideCodeLenses(message.find("params"))));
        } else if (*method == "foundation/compositeType" && id != nullptr) {
            sendMessage(output_, response(*id, provideCompositeType(message.find("params"))));
        } else if (*method == "textDocument/prepareTypeHierarchy" && id != nullptr) {
            sendMessage(output_, response(*id, prepareTypeHierarchy(message.find("params"))));
        } else if (*method == "typeHierarchy/supertypes" && id != nullptr) {
            sendMessage(output_, response(*id, provideTypeHierarchySupertypes(
                                                   message.find("params"))));
        } else if (*method == "typeHierarchy/subtypes" && id != nullptr) {
            sendMessage(output_, response(*id, provideTypeHierarchySubtypes(
                                                   message.find("params"))));
        } else if (*method == "textDocument/prepareCallHierarchy" && id != nullptr) {
            sendMessage(output_, response(*id, prepareCallHierarchy(message.find("params"))));
        } else if (*method == "callHierarchy/incomingCalls" && id != nullptr) {
            sendMessage(output_, response(*id, provideIncomingCalls(message.find("params"))));
        } else if (*method == "callHierarchy/outgoingCalls" && id != nullptr) {
            sendMessage(output_, response(*id, provideOutgoingCalls(message.find("params"))));
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
        } else if (*method == "textDocument/codeAction" && id != nullptr) {
            sendMessage(output_, response(*id, provideCodeActions(message.find("params"))));
        } else if (*method == "textDocument/foldingRange" && id != nullptr) {
            sendMessage(output_, response(*id, provideFoldingRanges(message.find("params"))));
        } else if (*method == "textDocument/selectionRange" && id != nullptr) {
            sendMessage(output_, response(*id, provideSelectionRanges(message.find("params"))));
        } else if (*method == "textDocument/formatting" && id != nullptr) {
            sendMessage(output_, response(*id, provideFormatting(message.find("params"))));
        } else if (*method == "textDocument/rangeFormatting" && id != nullptr) {
            sendMessage(output_, response(*id, provideRangeFormatting(message.find("params"))));
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
                            {"declarationProvider", true},
                            {"definitionProvider", true},
                            {"typeDefinitionProvider", true},
                            {"implementationProvider", true},
                            {"documentHighlightProvider", true},
                            {"codeLensProvider",
                             Json::object({{"resolveProvider", false}})},
                            {"typeHierarchyProvider", true},
                            {"callHierarchyProvider", true},
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
                            {"codeActionProvider",
                             Json::object({{"codeActionKinds", Json::array({"quickfix"})}})},
                            {"foldingRangeProvider", true},
                            {"selectionRangeProvider", true},
                            {"documentFormattingProvider", true},
                            {"documentRangeFormattingProvider", true},
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

    [[nodiscard]] std::filesystem::path analysisRoot(
        const std::filesystem::path &path) const {
        const std::filesystem::path *best{};
        for (const auto &root : workspaceRoots_) {
            if (containsPath(root, path) &&
                (best == nullptr || root.generic_string().size() > best->generic_string().size())) {
                best = &root;
            }
        }
        if (best != nullptr) {
            if (const auto manifest = discoverPackageManifest(path);
                manifest.has_value() && containsPath(*best, manifest->parent_path())) {
                return normalizedPath(manifest->parent_path());
            }
            return *best;
        }
        if (const auto manifest = discoverPackageManifest(path); manifest.has_value()) {
            return normalizedPath(manifest->parent_path());
        }
        return path.parent_path();
    }

    [[nodiscard]] std::filesystem::path analysisRoot(const OpenDocument &document) const {
        return analysisRoot(document.path);
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

    [[nodiscard]] ProjectAnalysis analyzeCompletion(
        const OpenDocument &document, const CompletionAccess &access,
        std::optional<std::size_t> receiverStart = std::nullopt) const {
        auto source = document.contents;
        const auto end = std::min(access.suffixEnd, source.size());
        const auto start = std::min(receiverStart.value_or(access.dot), end);
        for (auto offset = start; offset < end; ++offset) {
            if (source[offset] != '\r' && source[offset] != '\n') {
                source[offset] = ' ';
            }
        }
        auto inputs = overlays();
        auto replaced = false;
        for (auto &input : inputs) {
            if (normalizedPath(input.path) == document.path) {
                input.contents = source;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            inputs.push_back({document.path, std::move(source)});
        }
        return analyzeProject(analysisRoot(document), inputs,
                              AnalyzeOptions{.requireMain = false,
                                             .retainInvalidModel = true});
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
        std::vector<std::filesystem::path> roots;
        const auto addRoot = [&roots](const std::filesystem::path &root) {
            if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
                roots.push_back(root);
            }
        };
        for (const auto &workspace : workspaceRoots_) {
            auto hasDocument = false;
            for (const auto &[uri, document] : documents_) {
                static_cast<void>(uri);
                if (containsPath(workspace, document.path)) {
                    addRoot(analysisRoot(document));
                    hasDocument = true;
                }
            }
            if (!hasDocument) {
                addRoot(workspace);
            }
        }
        if (workspaceRoots_.empty()) {
            for (const auto &[uri, document] : documents_) {
                static_cast<void>(uri);
                addRoot(analysisRoot(document));
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

    static const ImportDeclaration *importAt(const ProjectAnalysis &analysis,
                                             std::size_t sourceId,
                                             std::size_t offset) {
        for (const auto &imported : analysis.program.imports) {
            if (imported.span.source == sourceId && imported.span.offset <= offset &&
                offset <= imported.span.offset + imported.span.length) {
                return &imported;
            }
        }
        return nullptr;
    }

    static SourceSpan packageNameSpan(const DiagnosticSource &source,
                                      std::size_t sourceId) {
        std::size_t lineStart{};
        while (lineStart < source.contents.size()) {
            const auto lineEnd = source.contents.find('\n', lineStart);
            const auto limit = lineEnd == std::string::npos ? source.contents.size() : lineEnd;
            auto offset = lineStart;
            while (offset < limit &&
                   (source.contents[offset] == ' ' || source.contents[offset] == '\t' ||
                    source.contents[offset] == '\r')) {
                ++offset;
            }
            constexpr std::string_view keyword = "package";
            if (source.contents.substr(offset, keyword.size()) == keyword) {
                offset += keyword.size();
                while (offset < limit &&
                       (source.contents[offset] == ' ' || source.contents[offset] == '\t' ||
                        source.contents[offset] == '\r')) {
                    ++offset;
                }
                if (source.contents.substr(offset, source.packageName.size()) ==
                    source.packageName) {
                    return {offset, source.packageName.size(), 1, 1, sourceId};
                }
            }
            if (lineEnd == std::string::npos) {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return {0, 0, 1, 1, sourceId};
    }

    [[nodiscard]] Json packageLocations(const ProjectAnalysis &analysis,
                                        const ImportDeclaration &imported) const {
        Json::Array result;
        for (std::size_t sourceId = 0; sourceId < analysis.sources.size(); ++sourceId) {
            const auto &source = analysis.sources[sourceId];
            if (source.packageName != imported.packageName || source.identity.empty()) {
                continue;
            }
            result.push_back(Json::object(
                {{"uri", sourceUri(source.identity)},
                 {"range", lspRange(source.contents, packageNameSpan(source, sourceId))}}));
        }
        return Json(std::move(result));
    }

    static bool nominalTypeSymbol(LanguageSymbolKind kind) {
        return kind == LanguageSymbolKind::Struct || kind == LanguageSymbolKind::Enum ||
               kind == LanguageSymbolKind::Contract;
    }

    void appendTypeTarget(const ProjectAnalysis &analysis,
                          const LanguageSymbol *symbol,
                          std::set<std::tuple<int, std::size_t, std::size_t>> &seen,
                          Json::Array &result) const {
        if (symbol == nullptr || !nominalTypeSymbol(symbol->id.kind) ||
            symbol->definition.source >= analysis.sources.size()) {
            return;
        }
        const auto key = std::tuple{static_cast<int>(symbol->id.kind), symbol->id.owner,
                                    symbol->id.member};
        if (!seen.insert(key).second) {
            return;
        }
        const auto &source = analysis.sources[symbol->definition.source];
        result.push_back(Json::object(
            {{"label", symbol->name},
             {"uri", sourceUri(source.identity)},
             {"position", lspPosition(positionAt(source.contents,
                                                   symbol->definition.offset))}}));
    }

    void appendTypeTargets(const ProjectAnalysis &analysis,
                           const LanguageIndex &index, const TypeSyntax &type,
                           std::set<std::tuple<int, std::size_t, std::size_t>> &seen,
                           Json::Array &result) const {
        const auto *occurrence = index.occurrenceAt(type.span.source, type.span.offset);
        appendTypeTarget(analysis,
                         occurrence == nullptr ? nullptr : index.symbol(occurrence->symbol),
                         seen, result);
        for (const auto &argument : type.arguments) {
            appendTypeTargets(analysis, index, argument, seen, result);
        }
    }

    [[nodiscard]] Json::Array typeTargets(const ProjectAnalysis &analysis,
                                          const LanguageIndex &index,
                                          const TypeSyntax &type) const {
        Json::Array result;
        std::set<std::tuple<int, std::size_t, std::size_t>> seen;
        appendTypeTargets(analysis, index, type, seen, result);
        return result;
    }

    [[nodiscard]] Json::Array typeTargets(const ProjectAnalysis &analysis,
                                          const LanguageIndex &index,
                                          const LanguageSymbol &symbol) const {
        Json::Array result;
        std::set<std::tuple<int, std::size_t, std::size_t>> seen;
        const auto appendParameters = [&](const std::vector<Parameter> &parameters,
                                          std::size_t first = 0) {
            for (auto parameter = first; parameter < parameters.size(); ++parameter) {
                appendTypeTargets(analysis, index, parameters[parameter].type, seen, result);
            }
        };
        if ((symbol.id.kind == LanguageSymbolKind::Function ||
             symbol.id.kind == LanguageSymbolKind::Method) &&
            symbol.id.owner < analysis.program.functions.size()) {
            const auto &function = analysis.program.functions[symbol.id.owner];
            appendParameters(function.parameters, function.receiver.has_value() ? 1U : 0U);
            appendTypeTargets(analysis, index, function.returnType, seen, result);
        } else if (symbol.id.kind == LanguageSymbolKind::ContractMethod &&
                   symbol.id.owner < analysis.program.contracts.size() &&
                   symbol.id.member <
                       analysis.program.contracts[symbol.id.owner].methods.size()) {
            const auto &method =
                analysis.program.contracts[symbol.id.owner].methods[symbol.id.member];
            appendParameters(method.parameters);
            appendTypeTargets(analysis, index, method.returnType, seen, result);
        } else if (symbol.id.kind == LanguageSymbolKind::Attribute &&
                   symbol.id.owner < analysis.program.attributeDeclarations.size()) {
            appendParameters(analysis.program.attributeDeclarations[symbol.id.owner].parameters);
        } else {
            appendTypeTarget(analysis, index.typeDefinition(symbol.id), seen, result);
        }
        return result;
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
        const auto sourceId = sourceIdForUri(*analysis, *uri);
        const auto position = requestPosition(params);
        if (sourceId.has_value() && position.has_value()) {
            const auto &source = analysis->sources[*sourceId];
            const auto offset = offsetAt(source.contents, *position);
            const auto *imported = offset.has_value()
                                       ? importAt(*analysis, *sourceId, *offset)
                                       : nullptr;
            if (imported != nullptr) {
                auto sourceCount = std::size_t{};
                for (const auto &candidate : analysis->sources) {
                    sourceCount += candidate.packageName == imported->packageName ? 1U : 0U;
                }
                auto detail = "package " + imported->packageName;
                if (!imported->alias.empty()) {
                    detail += " as " + imported->alias;
                }
                detail += "\n\n" + std::to_string(sourceCount) +
                          (sourceCount == 1 ? " source file" : " source files");
                return Json::object(
                    {{"contents",
                      Json::object({{"kind", "markdown"},
                                    {"value", "```foundation\n" + detail + "\n```"}})},
                         {"range", lspRange(source.contents, imported->span)}});
            }
            if (const auto found = wordAt(source.contents, *position, *sourceId);
                found.has_value()) {
                const auto keyword = source.contents.substr(found->offset, found->length);
                std::string documentation;
                if (keyword == "select") {
                    documentation =
                        "```foundation\nselect { ... }\n```\n\nSuspends the current task until a "
                        "typed channel operation is ready. Ready branches use source order. "
                        "Close and cancellation enter `else error`; timeout uses a monotonic "
                        "deadline.";
                } else if (keyword == "timeout") {
                    documentation =
                        "```foundation\ntimeout 5.seconds: action\n```\n\nRuns only when no "
                        "channel operation completes before the monotonic duration. Supported "
                        "units are `seconds`, `milliseconds`, `microseconds`, and "
                        "`nanoseconds`.";
                } else if (keyword == "blocking" && found->offset != 0 &&
                           source.contents[found->offset - 1] == '@') {
                    documentation =
                        "```foundation\n@blocking\nextern c fn read() i32 as native_read\n```\n\n"
                        "Runs a bodyless C ABI import on the bounded native worker pool. The "
                        "call is a suspension point and is valid only as a standalone binding "
                        "or discard inside a task.";
                } else if (keyword == "callback" && found->offset != 0 &&
                           source.contents[found->offset - 1] == '@') {
                    documentation =
                        "```foundation\n@callback(cancel = native_cancel)\nextern c fn "
                        "read(&result i32) i32 as native_read_start\n```\n\nRegisters "
                        "a bodyless C ABI import with the native completion reactor. The start "
                        "symbol receives an operation token after its declared arguments and "
                        "completes it exactly once with an `i32` status. The optional cancel "
                        "symbol receives the same token. Calls are valid only as a standalone "
                        "binding or discard inside a task.";
                } else if (keyword == "unsafe") {
                    documentation =
                        "```foundation\n// SAFETY: proof\nunsafe { ... }\n```\n\nBounds raw "
                        "pointer construction, arithmetic, dereference, slice storage access, "
                        "and C ABI calls whose signature contains a raw pointer. Type checking "
                        "and ownership rules remain active.";
                } else if (keyword == "null") {
                    documentation =
                        "```foundation\nfn null<P>() P\n```\n\nConstructs a null raw pointer "
                        "of the explicit pointer type, for example `null<*void>()`. This is "
                        "available only inside `unsafe`.";
                } else if (keyword == "isNull") {
                    documentation =
                        "```foundation\nfn isNull(pointer P) bool\n```\n\nChecks a raw pointer "
                        "without dereferencing it and is safe outside `unsafe`.";
                } else if (keyword == "print") {
                    documentation =
                        "```foundation\nfn print(value String) void\n```\n\nWrites one UTF-8 "
                        "String followed by a newline. The call reads its argument without "
                        "consuming an owned String.";
                } else if (keyword == "panic") {
                    documentation =
                        "```foundation\nfn panic(message String) never\n```\n\nTerminates the "
                        "program with a complete Foundation source trace. Panic does not unwind "
                        "or run drop glue; recoverable failures use `Result<T, E>`.";
                } else if (keyword == "len") {
                    documentation =
                        "```foundation\nfn len(value String | [N]T | [T]) usize\n```\n\nReturns "
                        "the encoded byte length of a String or the element count of an array or "
                        "slice. The call reads its argument without consuming it.";
                } else if (keyword == "channel") {
                    documentation =
                        "```foundation\nfn channel<T>(capacity u64) Channel<T>\n```\n\nCreates "
                        "owned directional sender and receiver endpoints. Capacity zero creates "
                        "a rendezvous channel; a positive capacity creates a bounded FIFO.";
                }
                if (!documentation.empty()) {
                    return Json::object(
                        {{"contents",
                          Json::object({{"kind", "markdown"}, {"value", documentation}})},
                         {"range", lspRange(source.contents, *found)}});
                }
            }
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
        if (symbol == nullptr) {
            if (sourceId.has_value() && position.has_value() &&
                analysis->semantic.has_value()) {
                const auto &source = analysis->sources[*sourceId].contents;
                const auto offset = offsetAt(source, *position);
                const auto emptyTest = offset.has_value()
                                           ? emptyTestAt(*analysis, *sourceId, *offset)
                                           : std::nullopt;
                if (emptyTest.has_value()) {
                    const auto id = emptyTest->first;
                    const auto &unary =
                        std::get<UnaryExpression>(analysis->program.expressions[id].value);
                    const auto operandType =
                        analysis->semantic->expressionTypes[unary.operand];
                    return Json::object(
                        {{"contents",
                          Json::object(
                              {{"kind", "markdown"},
                               {"value", "```foundation\n!" +
                                             displaySemanticType(*analysis, operandType) +
                                             " bool\n```\n\nTests whether the value is empty. "
                                             "It does not test absence or truthiness."}})},
                         {"range", lspRange(source, emptyTest->second)}});
                }
                const auto conversion = offset.has_value()
                                            ? numericConversionAt(*analysis, *sourceId, *offset)
                                            : std::nullopt;
                if (conversion.has_value()) {
                    const auto id = conversion->first;
                    const auto &target = *analysis->semantic->callTargets[id];
                    if (target.typeArguments.size() == 2) {
                        const auto detail =
                            "fn From(value " +
                            displaySemanticType(*analysis, target.typeArguments[0]) + ") " +
                            displaySemanticType(*analysis,
                                                analysis->semantic->expressionTypes[id]);
                        return Json::object(
                            {{"contents",
                              Json::object(
                                  {{"kind", "markdown"},
                                   {"value", "```foundation\n" + detail +
                                                 "\n```\n\nPerforms an explicit numeric "
                                                 "conversion. A conversion that can lose "
                                                 "information returns `Result<T, "
                                                 "NumberError>`."}})},
                             {"range", lspRange(source, conversion->second)}});
                    }
                }
                const auto senderClone =
                    offset.has_value()
                        ? channelSenderCloneAt(*analysis, *sourceId, *offset)
                        : std::nullopt;
                if (senderClone.has_value()) {
                    const auto id = senderClone->first;
                    const auto detail =
                        "fn clone() " + displaySemanticType(
                                            *analysis,
                                            analysis->semantic->expressionTypes[id]);
                    return Json::object(
                        {{"contents",
                          Json::object(
                              {{"kind", "markdown"},
                               {"value", "```foundation\n" + detail +
                                             "\n```\n\nCreates another owned sender handle "
                                             "for the same channel."}})},
                         {"range", lspRange(source, senderClone->second)}});
                }
                const auto operation = offset.has_value()
                                           ? channelOperationAt(*analysis, *sourceId, *offset)
                                           : std::nullopt;
                if (operation.has_value()) {
                    const auto id = operation->first;
                    const auto &target =
                        *analysis->semantic->channelOperationTargets[id];
                    const auto &expression = analysis->program.expressions[id];
                    const auto &member = std::get<MemberExpression>(expression.value);
                    const auto endpointType =
                        member.base.has_value()
                            ? analysis->semantic->expressionTypes[*member.base]
                            : invalidType;
                    const auto resultType = analysis->semantic->expressionTypes[id];
                    auto detail = std::string("fn ") + member.member + '(';
                    if (target.kind == ChannelOperationKind::Send &&
                        endpointType.arguments.size() == 1 &&
                        endpointType.arguments.front() != voidType) {
                        detail += "value " +
                                  displaySemanticType(*analysis,
                                                      endpointType.arguments.front());
                    }
                    detail += ") " + displaySemanticType(*analysis, resultType);
                    const auto documentation =
                        target.kind == ChannelOperationKind::Send
                            ? "Suspends this task until ownership transfers. A closed or "
                              "cancelled operation returns `ChannelError`; a failed send drops "
                              "the value."
                            : "Suspends this task until a value arrives. Channel close and "
                              "structured cancellation return a typed `ChannelError`.";
                    return Json::object(
                        {{"contents",
                          Json::object({{"kind", "markdown"},
                                        {"value", "```foundation\n" + detail +
                                                      "\n```\n\n" + documentation}})},
                         {"range", lspRange(source, operation->second)}});
                }
            }
            return Json(nullptr);
        }
        if (!sourceId.has_value()) {
            return Json(nullptr);
        }
        auto value = "```foundation\n" + symbol->detail + "\n```";
        if (!symbol->documentation.empty()) {
            value += "\n\n" + symbol->documentation;
        }
        const auto appendDocumentedMember = [&value](std::string_view heading,
                                                     std::string_view name,
                                                     std::string documentation) {
            if (documentation.empty()) {
                return;
            }
            if (value.find(std::string("\n\n**") + std::string(heading) + "**") ==
                std::string::npos) {
                value += "\n\n**" + std::string(heading) + "**";
            }
            std::replace(documentation.begin(), documentation.end(), '\n', ' ');
            value += "\n\n- `" + std::string(name) + "`: " + documentation;
        };
        if ((symbol->id.kind == LanguageSymbolKind::Function ||
             symbol->id.kind == LanguageSymbolKind::Method) &&
            symbol->id.owner < analysis->program.functions.size()) {
            const auto &function = analysis->program.functions[symbol->id.owner];
            const auto first = function.receiver.has_value() ? 1U : 0U;
            for (std::size_t parameter = first; parameter < function.parameters.size();
                 ++parameter) {
                const auto &declaration = function.parameters[parameter];
                appendDocumentedMember(
                    "Parameters", declaration.name,
                    languageParameterDocumentation(*analysis, declaration));
            }
        } else if (symbol->id.kind == LanguageSymbolKind::ContractMethod &&
                   symbol->id.owner < analysis->program.contracts.size() &&
                   symbol->id.member <
                       analysis->program.contracts[symbol->id.owner].methods.size()) {
            const auto &method =
                analysis->program.contracts[symbol->id.owner].methods[symbol->id.member];
            for (const auto &parameter : method.parameters) {
                appendDocumentedMember(
                    "Parameters", parameter.name,
                    languageParameterDocumentation(*analysis, parameter));
            }
        } else if (symbol->id.kind == LanguageSymbolKind::Attribute &&
                   symbol->id.owner < analysis->program.attributeDeclarations.size()) {
            for (const auto &parameter :
                 analysis->program.attributeDeclarations[symbol->id.owner].parameters) {
                appendDocumentedMember(
                    "Parameters", parameter.name,
                    languageParameterDocumentation(*analysis, parameter));
            }
        } else if (symbol->id.kind == LanguageSymbolKind::Struct &&
                   symbol->id.owner < analysis->program.structs.size()) {
            for (const auto &field : analysis->program.structs[symbol->id.owner].fields) {
                appendDocumentedMember("Fields", field.name,
                                       languageDocumentation(*analysis, field.span));
            }
        } else if (symbol->id.kind == LanguageSymbolKind::Enum &&
                   symbol->id.owner < analysis->program.enums.size()) {
            for (const auto &variant : analysis->program.enums[symbol->id.owner].variants) {
                appendDocumentedMember("Variants", variant.name,
                                       languageDocumentation(*analysis, variant.span));
            }
        } else if (symbol->id.kind == LanguageSymbolKind::Contract &&
                   symbol->id.owner < analysis->program.contracts.size()) {
            for (const auto &method : analysis->program.contracts[symbol->id.owner].methods) {
                appendDocumentedMember("Methods", method.name,
                                       languageDocumentation(*analysis, method.span));
            }
        }
        Json::Object result;
        const auto *type = symbol->id.kind == LanguageSymbolKind::Struct
                               ? symbol
                               : index.typeDefinition(symbol->id);
        if (type != nullptr && type->id.kind == LanguageSymbolKind::Struct) {
            const auto &declaration = analysis->program.structs[type->id.owner];
            auto methodCount = std::size_t{};
            std::set<std::size_t> sourceFiles{declaration.span.source};
            for (const auto &function : analysis->program.functions) {
                if (function.ownerType != declaration.name) {
                    continue;
                }
                ++methodCount;
                sourceFiles.insert(function.span.source);
            }
            auto summary = "\n\n" + std::to_string(declaration.fields.size()) +
                           (declaration.fields.size() == 1 ? " field, " : " fields, ") +
                           std::to_string(methodCount) +
                           (methodCount == 1 ? " method across " : " methods across ") +
                           std::to_string(sourceFiles.size()) +
                           (sourceFiles.size() == 1 ? " file" : " files");
            value += summary;
            const auto &source = analysis->sources[declaration.span.source];
            result.emplace(
                "foundationComposite",
                Json::object({{"uri", sourceUri(source.identity)},
                              {"position", lspPosition(positionAt(
                                               source.contents,
                                               declaration.span.offset))}}));
        }
        auto types = typeTargets(*analysis, index, *symbol);
        if (!types.empty()) {
            result.emplace("foundationTypes", Json(std::move(types)));
        }
        result.emplace("contents", Json::object({{"kind", "markdown"},
                                                   {"value", std::move(value)}}));
        result.emplace("range", lspRange(analysis->sources[*sourceId].contents, word));
        return Json(std::move(result));
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
        const auto sourceId = sourceIdForUri(*analysis, *uri);
        const auto position = requestPosition(params);
        if (sourceId.has_value() && position.has_value()) {
            const auto &source = analysis->sources[*sourceId];
            const auto offset = offsetAt(source.contents, *position);
            const auto *imported = offset.has_value()
                                       ? importAt(*analysis, *sourceId, *offset)
                                       : nullptr;
            if (imported != nullptr) {
                return packageLocations(*analysis, *imported);
            }
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
        if (symbol == nullptr || symbol->definition.source >= analysis->sources.size()) {
            return Json(nullptr);
        }
        auto target = symbol;
        const auto &definitionSource = analysis->sources[target->definition.source];
        if (definitionSource.path.ends_with(".foundation.generated.fdn") &&
            target->id.kind == LanguageSymbolKind::Method &&
            target->id.owner < analysis->program.functions.size()) {
            const auto &owner = analysis->program.functions[target->id.owner].ownerType;
            const auto declaration = std::find_if(
                analysis->program.structs.begin(), analysis->program.structs.end(),
                [&](const auto &type) { return type.name == owner; });
            if (declaration != analysis->program.structs.end()) {
                const auto id = static_cast<std::size_t>(
                    std::distance(analysis->program.structs.begin(), declaration));
                if (const auto *type = index.symbol(
                        {LanguageSymbolKind::Struct, id, 0});
                    type != nullptr) {
                    target = type;
                }
            }
        }
        const auto &source = analysis->sources[target->definition.source];
        return Json::object({{"uri", pathToFileUri(source.identity)},
                             {"range", lspRange(source.contents, target->definition)}});
    }

    [[nodiscard]] Json provideTypeDefinition(const Json *params) const {
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
        const auto *type = symbolId.has_value() ? index.typeDefinition(*symbolId) : nullptr;
        if (type == nullptr || type->definition.source >= analysis->sources.size()) {
            return Json(nullptr);
        }
        const auto &source = analysis->sources[type->definition.source];
        return Json::object({{"uri", pathToFileUri(source.identity)},
                             {"range", lspRange(source.contents, type->definition)}});
    }

    [[nodiscard]] bool contractExtends(const SemanticModel &semantic, std::size_t contract,
                                       std::size_t target,
                                       std::set<std::size_t> &visited) const {
        if (contract == target) {
            return true;
        }
        if (contract >= semantic.contracts.size() || !visited.insert(contract).second) {
            return false;
        }
        for (const auto &parent : semantic.contracts[contract].parents) {
            if (parent.kind == TypeKind::Contract &&
                contractExtends(semantic, parent.declaration, target, visited)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool structImplements(const SemanticModel &semantic,
                                        std::size_t declaration,
                                        std::size_t contract) const {
        if (declaration >= semantic.structs.size()) {
            return false;
        }
        for (const auto &implementation : semantic.structs[declaration].implementations) {
            std::set<std::size_t> visited;
            if (implementation.kind == TypeKind::Contract &&
                contractExtends(semantic, implementation.declaration, contract, visited)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] Json provideImplementations(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            return Json(Json::Array{});
        }
        auto analysis = analyzeUri(*uri);
        if (analysis == nullptr || !analysis->semantic.has_value()) {
            return Json(Json::Array{});
        }
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
        if (!symbolId.has_value() ||
            (symbolId->kind != LanguageSymbolKind::Contract &&
             symbolId->kind != LanguageSymbolKind::ContractMethod)) {
            return Json(Json::Array{});
        }
        const auto contract = symbolId->owner;
        const auto method = symbolId->kind == LanguageSymbolKind::ContractMethod
                                ? index.symbol(*symbolId)
                                : nullptr;
        Json::Array result;
        std::set<std::pair<std::size_t, std::size_t>> locations;
        const auto addLocation = [&](SourceSpan span) {
            if (span.source >= analysis->sources.size() ||
                !locations.emplace(span.source, span.offset).second) {
                return;
            }
            const auto &source = analysis->sources[span.source];
            result.push_back(Json::object({{"uri", pathToFileUri(source.identity)},
                                           {"range", lspRange(source.contents, span)}}));
        };
        for (std::size_t declaration = 0;
             declaration < analysis->program.structs.size(); ++declaration) {
            if (!structImplements(*analysis->semantic, declaration, contract)) {
                continue;
            }
            auto foundMethod = false;
            if (method != nullptr) {
                const auto &owner = analysis->program.structs[declaration];
                for (std::size_t function = 0;
                     function < analysis->program.functions.size(); ++function) {
                    const auto &candidate = analysis->program.functions[function];
                    if (!candidate.receiver.has_value() || candidate.ownerType != owner.name ||
                        shortName(candidate.name) != method->name) {
                        continue;
                    }
                    const auto *functionSymbol = index.symbol(
                        {LanguageSymbolKind::Method, function, 0});
                    if (functionSymbol != nullptr) {
                        addLocation(functionSymbol->definition);
                        foundMethod = true;
                    }
                }
            }
            if (method == nullptr || !foundMethod) {
                const auto *structSymbol = index.symbol(
                    {LanguageSymbolKind::Struct, declaration, 0});
                if (structSymbol != nullptr) {
                    addLocation(structSymbol->definition);
                }
            }
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideDocumentHighlights(const Json *params) const {
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
        SourceSpan word;
        const auto &index = languageIndex(*analysis);
        const auto symbol = semanticSymbolAt(*analysis, *uri, params, word, index);
        if (!symbol.has_value()) {
            return Json(Json::Array{});
        }
        Json::Array result;
        for (const auto &reference : index.references(*symbol, true)) {
            if (reference.span.source != *sourceId) {
                continue;
            }
            result.push_back(Json::object(
                {{"range", lspRange(analysis->sources[*sourceId].contents, reference.span)},
                 {"kind", 1}}));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideCompositeType(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        auto uri = stringField(textDocument, "uri");
        if (!uri.has_value()) {
            uri = stringField(params, "sourceUri");
        }
        if (!uri.has_value()) {
            return Json(nullptr);
        }
        const auto *analysis = analyzeUri(*uri);
        if (analysis == nullptr) {
            return Json(nullptr);
        }
        const auto &index = languageIndex(*analysis);
        const StructDeclaration *declaration = nullptr;
        auto declarationIndex = std::size_t{};
        if (const auto typeName = stringField(params, "typeName"); typeName.has_value()) {
            const auto packageName = stringField(params, "packageName");
            for (std::size_t current = 0; current < analysis->program.structs.size(); ++current) {
                const auto &candidate = analysis->program.structs[current];
                if (shortName(candidate.name) != *typeName ||
                    (packageName.has_value() && candidate.packageName != *packageName)) {
                    continue;
                }
                declaration = &candidate;
                declarationIndex = current;
                break;
            }
        } else {
            SourceSpan word;
            const auto symbolId = semanticSymbolAt(*analysis, *uri, params, word, index);
            const auto *symbol = symbolId.has_value() ? index.symbol(*symbolId) : nullptr;
            if (symbol != nullptr && symbol->id.kind != LanguageSymbolKind::Struct) {
                symbol = index.typeDefinition(symbol->id);
            }
            if (symbol != nullptr && symbol->id.kind == LanguageSymbolKind::Struct &&
                symbol->id.owner < analysis->program.structs.size()) {
                declarationIndex = symbol->id.owner;
                declaration = &analysis->program.structs[declarationIndex];
            }
        }
        if (declaration == nullptr || declaration->span.source >= analysis->sources.size()) {
            return Json(nullptr);
        }
        const auto &structSource = analysis->sources[declaration->span.source];
        const auto structExtent = declarationExtent(structSource.contents, declaration->span);
        if (!structExtent.has_value() || structExtent->length < 2) {
            return Json(nullptr);
        }

        struct MethodFragment {
            const Function *function{};
            SourceSpan extent;
        };
        std::vector<MethodFragment> externalMethods;
        auto methodCount = std::size_t{};
        std::set<std::size_t> sourceFiles{declaration->span.source};
        for (const auto &function : analysis->program.functions) {
            if (function.ownerType != declaration->name ||
                function.span.source >= analysis->sources.size()) {
                continue;
            }
            ++methodCount;
            sourceFiles.insert(function.span.source);
            const auto &source = analysis->sources[function.span.source];
            const auto extent = declarationExtent(source.contents, function.span);
            if (!extent.has_value()) {
                continue;
            }
            const auto end = extent->offset + extent->length;
            const auto structEnd = structExtent->offset + structExtent->length;
            if (extent->source == structExtent->source &&
                structExtent->offset <= extent->offset && end <= structEnd) {
                continue;
            }
            externalMethods.push_back({&function, *extent});
        }
        std::sort(externalMethods.begin(), externalMethods.end(),
                  [analysis](const auto &left, const auto &right) {
                      const auto leftName = shortName(left.function->name);
                      const auto rightName = shortName(right.function->name);
                      if (leftName != rightName) {
                          return leftName < rightName;
                      }
                      const auto &leftSource = analysis->sources[left.extent.source].identity;
                      const auto &rightSource = analysis->sources[right.extent.source].identity;
                      if (leftSource != rightSource) {
                          return leftSource < rightSource;
                      }
                      return left.extent.offset < right.extent.offset;
                  });

        const auto fragment = [this, analysis](std::string key, std::string kind,
                                               std::string name, SourceSpan span,
                                               int indent) {
            const auto &source = analysis->sources[span.source];
            return Json::object(
                {{"key", std::move(key)},
                 {"kind", std::move(kind)},
                 {"name", std::move(name)},
                 {"uri", sourceUri(source.identity)},
                 {"path", source.identity},
                 {"line", static_cast<double>(positionAt(source.contents, span.offset).line + 1)},
                 {"range", lspRange(source.contents, span)},
                 {"text", source.contents.substr(span.offset, span.length)},
                 {"indent", indent}});
        };

        Json::Array fragments;
        const SourceSpan prefix{structExtent->offset, structExtent->length - 1,
                                structExtent->line, structExtent->column,
                                structExtent->source};
        const auto suffixOffset = structExtent->offset + structExtent->length - 1;
        const auto suffixPosition = positionAt(structSource.contents, suffixOffset);
        const SourceSpan suffix{suffixOffset, 1, suffixPosition.line + 1,
                                suffixPosition.character + 1, structExtent->source};
        const auto declarationKind =
            declaration->kind == StructKind::Service ? "service" : "struct";
        fragments.push_back(fragment("struct-prefix", declarationKind,
                                     shortName(declaration->name),
                                     prefix, 0));
        for (const auto &method : externalMethods) {
            const auto &source = analysis->sources[method.extent.source];
            const auto key = "method:" + source.identity + ':' +
                             std::to_string(method.extent.offset) + ':' +
                             shortName(method.function->name);
            fragments.push_back(fragment(key, "method", shortName(method.function->name),
                                         method.extent, 4));
        }
        fragments.push_back(fragment("struct-suffix", declarationKind,
                                     shortName(declaration->name),
                                     suffix, 0));

        std::set<std::string> imports;
        for (const auto &imported : analysis->program.imports) {
            if (imported.span.source >= analysis->sources.size() ||
                analysis->sources[imported.span.source].packageName !=
                    declaration->packageName) {
                continue;
            }
            auto text = "import " + imported.packageName;
            if (!imported.alias.empty()) {
                text += " as " + imported.alias;
            }
            imports.insert(std::move(text));
        }
        Json::Array importValues;
        for (const auto &imported : imports) {
            importValues.emplace_back(imported);
        }
        const auto *structSymbol = index.symbol(
            {LanguageSymbolKind::Struct, declarationIndex, 0});
        return Json::object(
            {{"typeName", shortName(declaration->name)},
             {"packageName", declaration->packageName},
             {"sourceUri", sourceUri(structSource.identity)},
             {"documentation", structSymbol == nullptr ? std::string{}
                                                       : structSymbol->documentation},
             {"fieldCount", static_cast<double>(declaration->fields.size())},
             {"methodCount", static_cast<double>(methodCount)},
             {"fileCount", static_cast<double>(sourceFiles.size())},
             {"imports", Json(std::move(importValues))},
             {"fragments", Json(std::move(fragments))}});
    }

    [[nodiscard]] Json provideCodeLenses(const Json *params) const {
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
        const auto &index = languageIndex(*analysis);
        std::vector<const LanguageSymbol *> symbols;
        for (const auto &symbol : index.symbols()) {
            if (symbol.definition.source == *sourceId &&
                symbol.id.kind != LanguageSymbolKind::Parameter &&
                symbol.id.kind != LanguageSymbolKind::EnumPayload &&
                symbol.id.kind != LanguageSymbolKind::Local) {
                symbols.push_back(&symbol);
            }
        }
        std::sort(symbols.begin(), symbols.end(), [](const auto *left, const auto *right) {
            return left->definition.offset < right->definition.offset;
        });
        Json::Array result;
        const auto &requestedSource = analysis->sources[*sourceId];
        for (const auto *symbol : symbols) {
            const auto lensAnchor =
                codeLensAnchor(requestedSource.contents, symbol->definition);
            if (symbol->id.kind == LanguageSymbolKind::Struct) {
                const auto position = positionAt(requestedSource.contents,
                                                 symbol->definition.offset);
                result.push_back(Json::object(
                    {{"range", lspRange(requestedSource.contents, lensAnchor)},
                     {"command",
                      Json::object(
                          {{"title", "Peek Composite Type"},
                           {"command", "foundation.openCompositeType"},
                           {"arguments", Json::array({*uri, lspPosition(position)})}})}}));
            }
            const auto references = index.references(symbol->id, false);
            Json::Array locations;
            for (const auto &reference : references) {
                if (reference.span.source >= analysis->sources.size()) {
                    continue;
                }
                const auto &source = analysis->sources[reference.span.source];
                locations.push_back(
                    Json::object({{"uri", pathToFileUri(source.identity)},
                                  {"range", lspRange(source.contents, reference.span)}}));
            }
            const auto title = std::to_string(locations.size()) +
                               (locations.size() == 1 ? " reference" : " references");
            const auto position = positionAt(requestedSource.contents,
                                             symbol->definition.offset);
            result.push_back(Json::object(
                {{"range", lspRange(requestedSource.contents, lensAnchor)},
                 {"command",
                  Json::object(
                      {{"title", title},
                       {"command", "editor.action.showReferences"},
                       {"arguments",
                        Json::array({*uri, lspPosition(position),
                                     Json(std::move(locations))})}})}}));
        }
        return Json(std::move(result));
    }

    struct HierarchyContext {
        const ProjectAnalysis *analysis{};
        const LanguageIndex *index{};
        const LanguageSymbol *symbol{};
    };

    [[nodiscard]] Json typeHierarchyItem(const ProjectAnalysis &analysis,
                                         const LanguageSymbol &symbol) const {
        if (symbol.definition.source >= analysis.sources.size()) {
            return Json(nullptr);
        }
        const auto &source = analysis.sources[symbol.definition.source];
        const auto kind = symbol.id.kind == LanguageSymbolKind::Struct ? "struct" : "contract";
        return Json::object(
            {{"name", symbol.name},
             {"kind", symbol.id.kind == LanguageSymbolKind::Struct ? 23 : 11},
             {"detail", symbol.detail},
             {"uri", sourceUri(source.identity)},
             {"range", lspRange(source.contents, symbol.definition)},
             {"selectionRange", lspRange(source.contents, symbol.definition)},
             {"data", Json::object({{"kind", kind},
                                     {"name", symbol.name},
                                     {"scope", symbol.scope},
                                     {"uri", sourceUri(source.identity)}})}});
    }

    [[nodiscard]] std::optional<HierarchyContext> hierarchyContext(
        const Json *params) const {
        const auto *item = params == nullptr ? nullptr : params->find("item");
        const auto *data = item == nullptr ? nullptr : item->find("data");
        const auto kind = stringField(data, "kind");
        const auto name = stringField(data, "name");
        const auto scope = stringField(data, "scope");
        const auto uri = stringField(data, "uri");
        if (!kind.has_value() || !name.has_value() || !scope.has_value() ||
            !uri.has_value()) {
            return std::nullopt;
        }
        const auto path = fileUriToPath(*uri);
        if (!path.has_value()) {
            return std::nullopt;
        }
        const auto &analysis = analyzeRoot(analysisRoot(normalizedPath(*path)));
        const auto &index = languageIndex(analysis);
        const auto symbolKind = *kind == "struct" ? LanguageSymbolKind::Struct
                                : *kind == "contract" ? LanguageSymbolKind::Contract
                                                      : LanguageSymbolKind::Local;
        for (const auto &symbol : index.symbols()) {
            if (symbol.id.kind == symbolKind && symbol.name == *name &&
                symbol.scope == *scope) {
                return HierarchyContext{&analysis, &index, &symbol};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] Json prepareTypeHierarchy(const Json *params) const {
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
        if (symbol == nullptr ||
            (symbol->id.kind != LanguageSymbolKind::Struct &&
             symbol->id.kind != LanguageSymbolKind::Contract)) {
            return Json(nullptr);
        }
        return Json::array({typeHierarchyItem(*analysis, *symbol)});
    }

    [[nodiscard]] Json provideTypeHierarchySupertypes(const Json *params) const {
        const auto context = hierarchyContext(params);
        if (!context.has_value() || !context->analysis->semantic.has_value()) {
            return Json(Json::Array{});
        }
        std::vector<Type> parents;
        if (context->symbol->id.kind == LanguageSymbolKind::Struct &&
            context->symbol->id.owner < context->analysis->semantic->structs.size()) {
            parents = context->analysis->semantic->structs[context->symbol->id.owner]
                          .implementations;
        } else if (context->symbol->id.kind == LanguageSymbolKind::Contract &&
                   context->symbol->id.owner < context->analysis->semantic->contracts.size()) {
            parents = context->analysis->semantic->contracts[context->symbol->id.owner].parents;
        }
        Json::Array result;
        for (const auto &parent : parents) {
            const auto *symbol = parent.kind == TypeKind::Contract
                                     ? context->index->symbol(
                                           {LanguageSymbolKind::Contract,
                                            parent.declaration, 0})
                                     : nullptr;
            if (symbol != nullptr) {
                result.push_back(typeHierarchyItem(*context->analysis, *symbol));
            }
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideTypeHierarchySubtypes(const Json *params) const {
        const auto context = hierarchyContext(params);
        if (!context.has_value() || !context->analysis->semantic.has_value() ||
            context->symbol->id.kind != LanguageSymbolKind::Contract) {
            return Json(Json::Array{});
        }
        Json::Array result;
        const auto contract = context->symbol->id.owner;
        for (std::size_t declaration = 0;
             declaration < context->analysis->semantic->contracts.size(); ++declaration) {
            const auto &candidate = context->analysis->semantic->contracts[declaration];
            const auto direct = std::any_of(candidate.parents.begin(), candidate.parents.end(),
                                            [contract](const auto &parent) {
                                                return parent.kind == TypeKind::Contract &&
                                                       parent.declaration == contract;
                                            });
            const auto *symbol = direct ? context->index->symbol(
                                              {LanguageSymbolKind::Contract, declaration, 0})
                                        : nullptr;
            if (symbol != nullptr) {
                result.push_back(typeHierarchyItem(*context->analysis, *symbol));
            }
        }
        for (std::size_t declaration = 0;
             declaration < context->analysis->semantic->structs.size(); ++declaration) {
            const auto &candidate = context->analysis->semantic->structs[declaration];
            const auto direct = std::any_of(candidate.implementations.begin(),
                                            candidate.implementations.end(),
                                            [contract](const auto &implementation) {
                                                return implementation.kind == TypeKind::Contract &&
                                                       implementation.declaration == contract;
                                            });
            const auto *symbol = direct ? context->index->symbol(
                                              {LanguageSymbolKind::Struct, declaration, 0})
                                        : nullptr;
            if (symbol != nullptr) {
                result.push_back(typeHierarchyItem(*context->analysis, *symbol));
            }
        }
        return Json(std::move(result));
    }

    [[nodiscard]] bool callableSymbol(LanguageSymbolKind kind) const {
        return kind == LanguageSymbolKind::Function || kind == LanguageSymbolKind::Method ||
               kind == LanguageSymbolKind::ContractMethod;
    }

    [[nodiscard]] Json callHierarchyItem(const ProjectAnalysis &analysis,
                                         const LanguageSymbol &symbol) const {
        if (symbol.definition.source >= analysis.sources.size()) {
            return Json(nullptr);
        }
        const auto &source = analysis.sources[symbol.definition.source];
        const auto kind = symbol.id.kind == LanguageSymbolKind::Function
                              ? "function"
                          : symbol.id.kind == LanguageSymbolKind::Method
                              ? "method"
                              : "contractMethod";
        return Json::object(
            {{"name", symbol.name},
             {"kind", symbol.id.kind == LanguageSymbolKind::Function ? 12 : 6},
             {"detail", symbol.detail},
             {"uri", sourceUri(source.identity)},
             {"range", lspRange(source.contents, symbol.definition)},
             {"selectionRange", lspRange(source.contents, symbol.definition)},
             {"data", Json::object({{"kind", kind},
                                     {"name", symbol.name},
                                     {"scope", symbol.scope},
                                     {"uri", sourceUri(source.identity)}})}});
    }

    [[nodiscard]] std::optional<HierarchyContext> callContext(const Json *params) const {
        const auto *item = params == nullptr ? nullptr : params->find("item");
        const auto *data = item == nullptr ? nullptr : item->find("data");
        const auto kind = stringField(data, "kind");
        const auto name = stringField(data, "name");
        const auto scope = stringField(data, "scope");
        const auto uri = stringField(data, "uri");
        if (!kind.has_value() || !name.has_value() || !scope.has_value() ||
            !uri.has_value()) {
            return std::nullopt;
        }
        const auto path = fileUriToPath(*uri);
        if (!path.has_value()) {
            return std::nullopt;
        }
        const auto &analysis = analyzeRoot(analysisRoot(normalizedPath(*path)));
        const auto &index = languageIndex(analysis);
        const auto symbolKind = *kind == "function" ? LanguageSymbolKind::Function
                                : *kind == "method" ? LanguageSymbolKind::Method
                                : *kind == "contractMethod"
                                    ? LanguageSymbolKind::ContractMethod
                                    : LanguageSymbolKind::Local;
        for (const auto &symbol : index.symbols()) {
            if (symbol.id.kind == symbolKind && symbol.name == *name &&
                symbol.scope == *scope) {
                return HierarchyContext{&analysis, &index, &symbol};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] Json prepareCallHierarchy(const Json *params) const {
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
        if (symbol == nullptr || !callableSymbol(symbol->id.kind)) {
            return Json(nullptr);
        }
        return Json::array({callHierarchyItem(*analysis, *symbol)});
    }

    struct CallGroup {
        const LanguageSymbol *symbol{};
        std::vector<SourceSpan> ranges;
    };

    [[nodiscard]] Json provideIncomingCalls(const Json *params) const {
        const auto context = callContext(params);
        if (!context.has_value()) {
            return Json(Json::Array{});
        }
        std::vector<CallGroup> groups;
        for (const auto &call : context->index->incomingCalls(context->symbol->id)) {
            const auto *caller = context->index->symbol(call.caller);
            if (caller == nullptr) {
                continue;
            }
            const auto found = std::find_if(groups.begin(), groups.end(), [&](const auto &group) {
                return group.symbol->id == caller->id;
            });
            if (found != groups.end()) {
                found->ranges.push_back(call.span);
            } else {
                groups.push_back({caller, {call.span}});
            }
        }
        Json::Array result;
        for (const auto &group : groups) {
            Json::Array ranges;
            for (const auto span : group.ranges) {
                if (span.source < context->analysis->sources.size()) {
                    ranges.push_back(lspRange(
                        context->analysis->sources[span.source].contents, span));
                }
            }
            result.push_back(Json::object(
                {{"from", callHierarchyItem(*context->analysis, *group.symbol)},
                 {"fromRanges", Json(std::move(ranges))}}));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideOutgoingCalls(const Json *params) const {
        const auto context = callContext(params);
        if (!context.has_value()) {
            return Json(Json::Array{});
        }
        std::vector<CallGroup> groups;
        for (const auto &call : context->index->outgoingCalls(context->symbol->id)) {
            const auto *callee = context->index->symbol(call.callee);
            if (callee == nullptr) {
                continue;
            }
            const auto found = std::find_if(groups.begin(), groups.end(), [&](const auto &group) {
                return group.symbol->id == callee->id;
            });
            if (found != groups.end()) {
                found->ranges.push_back(call.span);
            } else {
                groups.push_back({callee, {call.span}});
            }
        }
        Json::Array result;
        for (const auto &group : groups) {
            Json::Array ranges;
            for (const auto span : group.ranges) {
                if (span.source < context->analysis->sources.size()) {
                    ranges.push_back(lspRange(
                        context->analysis->sources[span.source].contents, span));
                }
            }
            result.push_back(Json::object(
                {{"to", callHierarchyItem(*context->analysis, *group.symbol)},
                 {"fromRanges", Json(std::move(ranges))}}));
        }
        return Json(std::move(result));
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
                              std::optional<std::string> insertText = std::nullopt,
                              std::string documentation = {}) {
        Json::Object item{{"label", label}, {"kind", kind}};
        if (!detail.empty()) {
            item.emplace("detail", std::move(detail));
        }
        if (!documentation.empty()) {
            item.emplace("documentation",
                         Json::object({{"kind", "markdown"},
                                       {"value", std::move(documentation)}}));
        }
        if (insertText.has_value()) {
            const auto hasParameters = insertText->find("${1:") != std::string::npos;
            item.emplace("insertText", std::move(*insertText));
            item.emplace("insertTextFormat", 2);
            if (hasParameters) {
                item.emplace("command",
                             Json::object({{"title", "Show parameter information"},
                                           {"command",
                                            "editor.action.triggerParameterHints"}}));
            }
        }
        items.emplace(std::move(label), Json(std::move(item)));
    }

    static Json completionResult(std::map<std::string, Json> items) {
        Json::Array result;
        result.reserve(items.size());
        for (auto &[label, item] : items) {
            static_cast<void>(label);
            result.push_back(std::move(item));
        }
        return Json(std::move(result));
    }

    static std::string packageAlias(std::string_view packageName) {
        return shortName(packageName);
    }

    static std::optional<std::pair<std::size_t, std::size_t>> identifierBefore(
        std::string_view source, std::size_t end) {
        if (end == 0 || end > source.size() ||
            !identifierByte(static_cast<unsigned char>(source[end - 1]))) {
            return std::nullopt;
        }
        auto start = end - 1;
        while (start != 0 &&
               identifierByte(static_cast<unsigned char>(source[start - 1]))) {
            --start;
        }
        return std::pair{start, end};
    }

    static void setCompletion(std::map<std::string, Json> &items, std::string label,
                              int kind, std::string detail = {}) {
        Json::Object item{{"label", label}, {"kind", kind}};
        if (!detail.empty()) {
            item.emplace("detail", std::move(detail));
        }
        items.insert_or_assign(std::move(label), Json(std::move(item)));
    }

    static bool accessible(std::string_view ownerPackage, bool exported,
                           std::string_view currentPackage) {
        return ownerPackage == currentPackage || exported;
    }

    static std::string semanticMethodDetail(const ProjectAnalysis &analysis,
                                            const SemanticContractMethod &method) {
        if (method.originContract >= analysis.program.contracts.size()) {
            return "Foundation contract method";
        }
        for (const auto &candidate : analysis.program.contracts[method.originContract].methods) {
            if (candidate.name == method.name && candidate.span.source == method.span.source &&
                candidate.span.offset == method.span.offset) {
                return contractMethodDetail(candidate);
            }
        }
        return "Foundation contract method";
    }

    static Type completionValueType(Type type) {
        while ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
                type.kind == TypeKind::Edit) &&
               type.arguments.size() == 1) {
            type = type.arguments.front();
        }
        return type;
    }

    static std::optional<Type> completionReceiverType(const ProjectAnalysis &analysis,
                                                      std::size_t sourceId,
                                                      std::size_t receiverEnd) {
        if (!analysis.semantic.has_value()) {
            return std::nullopt;
        }
        std::optional<AstExpressionId> selected;
        for (AstExpressionId id = 0; id < analysis.program.expressions.size() &&
                                      id < analysis.semantic->expressionTypes.size();
             ++id) {
            const auto span = analysis.program.expressions[id].span;
            if (span.source != sourceId || span.offset + span.length != receiverEnd ||
                analysis.semantic->expressionTypes[id].kind == TypeKind::Invalid) {
                continue;
            }
            if (!selected.has_value() ||
                span.length > analysis.program.expressions[*selected].span.length) {
                selected = id;
            }
        }
        return selected.has_value()
                   ? std::optional<Type>{analysis.semantic->expressionTypes[*selected]}
                   : std::nullopt;
    }

    static std::optional<Type> completionReceiverSymbolType(
        const ProjectAnalysis &analysis, const LanguageIndex &index, std::size_t sourceId,
        std::size_t receiverStart) {
        if (!analysis.semantic.has_value()) {
            return std::nullopt;
        }
        const auto *occurrence = index.occurrenceAt(sourceId, receiverStart);
        if (occurrence == nullptr ||
            (occurrence->symbol.kind != LanguageSymbolKind::Parameter &&
             occurrence->symbol.kind != LanguageSymbolKind::Local) ||
            occurrence->symbol.owner >= analysis.semantic->functions.size()) {
            return std::nullopt;
        }
        const auto &function = analysis.semantic->functions[occurrence->symbol.owner];
        return occurrence->symbol.member < function.locals.size()
                   ? std::optional<Type>{function.locals[occurrence->symbol.member].type}
                   : std::nullopt;
    }

    void addContractCompletions(std::map<std::string, Json> &items,
                                const ProjectAnalysis &analysis,
                                std::size_t contract,
                                std::string_view currentPackage,
                                bool defaultsOnly) const {
        if (!analysis.semantic.has_value() ||
            contract >= analysis.semantic->contracts.size()) {
            return;
        }
        for (const auto &method : analysis.semantic->contracts[contract].methods) {
            if (defaultsOnly && !method.defaultFunction.has_value()) {
                continue;
            }
            const auto owner = method.originContract < analysis.program.contracts.size()
                                   ? analysis.program.contracts[method.originContract].packageName
                                   : std::string{};
            if (!accessible(owner, method.exported, currentPackage)) {
                continue;
            }
            SourceSpan documentationSpan = method.span;
            if (method.originContract < analysis.program.contracts.size()) {
                const auto &origin = analysis.program.contracts[method.originContract];
                for (std::size_t candidate = 0; candidate < origin.methods.size(); ++candidate) {
                    if (origin.methods[candidate].name == method.name &&
                        origin.methods[candidate].span.source == method.span.source &&
                        origin.methods[candidate].span.offset == method.span.offset) {
                        documentationSpan = origin.methods[candidate].span;
                        break;
                    }
                }
            }
            auto snippet = method.name + "($0)";
            if (method.originContract < analysis.program.contracts.size()) {
                const auto &origin = analysis.program.contracts[method.originContract];
                const auto declaration = std::find_if(
                    origin.methods.begin(), origin.methods.end(),
                    [&method](const auto &candidate) {
                        return candidate.name == method.name &&
                               candidate.span.source == method.span.source &&
                               candidate.span.offset == method.span.offset;
                    });
                if (declaration != origin.methods.end()) {
                    snippet = callSnippet(method.name, declaration->parameters);
                }
            }
            addCompletion(items, method.name, 2, semanticMethodDetail(analysis, method),
                          snippet,
                          languageDocumentation(analysis, documentationSpan));
        }
    }

    void addValueMemberCompletions(std::map<std::string, Json> &items,
                                   const ProjectAnalysis &analysis,
                                   Type type, std::string_view currentPackage) const {
        const auto sourceType = type;
        type = completionValueType(std::move(type));
        if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
            const auto editable = sourceType.kind == TypeKind::Edit;
            addCompletion(items, "pointer", 5,
                          std::string(editable ? "*" : "*const ") +
                              displaySemanticType(analysis, type.arguments.front()),
                          std::nullopt,
                          "Exposes the slice storage inside a documented unsafe block.");
            return;
        }
        if (type.kind == TypeKind::Channel && type.arguments.size() == 1) {
            addCompletion(items, "sender", 5, "Sender endpoint");
            addCompletion(items, "receiver", 5, "Receiver endpoint");
            return;
        }
        if (type.kind == TypeKind::Sender && type.arguments.size() == 1) {
            const auto payload = displaySemanticType(analysis, type.arguments.front());
            addCompletion(items, "clone", 2,
                          "fn clone() Sender<" + payload + ">", "clone()",
                          "Creates another owned sender handle for the same channel.");
            const auto snippet =
                type.arguments.front() == voidType ? "send()" : "send(${1:value})";
            const auto detail = type.arguments.front() == voidType
                                    ? std::string("fn send() Result<void, ChannelError>")
                                    : "fn send(value " + payload +
                                          ") Result<void, ChannelError>";
            addCompletion(items, "send", 2, detail, snippet,
                          "Suspends the current task until ownership of the value transfers or "
                          "the operation reports a typed channel error.");
            return;
        }
        if (type.kind == TypeKind::Receiver && type.arguments.size() == 1) {
            const auto payload = displaySemanticType(analysis, type.arguments.front());
            addCompletion(items, "receive", 2,
                          "fn receive() Result<" + payload + ", ChannelError>",
                          "receive()",
                          "Suspends the current task until a value arrives or the operation "
                          "reports a typed channel error.");
            return;
        }
        if (type.kind == TypeKind::Struct && type.declaration < analysis.program.structs.size()) {
            const auto &declaration = analysis.program.structs[type.declaration];
            for (std::size_t fieldIndex = 0; fieldIndex < declaration.fields.size();
                 ++fieldIndex) {
                const auto &field = declaration.fields[fieldIndex];
                if (accessible(declaration.packageName, field.exported, currentPackage)) {
                    addCompletion(items, field.name, 5,
                                  field.name + ' ' + displayTypeSyntax(field.type),
                                  std::nullopt,
                                  languageDocumentation(analysis, field.span));
                }
            }
            for (std::size_t functionIndex = 0;
                 functionIndex < analysis.program.functions.size(); ++functionIndex) {
                const auto &function = analysis.program.functions[functionIndex];
                if (!function.receiver.has_value() || shortName(function.name) == "drop" ||
                    function.packageName != declaration.packageName ||
                    shortName(function.ownerType) != shortName(declaration.name) ||
                    !accessible(function.packageName, function.exported, currentPackage)) {
                    continue;
                }
                const auto name = shortName(function.name);
                addCompletion(items, name, 2, functionDetail(function),
                              functionCallSnippet(function, name),
                              languageDocumentation(analysis, function.span));
            }
            if (analysis.semantic.has_value() &&
                type.declaration < analysis.semantic->structs.size()) {
                for (const auto &implementation :
                     analysis.semantic->structs[type.declaration].implementations) {
                    if (implementation.kind == TypeKind::Contract) {
                        addContractCompletions(items, analysis, implementation.declaration,
                                               currentPackage, true);
                    }
                }
            }
            return;
        }
        if (type.kind == TypeKind::Contract) {
            addContractCompletions(items, analysis, type.declaration, currentPackage, false);
            return;
        }
        if (type.kind == TypeKind::Enum && type.declaration < analysis.program.enums.size()) {
            const auto &declaration = analysis.program.enums[type.declaration];
            for (const auto &variant : declaration.variants) {
                if (accessible(declaration.packageName, variant.exported, currentPackage)) {
                    std::optional<std::string> snippet;
                    if (variant.payloadType.has_value()) {
                        snippet = variant.name + "(${1:" +
                                  variant.payloadName.value_or("value") + "})$0";
                    }
                    addCompletion(items, variant.name, 20, enumVariantDetail(variant), snippet,
                                  languageDocumentation(analysis, variant.span));
                }
            }
            for (const auto &function : analysis.program.functions) {
                if (!function.receiver.has_value() ||
                    function.packageName != declaration.packageName ||
                    shortName(function.ownerType) != shortName(declaration.name) ||
                    !accessible(function.packageName, function.exported, currentPackage)) {
                    continue;
                }
                const auto name = shortName(function.name);
                addCompletion(items, name, 2, functionDetail(function),
                              functionCallSnippet(function, name),
                              languageDocumentation(analysis, function.span));
            }
        }
    }

    void addAssociatedFunctionCompletions(std::map<std::string, Json> &items,
                                          const ProjectAnalysis &analysis,
                                          const StructDeclaration &declaration,
                                          std::string_view currentPackage) const {
        for (const auto &function : analysis.program.functions) {
            if (function.receiver.has_value() || function.ownerType != declaration.name ||
                !accessible(function.packageName, function.exported, currentPackage)) {
                continue;
            }
            const auto name = shortName(function.name);
            addCompletion(items, name, 2, functionDetail(function),
                          functionCallSnippet(function, name),
                          languageDocumentation(analysis, function.span));
        }
    }

    bool addPackageMemberCompletions(std::map<std::string, Json> &items,
                                     const ProjectAnalysis &analysis,
                                     std::size_t sourceId,
                                     std::string_view alias,
                                     bool attributeContext) const {
        std::optional<std::string> packageName;
        for (const auto &imported : analysis.program.imports) {
            const auto currentAlias = imported.alias.empty()
                                          ? packageAlias(imported.packageName)
                                          : imported.alias;
            if (imported.span.source == sourceId && currentAlias == alias) {
                packageName = imported.packageName;
                break;
            }
        }
        if (!packageName.has_value()) {
            return false;
        }
        const auto &index = languageIndex(analysis);
        if (attributeContext) {
            for (std::size_t declarationIndex = 0;
                 declarationIndex < analysis.program.attributeDeclarations.size();
                 ++declarationIndex) {
                const auto &declaration =
                    analysis.program.attributeDeclarations[declarationIndex];
                if (declaration.packageName == *packageName && declaration.exported) {
                    const auto name = shortName(declaration.name);
                    const auto *symbol = index.symbol(
                        {LanguageSymbolKind::Attribute, declarationIndex, 0});
                    addCompletion(items, name, 10,
                                  symbol == nullptr ? "typed Foundation attribute"
                                                    : symbol->detail,
                                  callSnippet(name, declaration.parameters),
                                  symbol == nullptr ? std::string{}
                                                    : symbol->documentation);
                }
            }
            return true;
        }
        for (std::size_t functionIndex = 0;
             functionIndex < analysis.program.functions.size(); ++functionIndex) {
            const auto &function = analysis.program.functions[functionIndex];
            if (function.packageName != *packageName || !function.exported ||
                !function.ownerType.empty() || function.closure) {
                continue;
            }
            const auto name = shortName(function.name);
            const auto *symbol = index.symbol(
                {LanguageSymbolKind::Function, functionIndex, 0});
            addCompletion(items, name, 3, functionDetail(function),
                          functionCallSnippet(function, name),
                          symbol == nullptr ? std::string{} : symbol->documentation);
        }
        for (std::size_t declarationIndex = 0;
             declarationIndex < analysis.program.structs.size(); ++declarationIndex) {
            const auto &declaration = analysis.program.structs[declarationIndex];
            if (declaration.packageName == *packageName && declaration.exported) {
                const auto *symbol = index.symbol(
                    {LanguageSymbolKind::Struct, declarationIndex, 0});
                addCompletion(items, shortName(declaration.name), 22,
                              symbol == nullptr ? "Foundation struct" : symbol->detail,
                              std::nullopt,
                              symbol == nullptr ? std::string{} : symbol->documentation);
            }
        }
        for (std::size_t declarationIndex = 0;
             declarationIndex < analysis.program.enums.size(); ++declarationIndex) {
            const auto &declaration = analysis.program.enums[declarationIndex];
            if (declaration.packageName == *packageName && declaration.exported &&
                declaration.builtin == BuiltinEnumKind::None) {
                const auto *symbol = index.symbol(
                    {LanguageSymbolKind::Enum, declarationIndex, 0});
                addCompletion(items, shortName(declaration.name), 13,
                              symbol == nullptr ? "Foundation enum" : symbol->detail,
                              std::nullopt,
                              symbol == nullptr ? std::string{}
                                                : symbol->documentation);
            }
        }
        for (std::size_t declarationIndex = 0;
             declarationIndex < analysis.program.contracts.size(); ++declarationIndex) {
            const auto &declaration = analysis.program.contracts[declarationIndex];
            if (declaration.packageName == *packageName && declaration.exported) {
                const auto *symbol = index.symbol(
                    {LanguageSymbolKind::Contract, declarationIndex, 0});
                addCompletion(items, shortName(declaration.name), 8,
                              symbol == nullptr ? "Foundation contract" : symbol->detail,
                              std::nullopt,
                              symbol == nullptr ? std::string{}
                                                : symbol->documentation);
            }
        }
        return true;
    }

    static void addVisibleLocalCompletions(std::map<std::string, Json> &items,
                                           const ProjectAnalysis &analysis,
                                           std::size_t sourceId, std::size_t offset,
                                           std::string_view source) {
        const auto delimiters = delimiterRanges(source, sourceId);
        const auto functionId = completionFunction(analysis.program, sourceId, offset,
                                                   delimiters);
        if (!functionId.has_value()) {
            return;
        }
        const auto &function = analysis.program.functions[*functionId];
        for (const auto &parameter : function.parameters) {
            const auto detail = parameter.name == "self"
                                    ? function.ownerType
                                    : parameter.name + ' ' + displayTypeSyntax(parameter.type);
            setCompletion(items, parameter.name, 6, detail);
        }
        for (const auto &capture : function.captures) {
            setCompletion(items, capture.name, 6, "Foundation closure capture");
        }
        std::vector<AstBlockId> path;
        if (!completionBlockPath(analysis.program, function.body, sourceId, offset,
                                 delimiters, path)) {
            return;
        }
        for (const auto blockId : path) {
            const auto &statements = analysis.program.blocks[blockId].statements;
            for (const auto statementId : statements) {
                if (statementId >= analysis.program.statements.size()) {
                    continue;
                }
                const auto &statement = analysis.program.statements[statementId];
                if (const auto *variable =
                        std::get_if<VariableStatement>(&statement.value)) {
                    const auto insideElse = variable->elseBlock.has_value() &&
                                            std::find(path.begin(), path.end(),
                                                      *variable->elseBlock) != path.end();
                    auto ready = completionExpressionEnd(
                        analysis.program, variable->initializer, delimiters);
                    if (variable->elseBlock.has_value()) {
                        if (const auto *range = completionBlockRange(
                                analysis.program, *variable->elseBlock, delimiters)) {
                            ready = std::max(ready,
                                             range->span.offset + range->span.length);
                        }
                    }
                    if (!insideElse && ready <= offset) {
                        setCompletion(items, variable->name, 6, "Foundation local binding");
                    }
                    if (insideElse && variable->elseBinding.has_value()) {
                        setCompletion(items, *variable->elseBinding, 6,
                                      "Foundation error binding");
                    }
                } else if (const auto *resultElse =
                               std::get_if<ResultElseStatement>(&statement.value)) {
                    if (resultElse->errorBinding.has_value() &&
                        std::find(path.begin(), path.end(), resultElse->elseBlock) !=
                            path.end()) {
                        setCompletion(items, *resultElse->errorBinding, 6,
                                      "Foundation error binding");
                    }
                } else if (const auto *destructure =
                               std::get_if<StructDestructureStatement>(&statement.value)) {
                    if (completionExpressionEnd(analysis.program, destructure->initializer,
                                                delimiters) <= offset) {
                        for (const auto &field : destructure->fields) {
                            setCompletion(items, field.binding, 6,
                                          "Foundation local binding");
                        }
                    }
                } else if (const auto *loop =
                               std::get_if<ForStatement>(&statement.value)) {
                    if (std::find(path.begin(), path.end(), loop->body) != path.end()) {
                        if (loop->indexBinding.has_value()) {
                            setCompletion(items, *loop->indexBinding, 6,
                                          "Foundation loop index");
                        }
                        setCompletion(items, loop->valueBinding, 6,
                                      loop->editable ? "Foundation editable loop binding"
                                                     : "Foundation loop binding");
                    }
                }
            }
        }
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
        const auto position = requestPosition(params);
        const auto requested = position.has_value() ? offsetAt(source.contents, *position)
                                                     : std::nullopt;
        if (requested.has_value()) {
            auto previous = *requested;
            while (previous != 0 &&
                   (source.contents[previous - 1] == ' ' ||
                    source.contents[previous - 1] == '\t')) {
                --previous;
            }
            attributeContext = previous != 0 && source.contents[previous - 1] == '@';
        }

        std::map<std::string, Json> items;
        const auto &index = languageIndex(*analysis);
        if (requested.has_value()) {
            if (const auto access = completionAccess(source.contents, *requested);
                access.has_value()) {
                const auto receiver = identifierBefore(source.contents, access->receiverEnd);
                if (receiver.has_value()) {
                    const auto alias = source.contents.substr(
                        receiver->first, receiver->second - receiver->first);
                    const auto attributeAccess = receiver->first != 0 &&
                                                 source.contents[receiver->first - 1] == '@';
                    if (addPackageMemberCompletions(items, *analysis, *sourceId, alias,
                                                    attributeAccess)) {
                        return completionResult(std::move(items));
                    }
                }

                const auto document = documents_.find(*uri);
                if (document == documents_.end()) {
                    return Json(Json::Array{});
                }
                if (const auto type =
                        completionReceiverType(*analysis, *sourceId, access->receiverEnd);
                    type.has_value()) {
                    addValueMemberCompletions(items, *analysis, *type,
                                              analysis->sources[*sourceId].packageName);
                    return completionResult(std::move(items));
                }
                if (receiver.has_value()) {
                    if (const auto type = completionReceiverSymbolType(
                            *analysis, index, *sourceId, receiver->first);
                        type.has_value()) {
                        addValueMemberCompletions(items, *analysis, *type,
                                                  analysis->sources[*sourceId].packageName);
                        return completionResult(std::move(items));
                    }
                }
                std::optional<std::size_t> eraseReceiver;
                const auto previousLine = receiver.has_value() && receiver->first != 0
                                              ? source.contents.rfind('\n', receiver->first - 1)
                                              : std::string_view::npos;
                const auto lineStart = previousLine == std::string_view::npos
                                           ? 0
                                           : previousLine + 1;
                const auto firstLineContent =
                    source.contents.find_first_not_of(" \t\r", lineStart);
                if (receiver.has_value() &&
                    (receiver->first == 0 ||
                     source.contents[receiver->first - 1] != '.') &&
                    (firstLineContent == std::string_view::npos ||
                     firstLineContent >= receiver->first) &&
                    std::isupper(static_cast<unsigned char>(
                        source.contents[receiver->first])) != 0) {
                    eraseReceiver = receiver->first;
                }
                const auto completion =
                    analyzeCompletion(document->second, *access, eraseReceiver);
                const auto completionSource = sourceIdForUri(completion, *uri);
                if (!completionSource.has_value()) {
                    return Json(Json::Array{});
                }
                if (const auto type = completionReceiverType(
                        completion, *completionSource, access->receiverEnd);
                    type.has_value()) {
                    addValueMemberCompletions(
                        items, completion, *type,
                        completion.sources[*completionSource].packageName);
                    return completionResult(std::move(items));
                }
                if (receiver.has_value()) {
                    const auto name = source.contents.substr(
                        receiver->first, receiver->second - receiver->first);
                    if (machineScalarName(name)) {
                        addCompletion(
                            items, "From", 2,
                            "fn From(value numeric) " + std::string(name) +
                                " or Result<" + std::string(name) + ", NumberError>",
                            "From(${1:value})",
                            "Performs an explicit numeric conversion. Conversions that can "
                            "lose information return a typed Result.");
                        return completionResult(std::move(items));
                    }
                    const auto currentPackage =
                        completion.sources[*completionSource].packageName;
                    auto receiverPackage = currentPackage;
                    if (receiver->first > 0 &&
                        source.contents[receiver->first - 1] == '.') {
                        const auto package = identifierBefore(source.contents,
                                                              receiver->first - 1);
                        if (package.has_value()) {
                            const auto alias = source.contents.substr(
                                package->first, package->second - package->first);
                            for (const auto &imported : completion.program.imports) {
                                const auto importedAlias = imported.alias.empty()
                                                               ? packageAlias(imported.packageName)
                                                               : imported.alias;
                                if (imported.span.source == *completionSource &&
                                    importedAlias == alias) {
                                    receiverPackage = imported.packageName;
                                    break;
                                }
                            }
                        }
                    }
                    for (const auto &candidate : completion.program.structs) {
                        if (shortName(candidate.name) == name &&
                            (candidate.packageName == receiverPackage ||
                             (receiverPackage == currentPackage &&
                              candidate.packageName == "std.prelude"))) {
                            addAssociatedFunctionCompletions(
                                items, completion, candidate, currentPackage);
                        }
                    }
                    for (std::size_t declaration = 0;
                         declaration < completion.program.enums.size(); ++declaration) {
                        const auto &candidate = completion.program.enums[declaration];
                        if (shortName(candidate.name) == name &&
                            (candidate.builtin != BuiltinEnumKind::None ||
                             candidate.packageName == currentPackage ||
                             candidate.packageName == "std.prelude")) {
                            addValueMemberCompletions(
                                items, completion,
                                Type{TypeKind::Enum, declaration}, currentPackage);
                        }
                    }
                }
                return completionResult(std::move(items));
            }
        }
        if (!attributeContext) {
            constexpr std::string_view keywords[] = {
                "package", "import", "as",      "extern", "struct", "service", "enum",  "contract",
                "attribute", "implements", "extends", "delegate", "methods", "fn", "action", "task", "test",
                "spawn", "unsafe",
                "const",    "var",
                "return",  "discard", "if",      "else",   "while", "for", "in", "break",
                "continue", "select", "timeout", "match", "capture",
                "replace", "with",    "own",     "view",   "edit",   "true",  "false",
            };
            for (const auto keyword : keywords) {
                addCompletion(items, std::string(keyword), 14);
            }
            for (const auto type : {"i8", "i16", "i32", "i64", "u8", "u16", "u32",
                                    "u64", "isize", "usize", "f32", "f64", "bool", "String",
                                    "void", "never", "Option", "Result", "ChannelError",
                                    "NumberError", "Task", "Channel", "Sender", "Receiver"}) {
                addCompletion(items, type, 25);
            }
            addCompletion(items, "print", 3, "fn print(value String) void",
                          "print(${1:value})",
                          "Writes one String followed by a newline without consuming it.");
            addCompletion(items, "panic", 3, "fn panic(message String) never",
                          "panic(${1:message})",
                          "Terminates with a complete Foundation source trace.");
            addCompletion(items, "len", 3,
                          "fn len(value String | [N]T | [T]) usize",
                          "len(${1:value})",
                          "Returns a String byte length or an array or slice element count.");
            addCompletion(items, "null", 3, "fn null<P>() P",
                          "null<${1:*void}>()",
                          "Constructs an explicitly typed null raw pointer inside unsafe.");
            addCompletion(items, "isNull", 3, "fn isNull(pointer P) bool",
                          "isNull(${1:pointer})",
                          "Checks a raw pointer without dereferencing it.");
            addCompletion(items, "channel", 3,
                          "fn channel<T>(capacity u64) Channel<T>",
                          "channel<${1:T}>(${2:capacity})",
                          "Creates owned directional channel endpoints.");
            addCompletion(items, "range", 3,
                          "fn range(start i32, stop i32, step i32) Range",
                          "range(${1:start}, ${2:stop}, step = ${3:1})");
            addCompletion(items, "test", 14, "Declare an isolated Foundation test",
                          "test \"${1:behavior}\" {\n    ${2}\n}");
            addCompletion(items, "expect", 3, "fn expect(condition bool) void",
                          "expect(${1:condition})");
            addCompletion(items, "fail", 3, "fn fail<T>(value T) never",
                          "fail(${1:value})");
            addCompletion(items, "pass", 3, "fn pass() void", "pass()");
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
            if (packageName == "std.prelude" && exported) {
                return std::string(name);
            }
            const auto alias = aliases.find(std::string(packageName));
            if (!exported || alias == aliases.end()) {
                return std::nullopt;
            }
            return alias->second + '.' + std::string(name);
        };

        for (std::size_t declarationIndex = 0;
             declarationIndex < analysis->program.attributeDeclarations.size();
             ++declarationIndex) {
            const auto &declaration =
                analysis->program.attributeDeclarations[declarationIndex];
            const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                         declaration.exported);
            if (!label.has_value()) {
                continue;
            }
            auto application = '@' + *label;
            const auto *symbol = index.symbol(
                {LanguageSymbolKind::Attribute, declarationIndex, 0});
            addCompletion(items, application, 10,
                          symbol == nullptr ? "typed Foundation attribute" : symbol->detail,
                          callSnippet(application, declaration.parameters),
                          symbol == nullptr ? std::string{} : symbol->documentation);
        }
        if (attributeContext) {
            addCompletion(items, "@blocking", 10,
                          "Run a bodyless C ABI import on the blocking executor",
                          "@blocking");
            addCompletion(items, "@callback", 10,
                          "Suspend a task on a native callback operation",
                          "@callback");
        }
        if (!attributeContext) {
            for (std::size_t functionIndex = 0;
                 functionIndex < analysis->program.functions.size(); ++functionIndex) {
                const auto &function = analysis->program.functions[functionIndex];
                if (!function.ownerType.empty() || function.closure ||
                    function.testName.has_value()) {
                    continue;
                }
                const auto label = qualified(function.packageName, shortName(function.name),
                                             function.exported || function.name == "main");
                if (label.has_value()) {
                    const auto *symbol = index.symbol(
                        {LanguageSymbolKind::Function, functionIndex, 0});
                    addCompletion(items, *label, 3, functionDetail(function),
                                  functionCallSnippet(function, *label),
                                  symbol == nullptr ? std::string{}
                                                    : symbol->documentation);
                }
            }
            for (std::size_t declarationIndex = 0;
                 declarationIndex < analysis->program.structs.size(); ++declarationIndex) {
                const auto &declaration = analysis->program.structs[declarationIndex];
                const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                             declaration.exported);
                if (label.has_value()) {
                    const auto *symbol = index.symbol(
                        {LanguageSymbolKind::Struct, declarationIndex, 0});
                    addCompletion(items, *label, 22,
                                  symbol == nullptr ? "Foundation struct" : symbol->detail,
                                  std::nullopt,
                                  symbol == nullptr ? std::string{}
                                                    : symbol->documentation);
                }
            }
            for (std::size_t declarationIndex = 0;
                 declarationIndex < analysis->program.enums.size(); ++declarationIndex) {
                const auto &declaration = analysis->program.enums[declarationIndex];
                if (declaration.builtin == BuiltinEnumKind::None) {
                    const auto label = qualified(declaration.packageName,
                                                 shortName(declaration.name),
                                                 declaration.exported);
                    if (label.has_value()) {
                        const auto *symbol = index.symbol(
                            {LanguageSymbolKind::Enum, declarationIndex, 0});
                        addCompletion(items, *label, 13,
                                      symbol == nullptr ? "Foundation enum" : symbol->detail,
                                      std::nullopt,
                                      symbol == nullptr ? std::string{}
                                                        : symbol->documentation);
                    }
                }
            }
            for (std::size_t declarationIndex = 0;
                 declarationIndex < analysis->program.contracts.size(); ++declarationIndex) {
                const auto &declaration = analysis->program.contracts[declarationIndex];
                const auto label = qualified(declaration.packageName, shortName(declaration.name),
                                             declaration.exported);
                if (label.has_value()) {
                    const auto *symbol = index.symbol(
                        {LanguageSymbolKind::Contract, declarationIndex, 0});
                    addCompletion(items, *label, 8,
                                  symbol == nullptr ? "Foundation contract" : symbol->detail,
                                  std::nullopt,
                                  symbol == nullptr ? std::string{}
                                                    : symbol->documentation);
                }
            }
            if (requested.has_value()) {
                addVisibleLocalCompletions(items, *analysis, *sourceId, *requested,
                                           source.contents);
            }
        }
        return completionResult(std::move(items));
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
        auto nameEnd = open;
        while (nameEnd != 0 && (source[nameEnd - 1] == ' ' || source[nameEnd - 1] == '\t' ||
                                source[nameEnd - 1] == '\r' || source[nameEnd - 1] == '\n')) {
            --nameEnd;
        }
        if (nameEnd != 0 && source[nameEnd - 1] == '>') {
            auto cursor = nameEnd;
            std::size_t angleDepth{};
            while (cursor != 0) {
                --cursor;
                if (source[cursor] == '>') {
                    ++angleDepth;
                } else if (source[cursor] == '<') {
                    if (angleDepth == 1) {
                        nameEnd = cursor;
                        break;
                    }
                    if (angleDepth != 0) {
                        --angleDepth;
                    }
                }
            }
        }
        auto start = nameEnd;
        while (start != 0 && identifierByte(static_cast<unsigned char>(source[start - 1]))) {
            --start;
        }
        if (start == nameEnd) {
            return Json(nullptr);
        }
        const auto name = source.substr(start, nameEnd - start);
        const auto builtinSignature = [](std::string label, std::string documentation,
                                         std::string parameter) {
            Json::Object signature{
                {"label", std::move(label)},
                {"documentation",
                 Json::object({{"kind", "markdown"},
                               {"value", std::move(documentation)}})}};
            if (!parameter.empty()) {
                signature.emplace(
                    "parameters",
                    Json::array({Json::object({{"label", std::move(parameter)}})}));
            }
            return Json::object(
                {{"signatures", Json::array({Json(std::move(signature))})},
                 {"activeSignature", 0},
                 {"activeParameter", 0}});
        };
        if (name == "print") {
            return builtinSignature(
                "fn print(value String) void",
                "Writes one String followed by a newline without consuming it.",
                "value String");
        }
        if (name == "panic") {
            return builtinSignature(
                "fn panic(message String) never",
                "Terminates with a complete Foundation source trace. Recoverable failures use "
                "`Result<T, E>`.",
                "message String");
        }
        if (name == "len") {
            return builtinSignature(
                "fn len(value String | [N]T | [T]) usize",
                "Returns a String byte length or an array or slice element count without "
                "consuming the value.",
                "value String | [N]T | [T]");
        }
        if (name == "channel") {
            return builtinSignature(
                "fn channel<T>(capacity u64) Channel<T>",
                "Creates owned directional channel endpoints.", "capacity u64");
        }
        if (name == "null") {
            return Json::object(
                {{"signatures",
                  Json::array({Json::object(
                      {{"label", "fn null<P>() P"},
                       {"documentation",
                        Json::object({{"kind", "markdown"},
                                      {"value", "Constructs an explicitly typed null raw "
                                                "pointer inside `unsafe`."}})}})})},
                 {"activeSignature", 0}, {"activeParameter", 0}});
        }
        if (name == "isNull") {
            return Json::object(
                {{"signatures",
                  Json::array({Json::object(
                      {{"label", "fn isNull(pointer P) bool"},
                       {"documentation",
                        Json::object({{"kind", "markdown"},
                                      {"value", "Checks a raw pointer without dereferencing "
                                                "it."}})},
                       {"parameters",
                        Json::array({Json::object({{"label", "pointer P"}})})}})})},
                 {"activeSignature", 0}, {"activeParameter", 0}});
        }
        const auto &index = languageIndex(*analysis);
        const auto *occurrence = index.occurrenceAt(*sourceId, start);
        const auto *symbol = occurrence == nullptr ? nullptr : index.symbol(occurrence->symbol);
        if (symbol == nullptr) {
            const auto conversion = numericConversionAt(*analysis, *sourceId, start);
            if (conversion.has_value() && analysis->semantic.has_value()) {
                const auto id = conversion->first;
                const auto &target = *analysis->semantic->callTargets[id];
                if (target.typeArguments.size() == 2) {
                    const auto parameter =
                        "value " + displaySemanticType(*analysis, target.typeArguments[0]);
                    const auto label =
                        "fn From(" + parameter + ") " +
                        displaySemanticType(*analysis,
                                            analysis->semantic->expressionTypes[id]);
                    return Json::object(
                        {{"signatures",
                          Json::array({Json::object(
                              {{"label", label},
                               {"documentation",
                                Json::object(
                                    {{"kind", "markdown"},
                                     {"value",
                                      "Explicit checked numeric conversion."}})},
                               {"parameters",
                                Json::array({Json::object({{"label", parameter}})})}})})},
                         {"activeSignature", 0},
                         {"activeParameter", 0}});
                }
            }
            const auto operation = channelOperationAt(*analysis, *sourceId, start);
            const auto senderClone = channelSenderCloneAt(*analysis, *sourceId, start);
            if (senderClone.has_value() && analysis->semantic.has_value()) {
                const auto id = senderClone->first;
                const auto label =
                    "fn clone() " + displaySemanticType(
                                         *analysis,
                                         analysis->semantic->expressionTypes[id]);
                Json::Object signature{
                    {"label", label},
                    {"documentation",
                     Json::object(
                         {{"kind", "markdown"},
                          {"value", "Creates another owned sender handle for the same "
                                    "channel."}})}};
                return Json::object(
                    {{"signatures",
                      Json::array({Json(std::move(signature))})},
                     {"activeSignature", 0},
                     {"activeParameter", 0}});
            }
            if (operation.has_value() && analysis->semantic.has_value()) {
                const auto id = operation->first;
                const auto &target = *analysis->semantic->channelOperationTargets[id];
                const auto &member =
                    std::get<MemberExpression>(analysis->program.expressions[id].value);
                const auto endpoint = member.base.has_value()
                                          ? analysis->semantic->expressionTypes[*member.base]
                                          : invalidType;
                auto label = std::string("fn ") + member.member + '(';
                Json::Array parameters;
                if (target.kind == ChannelOperationKind::Send &&
                    endpoint.arguments.size() == 1 && endpoint.arguments.front() != voidType) {
                    const auto parameter =
                        "value " + displaySemanticType(*analysis, endpoint.arguments.front());
                    label += parameter;
                    parameters.push_back(Json::object({{"label", parameter},
                                                       {"documentation",
                                                        "Value ownership transfers only when "
                                                        "the send succeeds."}}));
                }
                label += ") " + displaySemanticType(
                                     *analysis, analysis->semantic->expressionTypes[id]);
                Json::Object signature{{"label", label}};
                if (!parameters.empty()) {
                    signature.emplace("parameters", Json(std::move(parameters)));
                }
                return Json::object(
                    {{"signatures", Json::array({Json(std::move(signature))})},
                     {"activeSignature", 0}, {"activeParameter", 0}});
            }
            return Json(nullptr);
        }
        std::size_t activeParameter{};
        auto argumentStart = open + 1;
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
                argumentStart = offset + 1;
            }
        }
        Json::Object signature{{"label", symbol->detail}};
        if (!symbol->documentation.empty()) {
            signature.emplace("documentation",
                              Json::object({{"kind", "markdown"},
                                            {"value", symbol->documentation}}));
        }
        auto signatureTypes = typeTargets(*analysis, index, *symbol);
        if (!signatureTypes.empty()) {
            signature.emplace("foundationTypes", Json(std::move(signatureTypes)));
        }
        Json::Array parameters;
        std::vector<std::string> parameterNames;
        const auto appendParameters = [this, &parameters, &parameterNames, &analysis, &index](
                                          const std::vector<Parameter> &declarations,
                                          std::size_t first = 0) {
            for (auto parameter = first; parameter < declarations.size(); ++parameter) {
                const auto &declaration = declarations[parameter];
                Json::Object item{{"label", parameterDetail(declaration)}};
                const auto documentation =
                    languageParameterDocumentation(*analysis, declaration);
                if (!documentation.empty()) {
                    item.emplace("documentation",
                                 Json::object({{"kind", "markdown"},
                                               {"value", documentation}}));
                }
                auto types = typeTargets(*analysis, index, declaration.type);
                if (!types.empty()) {
                    item.emplace("foundationTypes", Json(std::move(types)));
                }
                parameterNames.push_back(declaration.name);
                parameters.push_back(Json(std::move(item)));
            }
        };
        if ((symbol->id.kind == LanguageSymbolKind::Function ||
             symbol->id.kind == LanguageSymbolKind::Method) &&
            symbol->id.owner < analysis->program.functions.size()) {
            const auto &function = analysis->program.functions[symbol->id.owner];
            appendParameters(function.parameters, function.receiver.has_value() ? 1U : 0U);
        } else if (symbol->id.kind == LanguageSymbolKind::ContractMethod &&
                   symbol->id.owner < analysis->program.contracts.size() &&
                   symbol->id.member <
                       analysis->program.contracts[symbol->id.owner].methods.size()) {
            appendParameters(
                analysis->program.contracts[symbol->id.owner]
                    .methods[symbol->id.member]
                    .parameters);
        } else if (symbol->id.kind == LanguageSymbolKind::Attribute &&
                   symbol->id.owner < analysis->program.attributeDeclarations.size()) {
            appendParameters(
                analysis->program.attributeDeclarations[symbol->id.owner].parameters);
        } else if (symbol->id.kind == LanguageSymbolKind::EnumVariant &&
                   symbol->id.owner < analysis->program.enums.size() &&
                   symbol->id.member <
                       analysis->program.enums[symbol->id.owner].variants.size()) {
            const auto &variant =
                analysis->program.enums[symbol->id.owner].variants[symbol->id.member];
            if (variant.payloadType.has_value()) {
                const auto name = variant.payloadName.value_or("value");
                parameters.push_back(Json::object(
                    {{"label", name + ' ' + displayTypeSyntax(*variant.payloadType)}}));
                parameterNames.push_back(name);
            }
        }
        if (!parameters.empty()) {
            signature.emplace("parameters", Json(std::move(parameters)));
        }
        while (argumentStart < *requested &&
               std::isspace(static_cast<unsigned char>(source[argumentStart])) != 0) {
            ++argumentStart;
        }
        auto argumentNameEnd = argumentStart;
        while (argumentNameEnd < *requested &&
               identifierByte(static_cast<unsigned char>(source[argumentNameEnd]))) {
            ++argumentNameEnd;
        }
        auto equals = argumentNameEnd;
        while (equals < *requested &&
               std::isspace(static_cast<unsigned char>(source[equals])) != 0) {
            ++equals;
        }
        if (argumentNameEnd != argumentStart && equals < *requested && source[equals] == '=' &&
            (equals + 1 >= source.size() || source[equals + 1] != '=')) {
            const auto name = source.substr(argumentStart, argumentNameEnd - argumentStart);
            const auto found = std::find(parameterNames.begin(), parameterNames.end(), name);
            if (found != parameterNames.end()) {
                activeParameter = static_cast<std::size_t>(found - parameterNames.begin());
            }
        }
        return Json::object(
            {{"signatures", Json::array({Json(std::move(signature))})},
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
        case LanguageSymbolKind::EnumPayload:
            return 8;
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
        for (AstExpressionId id = 0;
             id < analysis->program.expressions.size() &&
             id < analysis->semantic->emptyTests.size(); ++id) {
            if (!analysis->semantic->emptyTests[id]) {
                continue;
            }
            const auto &expression = analysis->program.expressions[id];
            if (expression.span.source != *sourceId || expression.span.offset < *start ||
                expression.span.offset >= *end) {
                continue;
            }
            result.push_back(Json::object(
                {{"position",
                  lspPosition(positionAt(source,
                                         expression.span.offset + expression.span.length))},
                 {"label", "is empty"},
                 {"kind", 1},
                 {"foundationKind", "emptyTest"},
                 {"paddingLeft", true}}));
        }
        for (std::size_t id = 0; id < analysis->program.expressions.size(); ++id) {
            std::vector<std::string> names;
            if (analysis->semantic->callTargets[id].has_value()) {
                names = parameterNames(*analysis, *analysis->semantic->callTargets[id]);
            } else if (analysis->semantic->channelOperationTargets[id].has_value() &&
                       analysis->semantic->channelOperationTargets[id]->kind ==
                           ChannelOperationKind::Send) {
                names.push_back("value");
            } else {
                continue;
            }
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

    [[nodiscard]] const OpenDocument *openDocument(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto document = uri.has_value() ? documents_.find(*uri) : documents_.end();
        return document == documents_.end() ? nullptr : &document->second;
    }

    [[nodiscard]] Json provideCodeActions(const Json *params) const {
        const auto *textDocument = params == nullptr ? nullptr : params->find("textDocument");
        const auto uri = stringField(textDocument, "uri");
        const auto *range = params == nullptr ? nullptr : params->find("range");
        const auto startPosition = jsonPosition(range == nullptr ? nullptr : range->find("start"));
        const auto endPosition = jsonPosition(range == nullptr ? nullptr : range->find("end"));
        if (!uri.has_value() || !startPosition.has_value() || !endPosition.has_value()) {
            return Json(Json::Array{});
        }
        const auto *analysis = analyzeUri(*uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, *uri);
        if (analysis == nullptr || !sourceId.has_value()) {
            return Json(Json::Array{});
        }
        const auto &source = analysis->sources[*sourceId].contents;
        const auto start = offsetAt(source, *startPosition);
        const auto end = offsetAt(source, *endPosition);
        if (!start.has_value() || !end.has_value()) {
            return Json(Json::Array{});
        }

        Json::Array result;
        std::set<std::pair<std::size_t, std::size_t>> emitted;
        for (const auto &diagnostic : analysis->diagnostics.all()) {
            if (diagnostic.span.source != *sourceId ||
                (diagnostic.code != "FDN2051" && diagnostic.code != "FDN2076")) {
                continue;
            }
            const auto diagnosticEnd = diagnostic.span.offset + diagnostic.span.length;
            const auto selected = *start == *end
                                      ? diagnostic.span.offset <= *start &&
                                            *start <= diagnosticEnd
                                      : diagnostic.span.offset < *end && diagnosticEnd > *start;
            if (!selected ||
                !emitted.emplace(diagnostic.span.offset, diagnostic.span.length).second) {
                continue;
            }
            const auto insertion = positionAt(source, diagnostic.span.offset);
            Json::Object changes;
            changes.emplace(
                *uri,
                Json::array({Json::object({{"range", lspRange(insertion, insertion)},
                                           {"newText", "discard "}})}));
            result.push_back(Json::object(
                {{"title", "Handle explicitly with discard"},
                 {"kind", "quickfix"},
                 {"isPreferred", true},
                 {"edit", Json::object({{"changes", Json(std::move(changes))}})}}));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideFoldingRanges(const Json *params) const {
        const auto *document = openDocument(params);
        if (document == nullptr) {
            return Json(Json::Array{});
        }
        Json::Array result;
        for (const auto &delimiter : delimiterRanges(document->contents, 0)) {
            if (delimiter.opening != TokenKind::LeftBrace) {
                continue;
            }
            const auto start = positionAt(document->contents, delimiter.span.offset);
            const auto closing = positionAt(
                document->contents, delimiter.span.offset + delimiter.span.length - 1);
            if (closing.line <= start.line + 1) {
                continue;
            }
            result.push_back(Json::object(
                {{"startLine", static_cast<double>(start.line)},
                 {"endLine", static_cast<double>(closing.line - 1)}}));
        }

        const auto uri = document->uri;
        const auto *analysis = analyzeUri(uri);
        const auto sourceId = analysis == nullptr ? std::nullopt
                                                  : sourceIdForUri(*analysis, uri);
        if (analysis != nullptr && sourceId.has_value()) {
            std::vector<std::size_t> lines;
            for (const auto &imported : analysis->program.imports) {
                if (imported.span.source == *sourceId) {
                    lines.push_back(imported.span.line - 1);
                }
            }
            std::sort(lines.begin(), lines.end());
            if (lines.size() > 1 && lines.front() < lines.back()) {
                result.push_back(Json::object(
                    {{"startLine", static_cast<double>(lines.front())},
                     {"endLine", static_cast<double>(lines.back())}, {"kind", "imports"}}));
            }
        }
        std::sort(result.begin(), result.end(), [](const Json &left, const Json &right) {
            const auto *leftStart = left.find("startLine")->asNumber();
            const auto *rightStart = right.find("startLine")->asNumber();
            if (*leftStart != *rightStart) {
                return *leftStart < *rightStart;
            }
            return *left.find("endLine")->asNumber() > *right.find("endLine")->asNumber();
        });
        return Json(std::move(result));
    }

    [[nodiscard]] static Json selectionRange(std::string_view source, Position position,
                                             const std::vector<DelimiterRange> &delimiters) {
        const auto offset = offsetAt(source, position).value_or(source.size());
        std::vector<SourceSpan> candidates;
        if (const auto token = tokenAt(source, offset, 0); token.has_value()) {
            candidates.push_back(*token);
        }
        for (const auto &delimiter : delimiters) {
            const auto end = delimiter.span.offset + delimiter.span.length;
            if (delimiter.span.offset <= offset && offset < end) {
                candidates.push_back(delimiter.span);
            }
        }
        candidates.push_back({0, source.size(), 1, 1, 0});
        std::sort(candidates.begin(), candidates.end(), [](SourceSpan left, SourceSpan right) {
            if (left.length != right.length) {
                return left.length < right.length;
            }
            return left.offset > right.offset;
        });

        std::vector<SourceSpan> chain;
        for (const auto candidate : candidates) {
            if (!chain.empty()) {
                const auto child = chain.back();
                const auto duplicate = candidate.offset == child.offset &&
                                       candidate.length == child.length;
                const auto contains = candidate.offset <= child.offset &&
                                      candidate.offset + candidate.length >=
                                          child.offset + child.length;
                if (duplicate || !contains) {
                    continue;
                }
            }
            chain.push_back(candidate);
        }
        constexpr std::size_t maxSelectionRanges = 256;
        if (chain.size() > maxSelectionRanges) {
            const auto document = chain.back();
            chain.resize(maxSelectionRanges - 1);
            chain.push_back(document);
        }
        auto current = Json::object({{"range", lspRange(source, chain.back())}});
        for (auto index = chain.size() - 1; index != 0; --index) {
            current = Json::object({{"range", lspRange(source, chain[index - 1])},
                                    {"parent", std::move(current)}});
        }
        return current;
    }

    [[nodiscard]] Json provideSelectionRanges(const Json *params) const {
        const auto *document = openDocument(params);
        const auto *positions = params == nullptr ? nullptr : params->find("positions");
        if (document == nullptr || positions == nullptr || positions->asArray() == nullptr) {
            return Json(Json::Array{});
        }
        const auto delimiters = delimiterRanges(document->contents, 0);
        Json::Array result;
        result.reserve(positions->asArray()->size());
        for (const auto &value : *positions->asArray()) {
            const auto position = jsonPosition(&value).value_or(
                positionAt(document->contents, document->contents.size()));
            result.push_back(selectionRange(document->contents, position, delimiters));
        }
        return Json(std::move(result));
    }

    [[nodiscard]] Json provideFormatting(const Json *params) const {
        const auto *document = openDocument(params);
        if (document == nullptr) {
            return Json(Json::Array{});
        }
        const auto formatted = formatSource(document->contents);
        if (formatted.diagnostics.hasErrors() || formatted.contents == document->contents) {
            return Json(Json::Array{});
        }
        const auto end = positionAt(document->contents, document->contents.size());
        return Json::array({Json::object({{"range", lspRange(Position{}, end)},
                                          {"newText", formatted.contents}})});
    }

    [[nodiscard]] Json provideRangeFormatting(const Json *params) const {
        const auto *document = openDocument(params);
        const auto *range = params == nullptr ? nullptr : params->find("range");
        const auto start = jsonPosition(range == nullptr ? nullptr : range->find("start"));
        const auto end = jsonPosition(range == nullptr ? nullptr : range->find("end"));
        if (document == nullptr || !start.has_value() || !end.has_value()) {
            return Json(Json::Array{});
        }
        const auto formatted = formatSource(document->contents);
        if (formatted.diagnostics.hasErrors() || formatted.contents == document->contents) {
            return Json(Json::Array{});
        }
        const auto originalLines = textLines(document->contents);
        const auto formattedLines = textLines(formatted.contents);
        if (originalLines.size() != formattedLines.size() || start->line >= originalLines.size() ||
            end->line >= originalLines.size() || end->line < start->line) {
            return Json(Json::Array{});
        }
        auto last = end->line;
        if (last != start->line && end->character == 0) {
            --last;
        }
        Json::Array edits;
        for (auto line = start->line; line <= last; ++line) {
            const auto original = document->contents.substr(originalLines[line].offset,
                                                            originalLines[line].length);
            const auto replacement = formatted.contents.substr(formattedLines[line].offset,
                                                                formattedLines[line].length);
            if (original == replacement) {
                continue;
            }
            const auto lineEnd = positionAt(document->contents,
                                            originalLines[line].offset +
                                                originalLines[line].length);
            edits.push_back(Json::object(
                {{"range", lspRange(Position{line, 0}, lineEnd)},
                 {"newText", std::string(replacement)}}));
        }
        return Json(std::move(edits));
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
