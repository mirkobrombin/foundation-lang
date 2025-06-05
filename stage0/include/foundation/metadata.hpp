#ifndef FOUNDATION_METADATA_HPP
#define FOUNDATION_METADATA_HPP

#include "foundation/fir.hpp"

#include <string>

namespace foundation {

[[nodiscard]] std::string emitMetadata(const FirProgram &program);

} // namespace foundation

#endif
