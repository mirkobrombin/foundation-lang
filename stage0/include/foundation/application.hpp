#ifndef FOUNDATION_APPLICATION_HPP
#define FOUNDATION_APPLICATION_HPP

#include "foundation/diagnostic.hpp"
#include "foundation/fir.hpp"

#include <string>
#include <string_view>

namespace foundation {

[[nodiscard]] std::string emitApplicationPlan(const FirProgram &program,
                                              Diagnostics &diagnostics);
[[nodiscard]] std::string emitPackageSource(const FirProgram &program,
                                            Diagnostics &diagnostics,
                                            std::string_view rootPackage,
                                            std::string_view generatedSourcePath = {});
[[nodiscard]] std::string emitApplicationHost(const FirProgram &program,
                                              Diagnostics &diagnostics,
                                              std::string_view generatedSourcePath = {});

} // namespace foundation

#endif
