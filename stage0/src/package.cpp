#include "foundation/package.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace foundation {

namespace {

constexpr std::size_t maxPackageFileBytes = 1024U * 1024U;
constexpr std::size_t maxPackageEntries = 4096;

struct LineTokens {
    std::vector<std::string> values;
    std::optional<std::size_t> errorColumn;
    std::string error;
};

bool identifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    auto segmentStart = true;
    for (const auto byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        if (byte == '.') {
            if (segmentStart) {
                return false;
            }
            segmentStart = true;
            continue;
        }
        if (segmentStart) {
            if (!std::isalpha(character)) {
                return false;
            }
            segmentStart = false;
        } else if (!std::isalnum(character) && byte != '-' && byte != '_') {
            return false;
        }
    }
    return !segmentStart;
}

bool versionIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        const auto character = static_cast<unsigned char>(byte);
        return std::isalnum(character) || byte == '-';
    });
}

std::optional<std::size_t> number(std::string_view text) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::size_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::string>> identifiers(std::string_view text,
                                                     bool rejectLeadingZero) {
    std::vector<std::string> result;
    auto start = std::size_t{};
    while (start <= text.size()) {
        const auto end = text.find('.', start);
        const auto value = text.substr(start, end == std::string_view::npos
                                                  ? text.size() - start
                                                  : end - start);
        if (!versionIdentifier(value) ||
            (rejectLeadingZero && value.size() > 1 &&
             std::all_of(value.begin(), value.end(), [](const auto byte) {
                 return std::isdigit(static_cast<unsigned char>(byte));
             }) &&
             value.front() == '0')) {
            return std::nullopt;
        }
        result.emplace_back(value);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::string join(const std::vector<std::string> &values) {
    std::string result;
    for (const auto &value : values) {
        if (!result.empty()) {
            result.push_back('.');
        }
        result += value;
    }
    return result;
}

std::strong_ordering comparePrerelease(const std::vector<std::string> &left,
                                       const std::vector<std::string> &right) {
    if (left.empty() || right.empty()) {
        if (left.empty() && right.empty()) {
            return std::strong_ordering::equal;
        }
        return left.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto leftNumber = number(left[index]);
        const auto rightNumber = number(right[index]);
        if (leftNumber.has_value() && rightNumber.has_value()) {
            if (*leftNumber != *rightNumber) {
                return *leftNumber < *rightNumber ? std::strong_ordering::less
                                                  : std::strong_ordering::greater;
            }
        } else if (leftNumber.has_value() != rightNumber.has_value()) {
            return leftNumber.has_value() ? std::strong_ordering::less
                                          : std::strong_ordering::greater;
        } else if (left[index] != right[index]) {
            return left[index] < right[index] ? std::strong_ordering::less
                                              : std::strong_ordering::greater;
        }
    }
    return left.size() <=> right.size();
}

std::strong_ordering comparePrecedence(const PackageVersion &left,
                                       const PackageVersion &right) {
    if (const auto core = std::tie(left.major, left.minor, left.patch) <=>
                          std::tie(right.major, right.minor, right.patch);
        core != std::strong_ordering::equal) {
        return core;
    }
    return comparePrerelease(left.prerelease, right.prerelease);
}

PackageVersion upperBound(const PackageRequirement &requirement) {
    auto result = requirement.version;
    result.prerelease.clear();
    result.build.clear();
    if (requirement.kind == PackageRequirementKind::Tilde) {
        ++result.minor;
        result.patch = 0;
    } else if (result.major != 0) {
        ++result.major;
        result.minor = 0;
        result.patch = 0;
    } else if (result.minor != 0) {
        ++result.minor;
        result.patch = 0;
    } else {
        ++result.patch;
    }
    return result;
}

void addError(std::vector<PackageError> &errors, const std::filesystem::path &path,
              std::size_t line, std::size_t column, std::string code,
              std::string message) {
    errors.push_back({path, line, column, std::move(code), std::move(message)});
}

LineTokens tokenize(std::string_view line) {
    LineTokens result;
    auto offset = std::size_t{};
    while (offset < line.size()) {
        while (offset < line.size() &&
               std::isspace(static_cast<unsigned char>(line[offset]))) {
            ++offset;
        }
        if (offset == line.size() || line[offset] == '#') {
            break;
        }
        if (line[offset] != '"') {
            const auto start = offset;
            while (offset < line.size() &&
                   !std::isspace(static_cast<unsigned char>(line[offset])) &&
                   line[offset] != '#') {
                ++offset;
            }
            result.values.emplace_back(line.substr(start, offset - start));
            continue;
        }
        const auto start = offset++;
        std::string value;
        auto closed = false;
        while (offset < line.size()) {
            const auto byte = line[offset++];
            if (byte == '"') {
                closed = true;
                break;
            }
            if (byte != '\\') {
                value.push_back(byte);
                continue;
            }
            if (offset == line.size()) {
                break;
            }
            const auto escaped = line[offset++];
            if (escaped == '"' || escaped == '\\') {
                value.push_back(escaped);
            } else if (escaped == 'n') {
                value.push_back('\n');
            } else if (escaped == 't') {
                value.push_back('\t');
            } else {
                result.errorColumn = offset;
                result.error = "unsupported escape in quoted value";
                return result;
            }
        }
        if (!closed) {
            result.errorColumn = start + 1;
            result.error = "unterminated quoted value";
            return result;
        }
        if (offset < line.size() &&
            !std::isspace(static_cast<unsigned char>(line[offset])) &&
            line[offset] != '#') {
            result.errorColumn = offset + 1;
            result.error = "quoted value must end before the next token";
            return result;
        }
        result.values.push_back(std::move(value));
    }
    return result;
}

std::string quote(std::string_view value) {
    auto required = value.empty();
    for (const auto byte : value) {
        required = required || std::isspace(static_cast<unsigned char>(byte)) || byte == '#' ||
                   byte == '"' || byte == '\\';
    }
    if (!required) {
        return std::string(value);
    }
    std::string result{"\""};
    for (const auto byte : value) {
        if (byte == '"' || byte == '\\') {
            result.push_back('\\');
            result.push_back(byte);
        } else if (byte == '\n') {
            result += "\\n";
        } else if (byte == '\t') {
            result += "\\t";
        } else {
            result.push_back(byte);
        }
    }
    result.push_back('"');
    return result;
}

std::optional<PackageLocationKind> locationKind(std::string_view value) {
    if (value == "path") {
        return PackageLocationKind::Path;
    }
    if (value == "registry") {
        return PackageLocationKind::Registry;
    }
    return std::nullopt;
}

std::string locationName(PackageLocationKind kind) {
    return kind == PackageLocationKind::Path ? "path" : "registry";
}

std::string scopeName(PackageDependencyScope scope) {
    return scope == PackageDependencyScope::Test ? "test" : "runtime";
}

bool nestedSource(const std::filesystem::path &left, const std::filesystem::path &right) {
    const auto normalizedLeft = left.lexically_normal();
    const auto normalizedRight = right.lexically_normal();
    auto leftPart = normalizedLeft.begin();
    auto rightPart = normalizedRight.begin();
    while (leftPart != normalizedLeft.end() && rightPart != normalizedRight.end() &&
           *leftPart == *rightPart) {
        ++leftPart;
        ++rightPart;
    }
    return leftPart == normalizedLeft.end() || rightPart == normalizedRight.end();
}

std::optional<TargetPlatform> target(std::string_view value) {
    if (value == "linux") {
        return TargetPlatform::Linux;
    }
    if (value == "macos") {
        return TargetPlatform::MacOS;
    }
    if (value == "windows") {
        return TargetPlatform::Windows;
    }
    return std::nullopt;
}

std::optional<std::string> readPackageFile(const std::filesystem::path &path,
                                           std::vector<PackageError> &errors) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        addError(errors, path, 1, 1, "FDN4001", "cannot read package file");
        return std::nullopt;
    }
    std::string result;
    char buffer[8192];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() != 0) {
        const auto count = static_cast<std::size_t>(input.gcount());
        if (result.size() > maxPackageFileBytes - count) {
            addError(errors, path, 1, 1, "FDN4002", "package file exceeds 1 MiB");
            return std::nullopt;
        }
        result.append(buffer, count);
    }
    if (input.bad()) {
        addError(errors, path, 1, 1, "FDN4001", "cannot read package file");
        return std::nullopt;
    }
    return result;
}

