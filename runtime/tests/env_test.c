#include "foundation/runtime.h"

#include <stdint.h>
#include <string.h>

static int equal(fdn_string value, const char *expected) {
    const size_t length = strlen(expected);
    return value.length == length &&
           (length == 0 || memcmp(value.data, expected, length) == 0);
}

int main(void) {
    const fdn_string present = fdn_string_static("FOUNDATION_ENV_TEST", 19);
    const fdn_string missing = fdn_string_static("FOUNDATION_ENV_TEST_MISSING_71C0", 32);
    const fdn_string invalid = fdn_string_static("INVALID=NAME", 12);
    fdn_string value = fdn_string_static("", 0);

    if (foundation_runtime_env_read(&present, &value) != 1 ||
        !equal(value, "foundation env value")) {
        return 1;
    }
    if (foundation_runtime_env_read(&missing, &value) != 0 || !equal(value, "")) {
        fdn_string_drop(&value);
        return 2;
    }
    if (foundation_runtime_env_read(&invalid, &value) != 2 || !equal(value, "")) {
        fdn_string_drop(&value);
        return 3;
    }
    fdn_string_drop(&value);
    return fdn_live_allocations() == 0 ? 0 : 4;
}
