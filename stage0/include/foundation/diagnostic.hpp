#ifndef FOUNDATION_DIAGNOSTIC_HPP
#define FOUNDATION_DIAGNOSTIC_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

struct SourceSpan {
    std::size_t offset{};
    std::size_t length{};
    std::size_t line{1};
    std::size_t column{1};
    std::size_t source{};
};

struct DiagnosticSource {
    std::string path;
    std::string contents;
};

struct Diagnostic {
    std::string code;
    std::string message;
    SourceSpan span;
};

class Diagnostics {
  public:
    void error(std::string code, std::string message, SourceSpan span);
    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] const std::vector<Diagnostic> &all() const;

  private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::string renderDiagnostics(std::string_view path, std::string_view source,
                                            const Diagnostics &diagnostics);
[[nodiscard]] std::string renderDiagnostics(const std::vector<DiagnosticSource> &sources,
                                            const Diagnostics &diagnostics);

} // namespace foundation

#endif