bool validLocation(PackageLocationKind kind, std::string_view value);

bool validateLockGraph(const std::filesystem::path &path, const PackageLock &lock,
                       std::vector<PackageError> &errors) {
    std::set<std::string> packages;
    for (const auto &package : lock.packages) {
        if (package.name == lock.rootName ||
            !validLocation(package.kind, package.location)) {
            addError(errors, path, 1, 1, "FDN4027",
                     "locked package has an invalid source identity");
            return false;
        }
        packages.insert(package.name);
    }

    std::map<std::string, std::vector<std::string>> graph;
    std::map<std::string, std::size_t> incoming;
    graph[lock.rootName];
    incoming[lock.rootName] = 0;
    for (const auto &name : packages) {
        graph[name];
        incoming[name] = 0;
    }
    std::set<std::pair<std::string, std::string>> uniqueEdges;
    for (const auto &edge : lock.edges) {
        if ((edge.parent != lock.rootName && !packages.contains(edge.parent)) ||
            !packages.contains(edge.dependency) ||
            (edge.scope == PackageDependencyScope::Test &&
             edge.parent != lock.rootName) ||
            !uniqueEdges.emplace(edge.parent, edge.dependency).second) {
            addError(errors, path, 1, 1, "FDN4027",
                     "lock contains an unknown or duplicate package edge");
            return false;
        }
        graph[edge.parent].push_back(edge.dependency);
        ++incoming[edge.dependency];
    }

    std::vector<std::string> pending{lock.rootName};
    std::set<std::string> reached;
    while (!pending.empty()) {
        auto name = std::move(pending.back());
        pending.pop_back();
        if (!reached.insert(name).second) {
            continue;
        }
        for (const auto &dependency : graph[name]) {
            pending.push_back(dependency);
        }
    }
    if (reached.size() != packages.size() + 1) {
        addError(errors, path, 1, 1, "FDN4027",
                 "lock contains a package unreachable from the root");
        return false;
    }

    pending.clear();
    for (const auto &[name, count] : incoming) {
        if (count == 0) {
            pending.push_back(name);
        }
    }
    auto visited = std::size_t{};
    while (!pending.empty()) {
        const auto name = std::move(pending.back());
        pending.pop_back();
        ++visited;
        for (const auto &dependency : graph[name]) {
            auto &count = incoming[dependency];
            --count;
            if (count == 0) {
                pending.push_back(dependency);
            }
        }
    }
    if (visited != packages.size() + 1) {
        addError(errors, path, 1, 1, "FDN4027", "lock contains a package cycle");
        return false;
    }
    return true;
}

