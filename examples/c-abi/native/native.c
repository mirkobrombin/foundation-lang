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
    if (!foundation_accept_label(label)) {
        return -1;
    }
    foundation_mark();
    return foundation_double(value);
}
