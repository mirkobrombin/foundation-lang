#include "foundation/diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace foundation {

void Diagnostics::error(std::string code, std::string message, SourceSpan span) {
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

        const auto start = source.rfind('\n', diagnostic.span.offset);
        const auto lineStart = start == std::string_view::npos ? 0 : start + 1;
        const auto end = source.find('\n', diagnostic.span.offset);
        const auto lineEnd = end == std::string_view::npos ? source.size() : end;
        const auto line = source.substr(lineStart, lineEnd - lineStart);
        const auto width = std::max<std::size_t>(1, diagnostic.span.length);

        out << "  " << diagnostic.span.line << " | " << line << '\n';
        out << "    | " << std::string(diagnostic.span.column - 1, ' ') << '^';
        if (width > 1) {
            out << std::string(width - 1, '~');
        }
        out << '\n';
    }

    return out.str();
}

} // namespace foundation
