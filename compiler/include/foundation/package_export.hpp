#ifndef FOUNDATION_PACKAGE_EXPORT_HPP
#define FOUNDATION_PACKAGE_EXPORT_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"
#include "foundation/package_interface.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

enum class PackageExportFormat {
    Zig,
    Rust,
    GoCgo,
    GoDynamic,
    GoSource,
};

enum class PackageExportArtifact {
    None,
    Static,
    Shared,
};

struct PackageExportFile {
    std::filesystem::path path;
    std::string contents;
};

struct PackageExport {
    PackageExportArtifact artifact{PackageExportArtifact::None};
    std::vector<PackageExportFile> files;
};

[[nodiscard]] std::optional<PackageExportFormat> parsePackageExportFormat(std::string_view value);
[[nodiscard]] std::optional<PackageExport>
generatePackageExport(const FirProgram& program, const PackageInterface& packageInterface,
                      PackageExportFormat format, Diagnostics& diagnostics);

} // namespace foundation

#endif
