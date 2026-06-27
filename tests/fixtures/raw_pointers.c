#include <stddef.h>
#include <stdint.h>

static int32_t foundation_raw_values[2];
static const int32_t foundation_raw_constant = 7;
static const int32_t foundation_raw_opaque = 9;

int32_t *foundation_raw_mutable_values(void) {
    return foundation_raw_values;
}

const int32_t *foundation_raw_constant_value(void) {
    return &foundation_raw_constant;
}

int32_t foundation_raw_sum_values(int32_t *values, size_t length) {
    int32_t result = 0;
    for (size_t index = 0; index < length; ++index) {
        result += values[index];
    }
    return result;
}

int32_t foundation_raw_read_values(const int32_t *values, size_t length) {
    int32_t result = 0;
    for (size_t index = 0; index < length; ++index) {
        result += values[index];
    }
    return result;
}

void foundation_raw_copy(uint8_t *destination, const uint8_t *source, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
}

const void *foundation_raw_opaque_handle(void) {
    return &foundation_raw_opaque;
}

int32_t foundation_raw_opaque_value(const void *handle) {
    return *(const int32_t *)handle;
}
