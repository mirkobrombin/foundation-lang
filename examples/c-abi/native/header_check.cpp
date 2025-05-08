#include "foundation_abi.h"

#include <cstdint>
#include <type_traits>

static_assert(std::is_same_v<decltype(foundation_double(std::int32_t{})), std::int32_t>);
static_assert(std::is_same_v<decltype(foundation_ready(true)), bool>);
static_assert(std::is_same_v<decltype(foundation_accept_label(nullptr)), bool>);

void checkFoundationHeader() {
    foundation_mark();
}
