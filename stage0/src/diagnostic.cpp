#include "foundation/diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace foundation {

namespace {

constexpr std::size_t maxExcerptWidth = 120;
constexpr std::size_t markerContext = 40;
constexpr std::size_t maxMarkerWidth = 80;
constexpr std::size_t maxDiagnostics = 100;

} // namespace

void Diagnostics::error(std::string code, std::string message, SourceSpan span) {
    if (diagnostics_.size() > maxDiagnostics) {
        return;
    }
    if (diagnostics_.size() == maxDiagnostics) {
        diagnostics_.push_back({"FDN0000", "too many errors", span});
        return;
    }
    diagnostics_.push_back({std::move(code), std::move(message), span});
}

bool Diagnostics::hasErrors() const { return !diagnostics_.empty(); }

const std::vector<Diagnostic> &Diagnostics::all() const { return diagnostics_; }

std::string renderDiagnostics(std::string_view path, std::string_view source,
                              const Diagnostics &diagnostics) {
    std::ostringstream out;

    for (const auto &diagnostic : diagnostics.all()) {
        out << path << ':' << diagnostic.span.line << ':' << diagnostic.span.column << ": error["
            << diagnostic.code << "]: " << diagnostic.message << '\n';

        const auto boundedOffset = std::min(diagnostic.span.offset, source.size());
        const auto start = boundedOffset == 0 ? std::string_view::npos
                                              : source.rfind('\n', boundedOffset - 1);
        const auto lineStart = start == std::string_view::npos ? 0 : start + 1;
        const auto end = source.find('\n', boundedOffset);
        const auto lineEnd = end == std::string_view::npos ? source.size() : end;
        const auto line = source.substr(lineStart, lineEnd - lineStart);
        const auto offset = std::min(boundedOffset, lineEnd) - lineStart;
        const auto maxStart = line.size() > maxExcerptWidth ? line.size() - maxExcerptWidth : 0;
        const auto desiredStart = offset > markerContext ? offset - markerContext : 0;
        const auto excerptStart = std::min(desiredStart, maxStart);
        const auto excerptLength = std::min(maxExcerptWidth, line.size() - excerptStart);
        const auto excerpt = line.substr(excerptStart, excerptLength);
        const auto hasPrefix = excerptStart != 0;
        const auto hasSuffix = excerptStart + excerptLength != line.size();
        const auto markerColumn = offset - excerptStart + (hasPrefix ? 3 : 0);
        const auto markerWidth =
            std::max<std::size_t>(1, std::min(diagnostic.span.length, maxMarkerWidth));

        out << "  " << diagnostic.span.line << " | ";
        if (hasPrefix) {
            out << "...";
        }
        out << excerpt;
        if (hasSuffix) {
            out << "...";
        }
        out << '\n';
        out << "    | " << std::string(markerColumn, ' ') << '^';
        if (markerWidth > 1) {
            out << std::string(markerWidth - 1, '~');
        }
        out << '\n';
    }

    return out.str();
}

std::string renderDiagnostics(const std::vector<DiagnosticSource> &sources,
                              const Diagnostics &diagnostics) {
    std::ostringstream out;
    for (const auto &diagnostic : diagnostics.all()) {
        if (diagnostic.span.source < sources.size()) {
            Diagnostics current;
            current.error(diagnostic.code, diagnostic.message, diagnostic.span);
            const auto &source = sources[diagnostic.span.source];
            out << renderDiagnostics(source.path, source.contents, current);
            continue;
        }
        out << "<project>:" << diagnostic.span.line << ':' << diagnostic.span.column
            << ": error[" << diagnostic.code << "]: " << diagnostic.message << '\n';
    }
    return out.str();
}

} // namespace foundation
