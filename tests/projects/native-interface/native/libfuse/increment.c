#include <stdint.h>

#include "foundation/runtime.h"

int32_t sample_native_increment(int32_t value) {
    return value + 1;
}

void sample_native_double_start(int32_t value, int32_t *result,
                                fdn_reactor_operation *operation) {
    *result = value * 2;
    fdn_reactor_complete(operation, 7);
}

void sample_native_double_cancel(fdn_reactor_operation *operation) {
    (void)operation;
}
