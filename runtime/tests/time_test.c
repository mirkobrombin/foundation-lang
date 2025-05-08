#include "foundation/runtime.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    const uint64_t now = foundation_runtime_time_unix_seconds();
    fdn_string epoch = fdn_string_static("", 0);
    fdn_string leap = fdn_string_static("", 0);
    if (now < UINT64_C(1700000000)) {
        return 1;
    }
    if (foundation_runtime_time_format_utc(UINT64_C(0), &epoch) != 0 ||
        epoch.length != 20 || memcmp(epoch.data, "1970-01-01T00:00:00Z", 20) != 0) {
        fdn_string_drop(&epoch);
        return 2;
    }
    if (foundation_runtime_time_format_utc(UINT64_C(1709164800), &leap) != 0 ||
        leap.length != 20 || memcmp(leap.data, "2024-02-29T00:00:00Z", 20) != 0) {
        fdn_string_drop(&epoch);
        fdn_string_drop(&leap);
        return 3;
    }
    fdn_string_drop(&epoch);
    fdn_string_drop(&leap);
    return fdn_live_allocations() == 0 ? 0 : 4;
}
