#include "foundation_abi.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int32_t foundation_native_drive(int32_t value, const fdn_string *label) {
    if (label == NULL || label->length != 3 || memcmp(label->data, "abi", 3) != 0) {
        return -1;
    }
    if (!foundation_ready(true)) {
        return -1;
    }
    const int32_t raw_value = 42;
    if (foundation_raw(&raw_value) != &raw_value) {
        return -1;
    }
    if (!foundation_accept_label(label)) {
        return -1;
    }
    if (!foundation_scalars(INT8_C(-8), INT16_C(-16), INT32_C(-32), INT64_C(-64),
                            (intptr_t)-5, UINT8_C(8), UINT16_C(16), UINT32_C(32),
                            UINT64_C(64), (size_t)10, 1.5F, -4.25, true)) {
        return -1;
    }
    foundation_mark();
    return foundation_double(value);
}
