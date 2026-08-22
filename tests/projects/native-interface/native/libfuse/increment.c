#include <math.h>
#include <stdint.h>

#include "foundation/runtime.h"

extern int32_t sample_private_identity(int32_t value);

int32_t sample_native_increment(int32_t value) {
    return value + 1;
}

double sample_native_sine(double value) {
    return sin(value);
}

int32_t sample_native_round_trip(int32_t value) {
    return sample_private_identity(value);
}

int32_t sample_native_apply(int32_t (*callback)(int32_t), int32_t value) {
    return callback(value);
}

void sample_native_double_start(int32_t value, int32_t *result,
                                fdn_reactor_operation *operation) {
    *result = value * 2;
    fdn_reactor_complete(operation, 7);
}

void sample_native_double_cancel(fdn_reactor_operation *operation) {
    (void)operation;
}