template <typename Callback>
void lines(const std::filesystem::path &path, std::string_view source,
           std::vector<PackageError> &errors, Callback callback) {
    auto lineNumber = std::size_t{1};
    auto start = std::size_t{};
    while (start <= source.size()) {
        const auto end = source.find('\n', start);
        auto line = source.substr(start, end == std::string_view::npos
                                            ? source.size() - start
                                            : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto tokens = tokenize(line);
        if (tokens.errorColumn.has_value()) {
            addError(errors, path, lineNumber, *tokens.errorColumn, "FDN4003", tokens.error);
        } else if (!tokens.values.empty()) {
            callback(tokens.values, lineNumber);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        ++lineNumber;
    }
}

bool relativeSource(std::string_view value) {
    const std::filesystem::path path{value};
    if (path.empty() || path.has_root_path() || path.lexically_normal() == ".") {
        return false;
    }
    for (const auto &part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool validLocation(PackageLocationKind kind, std::string_view value) {
    if (value.empty() ||
        std::any_of(value.begin(), value.end(), [](const auto byte) {
            return static_cast<unsigned char>(byte) < 0x20U;
        })) {
        return false;
    }
    if (kind == PackageLocationKind::Registry) {
        return identifier(value);
    }
    return !std::filesystem::path(value).has_root_path();
}

bool digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    return value.starts_with(prefix) && value.size() == prefix.size() + 64 &&
           std::all_of(value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(),
                       [](const auto byte) {
                           return std::isdigit(static_cast<unsigned char>(byte)) ||
                                  (byte >= 'a' && byte <= 'f');
                       });
}

} // namespace

std::string PackageVersion::string() const {
    auto result = std::to_string(major) + '.' + std::to_string(minor) + '.' +
                  std::to_string(patch);
    if (!prerelease.empty()) {
        result += '-' + join(prerelease);
    }
    if (!build.empty()) {
        result += '+' + join(build);
    }
    return result;
}

bool isValidPackageName(std::string_view value) { return identifier(value); }

std::strong_ordering PackageVersion::operator<=>(const PackageVersion &other) const {
    if (const auto precedence = comparePrecedence(*this, other);
        precedence != std::strong_ordering::equal) {
        return precedence;
    }
    return build <=> other.build;
}

bool PackageVersion::operator==(const PackageVersion &other) const {
    return major == other.major && minor == other.minor && patch == other.patch &&
           prerelease == other.prerelease && build == other.build;
}

std::string PackageRequirement::string() const {
    switch (kind) {
    case PackageRequirementKind::Any:
        return "*";
    case PackageRequirementKind::Exact:
        return version.string();
    case PackageRequirementKind::Caret:
        return '^' + version.string();
    case PackageRequirementKind::Tilde:
        return '~' + version.string();
    }
    return version.string();
}

bool PackageRequirement::accepts(const PackageVersion &candidate) const {
    if (kind == PackageRequirementKind::Any) {
        return true;
    }
    if (kind == PackageRequirementKind::Exact) {
        return candidate == version;
    }
    return comparePrecedence(candidate, version) != std::strong_ordering::less &&
           comparePrecedence(candidate, upperBound(*this)) == std::strong_ordering::less;
}

std::optional<PackageVersion> parsePackageVersion(std::string_view text) {
    PackageVersion result;
    const auto buildOffset = text.find('+');
    const auto prereleaseOffset = text.find('-');
    const auto coreEnd = std::min(buildOffset, prereleaseOffset);
    const auto core = text.substr(0, coreEnd);
    const auto first = core.find('.');
    const auto second = first == std::string_view::npos
                            ? std::string_view::npos
                            : core.find('.', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        core.find('.', second + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = number(core.substr(0, first));
    const auto minor = number(core.substr(first + 1, second - first - 1));
    const auto patch = number(core.substr(second + 1));
    if (!major.has_value() || !minor.has_value() || !patch.has_value()) {
        return std::nullopt;
    }
    result.major = *major;
    result.minor = *minor;
    result.patch = *patch;
    if (prereleaseOffset != std::string_view::npos) {
        const auto end = buildOffset == std::string_view::npos ? text.size() : buildOffset;
        if (buildOffset < prereleaseOffset) {
            return std::nullopt;
        }
        const auto parsed = identifiers(
            text.substr(prereleaseOffset + 1, end - prereleaseOffset - 1), true);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        result.prerelease = *parsed;
    }
    if (buildOffset != std::string_view::npos) {
        const auto parsed = identifiers(text.substr(buildOffset + 1), false);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        result.build = *parsed;
    }
    return result;
}

std::optional<PackageRequirement> parsePackageRequirement(std::string_view text) {
    if (text == "*") {
        return PackageRequirement{PackageRequirementKind::Any, {}};
    }
    auto kind = PackageRequirementKind::Exact;
    if (!text.empty() && (text.front() == '^' || text.front() == '~')) {
        kind = text.front() == '^' ? PackageRequirementKind::Caret
                                   : PackageRequirementKind::Tilde;
        text.remove_prefix(1);
    }
    const auto version = parsePackageVersion(text);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return PackageRequirement{kind, *version};
}

PackageParseResult<PackageManifest>
parsePackageManifest(const std::filesystem::path &path, std::string_view source) {
    PackageParseResult<PackageManifest> result;
    PackageManifest manifest;
    auto formatSeen = false;
    auto nameSeen = false;
    auto versionSeen = false;
    auto sdkSeen = false;
    auto codeStandardSeen = false;
    std::set<std::string> codeStandardRules;
    auto sourceSeen = false;
    auto testSourceSeen = false;
    auto nativeLibrarySeen = false;
    auto nativeNameSeen = false;
    auto nativeSOVersionSeen = false;
    lines(path, source, result.errors, [&](const auto &tokens, std::size_t line) {
        const auto &directive = tokens.front();
        if (directive == "format") {
            if (tokens.size() != 2 || tokens[1] != "foundation.package/v1" || formatSeen) {
                addError(result.errors, path, line, 1, "FDN4004",
                         "expected one format foundation.package/v1 directive");
            }
            formatSeen = true;
        } else if (directive == "name") {
            if (tokens.size() != 2 || !identifier(tokens[1]) || nameSeen) {
                addError(result.errors, path, line, 1, "FDN4005",
                         "expected one valid package name");
            } else {
                manifest.name = tokens[1];
            }
            nameSeen = true;
        } else if (directive == "version") {
            const auto parsed = tokens.size() == 2 ? parsePackageVersion(tokens[1])
                                                   : std::nullopt;
            if (!parsed.has_value() || versionSeen) {
                addError(result.errors, path, line, 1, "FDN4006",
                         "expected one semantic package version");
            } else {
                manifest.version = *parsed;
            }
            versionSeen = true;
        } else if (directive == "sdk") {
            const auto parsed = tokens.size() == 2 ? parsePackageRequirement(tokens[1])
                                                   : std::nullopt;
            if (!parsed.has_value() || sdkSeen) {
                addError(result.errors, path, line, 1, "FDN4007",
                         "expected one SDK version requirement");
            } else {
                manifest.sdk = *parsed;
            }
            sdkSeen = true;
        } else if (directive == "fcs") {
            const auto parsed = tokens.size() == 2
                                    ? parseCodeStandardProfile(tokens[1])
                                    : std::nullopt;
            if (!parsed.has_value() || codeStandardSeen) {
                addError(result.errors, path, line, 1, "FDN4014",
                         "expected at most one FCS profile: valid, standard, or strict");
            } else {
                manifest.codeStandard = *parsed;
                manifest.codeStandardExplicit = true;
            }
            codeStandardSeen = true;
        } else if (directive == "fcs_rule") {
            const auto severity = tokens.size() == 3
                                      ? parseCodeStandardSeverity(tokens[2])
                                      : std::nullopt;
            if (!severity.has_value() || !configurableCodeStandardRule(tokens[1]) ||
                !codeStandardRules.insert(tokens[1]).second ||
                manifest.codeStandardRules.size() == maxPackageEntries) {
                addError(result.errors, path, line, 1, "FDN4014",
                         "fcs_rule requires one unique configurable FCS code and severity: off, warning, or error");
            } else {
                manifest.codeStandardRules.push_back({tokens[1], *severity});
            }
        } else if (directive == "source") {
            if (tokens.size() != 2 || !relativeSource(tokens[1]) || sourceSeen) {
                addError(result.errors, path, line, 1, "FDN4008",
                         "expected one relative source directory without ..");
            } else {
                manifest.source = tokens[1];
            }
            sourceSeen = true;
        } else if (directive == "test_source") {
            if (tokens.size() != 2 || !relativeSource(tokens[1]) || testSourceSeen) {
                addError(result.errors, path, line, 1, "FDN4013",
                         "expected at most one relative test source directory without ..");
            } else {
                manifest.testSource = tokens[1];
            }
            testSourceSeen = true;
        } else if (directive == "native_library") {
            if (tokens.size() != 2 || tokens[1] != "c" || nativeLibrarySeen) {
                addError(result.errors, path, line, 1, "FDN4015",
                         "expected one native_library c directive");
            } else {
                manifest.nativeLibrary = true;
            }
            nativeLibrarySeen = true;
        } else if (directive == "native_name") {
            if (tokens.size() != 2 || !identifier(tokens[1]) || nativeNameSeen) {
                addError(result.errors, path, line, 1, "FDN4015",
                         "expected one valid native library name");
            } else {
                manifest.nativeName = tokens[1];
            }
            nativeNameSeen = true;
        } else if (directive == "native_soversion") {
            const auto parsed = tokens.size() == 2 ? number(tokens[1]) : std::nullopt;
            if (!parsed.has_value() ||
                *parsed > std::numeric_limits<std::uint32_t>::max() ||
                nativeSOVersionSeen) {
                addError(result.errors, path, line, 1, "FDN4015",
                         "expected one native library SOVERSION");
            } else {
                manifest.nativeSOVersion = static_cast<std::uint32_t>(*parsed);
            }
            nativeSOVersionSeen = true;
        } else if (directive == "native_link") {
            const auto linkTarget = tokens.size() == 4 && tokens[2] == "target"
                                        ? target(tokens[3])
                                        : std::optional<TargetPlatform>{};
            const auto valid = (tokens.size() == 2 || tokens.size() == 4) &&
                               identifier(tokens[1]) &&
                               (tokens.size() == 2 || linkTarget.has_value());
            const auto duplicate = valid &&
                                   std::find_if(manifest.nativeLinks.begin(),
                                                manifest.nativeLinks.end(),
                                                [&](const auto &entry) {
                                                    return entry.library == tokens[1] &&
                                                           (!entry.target.has_value() ||
                                                            !linkTarget.has_value() ||
                                                            entry.target == linkTarget);
                                                }) != manifest.nativeLinks.end();
            if (!valid || duplicate) {
                addError(result.errors, path, line, 1, "FDN4015",
                         "native_link requires one unique library and optional target");
            } else {
                manifest.nativeLinks.push_back({tokens[1], linkTarget});
            }
        } else if (directive == "foreign") {
            const auto ecosystem =
                tokens.size() == 8 &&
                (tokens[1] == "c" || tokens[1] == "zig" || tokens[1] == "rust" ||
                 tokens[1] == "go");
            const auto duplicate =
                tokens.size() == 8 &&
                std::find_if(manifest.foreign.begin(), manifest.foreign.end(),
                             [&](const auto &entry) {
                                 return entry.ecosystem == tokens[1] &&
                                        entry.identifier == tokens[2];
                             }) != manifest.foreign.end();
            if (tokens.size() != 8 || !ecosystem || tokens[2].empty() || tokens[3].empty() ||
                tokens[5].empty() ||
                (tokens[4] != "path" && tokens[4] != "registry" &&
                 tokens[4] != "system") ||
                (tokens[4] == "path" && !relativeSource(tokens[5])) ||
                tokens[6] != "abi" || tokens[7] != "c/v1" || duplicate) {
                addError(result.errors, path, line, 1, "FDN4015",
                         "foreign requires ecosystem, identifier, version, resolver kind, "
                         "resolver, and abi c/v1");
            } else {
                manifest.foreign.push_back(
                    {tokens[1], tokens[2], tokens[3], tokens[4], tokens[5]});
            }
        } else if (directive == "dependency") {
            if (tokens.size() != 5 && tokens.size() != 7 && tokens.size() != 9) {
                addError(result.errors, path, line, 1, "FDN4009",
                         "dependency requires name, version, source kind, and location");
                return;
            }
            const auto requirement = parsePackageRequirement(tokens[2]);
            const auto kind = locationKind(tokens[3]);
            std::optional<TargetPlatform> dependencyTarget;
            auto dependencyScope = PackageDependencyScope::Runtime;
            auto validQualifiers = true;
            auto scopeSeen = false;
            for (std::size_t index = 5; index < tokens.size(); index += 2) {
                if (tokens[index] == "target" && !dependencyTarget.has_value()) {
                    dependencyTarget = target(tokens[index + 1]);
                    validQualifiers = validQualifiers && dependencyTarget.has_value();
                } else if (tokens[index] == "scope" && !scopeSeen &&
                           tokens[index + 1] == "test") {
                    dependencyScope = PackageDependencyScope::Test;
                    scopeSeen = true;
                } else {
                    validQualifiers = false;
                }
            }
            if (!identifier(tokens[1]) || !requirement.has_value() || !kind.has_value() ||
                (kind.has_value() && !validLocation(*kind, tokens[4])) ||
                !validQualifiers) {
                addError(result.errors, path, line, 1, "FDN4009",
                         "invalid package dependency");
                return;
            }
            if (manifest.dependencies.size() == maxPackageEntries) {
                addError(result.errors, path, line, 1, "FDN4010",
                         "package manifest exceeds 4096 dependencies");
                return;
            }
            const auto duplicate = std::find_if(
                manifest.dependencies.begin(), manifest.dependencies.end(),
                [&](const auto &candidate) {
                    return candidate.name == tokens[1] &&
                           candidate.target == dependencyTarget;
                });
            if (duplicate != manifest.dependencies.end()) {
                addError(result.errors, path, line, 1, "FDN4011",
                         "duplicate package dependency " + tokens[1]);
                return;
            }
            manifest.dependencies.push_back(
                {tokens[1], *requirement, *kind, tokens[4], dependencyTarget,
                 dependencyScope});
        } else {
            addError(result.errors, path, line, 1, "FDN4012",
                     "unknown package directive " + directive);
        }
    });
    if (!formatSeen) {
        addError(result.errors, path, 1, 1, "FDN4004", "missing package format directive");
    }
    if (!nameSeen) {
        addError(result.errors, path, 1, 1, "FDN4005", "missing package name");
    }
    if (!versionSeen) {
        addError(result.errors, path, 1, 1, "FDN4006", "missing package version");
    }
    if (!sdkSeen) {
        addError(result.errors, path, 1, 1, "FDN4007", "missing SDK requirement");
    }
    if (!sourceSeen) {
        addError(result.errors, path, 1, 1, "FDN4008", "missing source directory");
    }
    if (manifest.testSource.has_value() &&
        nestedSource(manifest.source, *manifest.testSource)) {
        addError(result.errors, path, 1, 1, "FDN4013",
                 "source and test_source directories cannot overlap");
    }
    if (!manifest.testSource.has_value() &&
        std::any_of(manifest.dependencies.begin(), manifest.dependencies.end(),
                    [](const auto &dependency) {
                        return dependency.scope == PackageDependencyScope::Test;
                    })) {
        addError(result.errors, path, 1, 1, "FDN4013",
                 "test dependencies require a test_source directory");
    }
    if ((manifest.nativeName.has_value() || manifest.nativeSOVersion.has_value() ||
         !manifest.nativeLinks.empty() || !manifest.foreign.empty()) &&
        !manifest.nativeLibrary) {
        addError(result.errors, path, 1, 1, "FDN4015",
                 "native directives require native_library c");
    }
    if (manifest.nativeLibrary && !manifest.nativeName.has_value()) {
        addError(result.errors, path, 1, 1, "FDN4015",
                 "native_library c requires native_name");
    }
    if (result.errors.empty()) {
        std::sort(manifest.nativeLinks.begin(), manifest.nativeLinks.end(),
                  [](const auto &left, const auto &right) {
                      return std::tie(left.library, left.target) <
                             std::tie(right.library, right.target);
                  });
        std::sort(manifest.dependencies.begin(), manifest.dependencies.end(),
                  [](const auto &left, const auto &right) {
                      return std::tie(left.name, left.target) <
                             std::tie(right.name, right.target);
                  });
        result.value = std::move(manifest);
    }
    return result;
}

PackageParseResult<PackageManifest>
readPackageManifest(const std::filesystem::path &path) {
    PackageParseResult<PackageManifest> result;
    const auto source = readPackageFile(path, result.errors);
    if (!source.has_value()) {
        return result;
    }
    return parsePackageManifest(path, *source);
}

std::string renderPackageManifest(const PackageManifest &manifest) {
    std::ostringstream output;
    output << "format foundation.package/v1\n"
           << "name " << manifest.name << '\n'
           << "version " << manifest.version.string() << '\n'
           << "sdk " << manifest.sdk.string() << '\n';
    if (manifest.codeStandardExplicit) {
        output << "fcs " << codeStandardProfileName(manifest.codeStandard) << '\n';
    }
    auto codeStandardRules = manifest.codeStandardRules;
    std::sort(codeStandardRules.begin(), codeStandardRules.end(),
              [](const auto &left, const auto &right) { return left.code < right.code; });
    for (const auto &rule : codeStandardRules) {
        output << "fcs_rule " << rule.code << ' '
               << codeStandardSeverityName(rule.severity) << '\n';
    }
    output << "source " << quote(manifest.source.generic_string()) << '\n';
    if (manifest.testSource.has_value()) {
        output << "test_source " << quote(manifest.testSource->generic_string()) << '\n';
    }
    if (manifest.nativeLibrary) {
        output << "native_library c\n";
    }
    if (manifest.nativeName.has_value()) {
        output << "native_name " << quote(*manifest.nativeName) << '\n';
    }
    if (manifest.nativeSOVersion.has_value()) {
        output << "native_soversion " << *manifest.nativeSOVersion << '\n';
    }
    auto nativeLinks = manifest.nativeLinks;
    std::sort(nativeLinks.begin(), nativeLinks.end(), [](const auto &left, const auto &right) {
        return std::tie(left.library, left.target) < std::tie(right.library, right.target);
    });
    for (const auto &link : nativeLinks) {
        output << "native_link " << quote(link.library);
        if (link.target.has_value()) {
            output << " target " << targetPlatformName(*link.target);
        }
        output << '\n';
    }
    auto foreign = manifest.foreign;
    std::sort(foreign.begin(), foreign.end(), [](const auto &left, const auto &right) {
        return std::tie(left.ecosystem, left.identifier, left.version, left.kind,
                        left.resolver) <
               std::tie(right.ecosystem, right.identifier, right.version, right.kind,
                        right.resolver);
    });
    for (const auto &entry : foreign) {
        output << "foreign " << quote(entry.ecosystem) << ' ' << quote(entry.identifier) << ' '
               << quote(entry.version) << ' ' << entry.kind << ' ' << quote(entry.resolver)
               << " abi c/v1\n";
    }
    auto dependencies = manifest.dependencies;
    std::sort(dependencies.begin(), dependencies.end(), [](const auto &left, const auto &right) {
        return std::tie(left.name, left.target) < std::tie(right.name, right.target);
    });
    for (const auto &dependency : dependencies) {
        output << "dependency " << dependency.name << ' ' << dependency.requirement.string()
               << ' ' << locationName(dependency.kind) << ' ' << quote(dependency.location);
        if (dependency.target.has_value()) {
            output << " target " << targetPlatformName(*dependency.target);
        }
        if (dependency.scope == PackageDependencyScope::Test) {
            output << " scope " << scopeName(dependency.scope);
        }
        output << '\n';
    }
    return output.str();
}

PackageParseResult<PackageLock>
parsePackageLock(const std::filesystem::path &path, std::string_view source) {
    PackageParseResult<PackageLock> result;
    PackageLock lock;
    auto formatSeen = false;
    auto rootSeen = false;
    auto targetSeen = false;
    auto nativeSeen = false;
    lines(path, source, result.errors, [&](const auto &tokens, std::size_t line) {
        const auto &directive = tokens.front();
        if (directive == "format") {
            if (tokens.size() != 2 || tokens[1] != "foundation.lock/v1" || formatSeen) {
                addError(result.errors, path, line, 1, "FDN4020",
                         "expected one format foundation.lock/v1 directive");
            }
            formatSeen = true;
        } else if (directive == "root") {
            const auto parsed = tokens.size() == 3 ? parsePackageVersion(tokens[2])
                                                   : std::nullopt;
            if (tokens.size() != 3 || !identifier(tokens[1]) || !parsed.has_value() ||
                rootSeen) {
                addError(result.errors, path, line, 1, "FDN4021",
                         "expected one root package and version");
            } else {
                lock.rootName = tokens[1];
                lock.rootVersion = *parsed;
            }
            rootSeen = true;
        } else if (directive == "target") {
            const auto parsed = tokens.size() == 2 ? target(tokens[1]) : std::nullopt;
            if (!parsed.has_value() || targetSeen) {
                addError(result.errors, path, line, 1, "FDN4022",
                         "expected one target linux, macos, or windows");
            } else {
                lock.target = *parsed;
            }
            targetSeen = true;
        } else if (directive == "native") {
            const auto soVersion = tokens.size() == 5 && tokens[3] != "-"
                                       ? number(tokens[3])
                                       : std::optional<std::uint64_t>{};
            const auto validSO = tokens.size() == 5 &&
                                 (tokens[3] == "-" ||
                                  (soVersion.has_value() &&
                                   *soVersion <= std::numeric_limits<std::uint32_t>::max()));
            if (tokens.size() != 5 || tokens[1] != "c" || !identifier(tokens[2]) ||
                !validSO || !digest(tokens[4]) || nativeSeen) {
                addError(result.errors, path, line, 1, "FDN4028",
                         "invalid native library lock entry");
            } else {
                std::optional<std::uint32_t> storedSO;
                if (soVersion.has_value()) {
                    storedSO = static_cast<std::uint32_t>(*soVersion);
                }
                lock.nativeLibrary = LockedNativeLibrary{tokens[2], storedSO, tokens[4]};
            }
            nativeSeen = true;
        } else if (directive == "foreign") {
            const auto validEcosystem =
                tokens.size() == 9 &&
                (tokens[1] == "c" || tokens[1] == "zig" || tokens[1] == "rust" ||
                 tokens[1] == "go");
            const auto validKind = tokens.size() == 9 &&
                                   (tokens[4] == "path" || tokens[4] == "registry" ||
                                    tokens[4] == "system");
            const auto duplicate =
                tokens.size() == 9 &&
                std::any_of(lock.foreign.begin(), lock.foreign.end(), [&](const auto &entry) {
                    return entry.ecosystem == tokens[1] && entry.identifier == tokens[2];
                });
            if (tokens.size() != 9 || !validEcosystem || tokens[2].empty() ||
                tokens[3].empty() || !validKind || tokens[5].empty() || tokens[6] != "abi" ||
                tokens[7] != "c/v1" || !digest(tokens[8]) || duplicate) {
                addError(result.errors, path, line, 1, "FDN4029",
                         "invalid foreign provenance lock entry");
            } else {
                lock.foreign.push_back(
                    {tokens[1], tokens[2], tokens[3], tokens[4], tokens[5], tokens[8]});
            }
        } else if (directive == "package") {
            const auto version = tokens.size() == 6 ? parsePackageVersion(tokens[2])
                                                    : std::nullopt;
            const auto kind = tokens.size() == 6 ? locationKind(tokens[4]) : std::nullopt;
            if (tokens.size() != 6 || !identifier(tokens[1]) || !version.has_value() ||
                !digest(tokens[3]) || !kind.has_value() ||
                !validLocation(kind.value_or(PackageLocationKind::Registry), tokens[5])) {
                addError(result.errors, path, line, 1, "FDN4023", "invalid locked package");
                return;
            }
            const auto duplicate = std::find_if(lock.packages.begin(), lock.packages.end(),
                                                [&](const auto &candidate) {
                                                    return candidate.name == tokens[1];
                                                });
            if (duplicate != lock.packages.end()) {
                addError(result.errors, path, line, 1, "FDN4024",
                         "duplicate locked package " + tokens[1]);
                return;
            }
            lock.packages.push_back({tokens[1], *version, tokens[3], *kind, tokens[5]});
        } else if (directive == "edge") {
            auto scope = PackageDependencyScope::Runtime;
            const auto validScope = tokens.size() == 3 ||
                                    (tokens.size() == 5 && tokens[3] == "scope" &&
                                     tokens[4] == "test");
            if (!validScope || !identifier(tokens[1]) || !identifier(tokens[2])) {
                addError(result.errors, path, line, 1, "FDN4025", "invalid package edge");
                return;
            }
            if (tokens.size() == 5) {
                scope = PackageDependencyScope::Test;
            }
            lock.edges.push_back({tokens[1], tokens[2], scope});
        } else {
            addError(result.errors, path, line, 1, "FDN4026",
                     "unknown lock directive " + directive);
        }
    });
    if (!formatSeen) {
        addError(result.errors, path, 1, 1, "FDN4020", "missing lock format directive");
    }
    if (!rootSeen) {
        addError(result.errors, path, 1, 1, "FDN4021", "missing root package");
    }
    if (!targetSeen) {
        addError(result.errors, path, 1, 1, "FDN4022", "missing lock target");
    }
    if (!lock.foreign.empty() && !lock.nativeLibrary.has_value()) {
        addError(result.errors, path, 1, 1, "FDN4029",
                 "foreign provenance requires a native library lock entry");
    }
    if (result.errors.empty() && validateLockGraph(path, lock, result.errors)) {
        std::sort(lock.packages.begin(), lock.packages.end(), [](const auto &left,
                                                                const auto &right) {
            return left.name < right.name;
        });
        std::sort(lock.edges.begin(), lock.edges.end(), [](const auto &left, const auto &right) {
            return std::tie(left.parent, left.dependency, left.scope) <
                   std::tie(right.parent, right.dependency, right.scope);
        });
        std::sort(lock.foreign.begin(), lock.foreign.end(), [](const auto &left,
                                                               const auto &right) {
            return std::tie(left.ecosystem, left.identifier, left.version, left.kind,
                            left.resolver, left.digest) <
                   std::tie(right.ecosystem, right.identifier, right.version, right.kind,
                            right.resolver, right.digest);
        });
        result.value = std::move(lock);
    }
    return result;
}

PackageParseResult<PackageLock> readPackageLock(const std::filesystem::path &path) {
    PackageParseResult<PackageLock> result;
    const auto source = readPackageFile(path, result.errors);
    if (!source.has_value()) {
        return result;
    }
    return parsePackageLock(path, *source);
}

std::string renderPackageLock(const PackageLock &lock) {
    std::ostringstream output;
    output << "format foundation.lock/v1\n"
           << "root " << lock.rootName << ' ' << lock.rootVersion.string() << '\n'
           << "target " << targetPlatformName(lock.target) << '\n';
    if (lock.nativeLibrary.has_value()) {
        output << "native c " << lock.nativeLibrary->name << ' ';
        if (lock.nativeLibrary->soVersion.has_value()) {
            output << *lock.nativeLibrary->soVersion;
        } else {
            output << '-';
        }
        output << ' ' << lock.nativeLibrary->digest << '\n';
    }
    auto foreign = lock.foreign;
    std::sort(foreign.begin(), foreign.end(), [](const auto &left, const auto &right) {
        return std::tie(left.ecosystem, left.identifier, left.version, left.kind,
                        left.resolver, left.digest) <
               std::tie(right.ecosystem, right.identifier, right.version, right.kind,
                        right.resolver, right.digest);
    });
    for (const auto &entry : foreign) {
        output << "foreign " << quote(entry.ecosystem) << ' ' << quote(entry.identifier) << ' '
               << quote(entry.version) << ' ' << entry.kind << ' ' << quote(entry.resolver)
               << " abi c/v1 " << entry.digest << '\n';
    }
    auto packages = lock.packages;
    std::sort(packages.begin(), packages.end(), [](const auto &left, const auto &right) {
        return left.name < right.name;
    });
    for (const auto &package : packages) {
        output << "package " << package.name << ' ' << package.version.string() << ' '
               << package.digest << ' ' << locationName(package.kind) << ' '
               << quote(package.location) << '\n';
    }
    auto edges = lock.edges;
    std::sort(edges.begin(), edges.end(), [](const auto &left, const auto &right) {
        return std::tie(left.parent, left.dependency, left.scope) <
               std::tie(right.parent, right.dependency, right.scope);
    });
    for (const auto &edge : edges) {
        output << "edge " << edge.parent << ' ' << edge.dependency;
        if (edge.scope == PackageDependencyScope::Test) {
            output << " scope test";
        }
        output << '\n';
    }
    return output.str();
}

std::string renderPackageError(const PackageError &error) {
    return error.path.generic_string() + ':' + std::to_string(error.line) + ':' +
           std::to_string(error.column) + ": error " + error.code + ": " + error.message +
           '\n';
}

} // namespace foundation
