#include "foundation/runtime.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool string_equals(fdn_string *value, const char *expected) {
    const size_t length = strlen(expected);
    const bool equal = value->length == length && memcmp(value->data, expected, length) == 0;
    fdn_string_drop(value);
    return equal;
}

int main(void) {
    if (fdn_i8_add(INT8_C(120), INT8_C(7)) != INT8_MAX) {
        return 1;
    }
    if (fdn_i16_subtract(INT16_C(-12), INT16_C(3)) != INT16_C(-15)) {
        return 2;
    }
    if (fdn_i32_multiply(INT32_C(-6), INT32_C(7)) != INT32_C(-42)) {
        return 3;
    }
    if (fdn_i64_divide(INT64_MIN, INT64_C(1)) != INT64_MIN) {
        return 4;
    }
    if (fdn_i64_remainder(INT64_MIN, INT64_C(-1)) != INT64_C(0)) {
        return 5;
    }
    if (fdn_isize_negate((intptr_t)-7) != (intptr_t)7) {
        return 6;
    }
    if (fdn_u8_add(UINT8_C(250), UINT8_C(5)) != UINT8_MAX) {
        return 7;
    }
    if (fdn_u16_multiply(UINT16_C(255), UINT16_C(257)) != UINT16_MAX) {
        return 8;
    }
    if (fdn_u32_subtract(UINT32_MAX, UINT32_C(1)) != UINT32_MAX - UINT32_C(1)) {
        return 9;
    }
    if (fdn_u64_add(UINT64_MAX - UINT64_C(1), UINT64_C(1)) != UINT64_MAX) {
        return 10;
    }
    if (fdn_u64_subtract(UINT64_C(9), UINT64_C(4)) != UINT64_C(5)) {
        return 11;
    }
    if (fdn_u64_multiply(UINT64_C(6), UINT64_C(7)) != UINT64_C(42)) {
        return 12;
    }
    if (fdn_u64_divide(UINT64_C(42), UINT64_C(2)) != UINT64_C(21)) {
        return 13;
    }
    if (fdn_u64_remainder(UINT64_C(42), UINT64_C(5)) != UINT64_C(2)) {
        return 14;
    }
    if (fdn_usize_add(SIZE_MAX - (size_t)1, (size_t)1) != SIZE_MAX) {
        return 15;
    }
    if (fdn_usize_subtract((size_t)9, (size_t)4) != (size_t)5) {
        return 16;
    }
    if (fdn_usize_multiply((size_t)6, (size_t)7) != (size_t)42) {
        return 17;
    }
    if (fdn_usize_divide((size_t)42, (size_t)2) != (size_t)21) {
        return 18;
    }
    if (fdn_usize_remainder((size_t)42, (size_t)5) != (size_t)2) {
        return 19;
    }
    fdn_string formatted = foundation_runtime_format_bool(true);
    if (!string_equals(&formatted, "true")) {
        return 20;
    }
    formatted = foundation_runtime_format_i8(INT8_MIN);
    if (!string_equals(&formatted, "-128")) {
        return 21;
    }
    formatted = foundation_runtime_format_i16(INT16_MIN);
    if (!string_equals(&formatted, "-32768")) {
        return 22;
    }
    formatted = foundation_runtime_format_i32(INT32_MIN);
    if (!string_equals(&formatted, "-2147483648")) {
        return 23;
    }
    formatted = foundation_runtime_format_i64(INT64_MIN);
    if (!string_equals(&formatted, "-9223372036854775808")) {
        return 24;
    }
    formatted = foundation_runtime_format_u8(UINT8_MAX);
    if (!string_equals(&formatted, "255")) {
        return 25;
    }
    formatted = foundation_runtime_format_u16(UINT16_MAX);
    if (!string_equals(&formatted, "65535")) {
        return 26;
    }
    formatted = foundation_runtime_format_u32(UINT32_MAX);
    if (!string_equals(&formatted, "4294967295")) {
        return 27;
    }
    formatted = foundation_runtime_format_u64(UINT64_MAX);
    if (!string_equals(&formatted, "18446744073709551615")) {
        return 28;
    }
    formatted = foundation_runtime_format_f32(1.5F);
    if (!string_equals(&formatted, "1.5")) {
        return 29;
    }
    formatted = foundation_runtime_format_f64(-INFINITY);
    if (!string_equals(&formatted, "-Infinity")) {
        return 30;
    }
    return 0;
}
