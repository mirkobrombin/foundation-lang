#include "foundation_abi.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_same_v<decltype(foundation_double(std::int32_t{})), std::int32_t>);
static_assert(std::is_same_v<decltype(foundation_ready(true)), bool>);
static_assert(std::is_same_v<decltype(foundation_raw(nullptr)), const std::int32_t *>);
static_assert(std::is_same_v<decltype(foundation_accept_label(nullptr)), bool>);
static_assert(std::is_same_v<
              decltype(&foundation_scalars),
              bool (*)(std::int8_t, std::int16_t, std::int32_t, std::int64_t, std::intptr_t,
                       std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::size_t,
                       float, double, bool)>);

void checkFoundationHeader() {
    foundation_mark();
}
