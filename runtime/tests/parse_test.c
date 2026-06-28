#include "foundation/runtime.h"

#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static fdn_string text(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static bool parses_f32(const char *source, float expected) {
    const fdn_string input = text(source);
    float result = 0.0F;
    return foundation_runtime_parse_f32(&input, &result) == 0 && result == expected;
}

static bool parses_f64(const char *source, double expected) {
    const fdn_string input = text(source);
    double result = 0.0;
    return foundation_runtime_parse_f64(&input, &result) == 0 && result == expected;
}

static bool rejects_f64(const char *source, int32_t status) {
    const fdn_string input = text(source);
    double result = 0.0;
    return foundation_runtime_parse_f64(&input, &result) == status;
}

static bool rejects_f32(const char *source, int32_t status) {
    const fdn_string input = text(source);
    float result = 0.0F;
    return foundation_runtime_parse_f32(&input, &result) == status;
}

static float f32_bits(uint32_t bits) {
    float result = 0.0F;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static double f64_bits(uint64_t bits) {
    double result = 0.0;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool parses_duration(const char *source, int64_t expected) {
    const fdn_string input = text(source);
    int64_t result = 0;
    return foundation_runtime_time_parse_duration(&input, &result) == 0 && result == expected;
}

static bool rejects_duration(const char *source, int32_t status) {
    const fdn_string input = text(source);
    int64_t result = 0;
    return foundation_runtime_time_parse_duration(&input, &result) == status;
}

int main(void) {
    if (!parses_f64("3.14", 3.14) || !parses_f64("-0", -0.0) ||
        !parses_f64("1_2.5_0", 12.5) || !parses_f64("0x1.8p+1", 3.0) ||
        !parses_f64("1e-5000", 0.0) ||
        !parses_f64("1.7976931348623157e308", DBL_MAX)) {
        return 1;
    }
    fdn_string input = text("-0");
    double negative_zero = 1.0;
    if (foundation_runtime_parse_f64(&input, &negative_zero) != 0 ||
        !signbit(negative_zero)) {
        return 2;
    }
    input = text("NaN");
    double special = 0.0;
    if (foundation_runtime_parse_f64(&input, &special) != 0 || !isnan(special)) {
        return 3;
    }
    input = text("-Infinity");
    if (foundation_runtime_parse_f64(&input, &special) != 0 || !isinf(special) ||
        !signbit(special)) {
        return 4;
    }
    if (!parses_f32("0x1.8p+1", 3.0F) ||
        !parses_f32("3.4028234663852886e38", FLT_MAX)) {
        return 5;
    }
    if (!parses_f32("0x1p-149", f32_bits(UINT32_C(1))) ||
        !parses_f32("0x1p-150", 0.0F) ||
        !parses_f32("0x1.000002p-150", f32_bits(UINT32_C(1))) ||
        !parses_f32("0x1.000001p0", 1.0F) ||
        !parses_f32("0x1.000001000001p0", f32_bits(UINT32_C(0x3f800001))) ||
        !parses_f32("0x1.fffffep+127", FLT_MAX) ||
        !parses_f64("0x1p-1074", f64_bits(UINT64_C(1))) ||
        !parses_f64("0x1p-1075", 0.0) ||
        !parses_f64("0x1.0000000000001p-1075", f64_bits(UINT64_C(1))) ||
        !parses_f64("0x1.00000000000008p0", 1.0) ||
        !parses_f64("0x1.0000000000000800001p0",
                    f64_bits(UINT64_C(0x3ff0000000000001))) ||
        !parses_f64("0x1.fffffffffffffp+1023", DBL_MAX) ||
        !rejects_f32("0x1p+128", 3) || !rejects_f64("0x1p+1024", 3)) {
        return 13;
    }
    if (!rejects_f64("", 1) || !rejects_f64(" 1", 2) || !rejects_f64("1,5", 2) ||
        !rejects_f64("+NaN", 2) || !rejects_f64("nan(1)", 2) ||
        !rejects_f64("1__2", 2) || !rejects_f64("1_.0", 2) ||
        !rejects_f64("0x1.0", 2) || !rejects_f64("1e", 2) ||
        !rejects_f64("1.7976931348623159e308", 3)) {
        return 6;
    }
    input = text("3.4028236e38");
    float float32 = 0.0F;
    if (foundation_runtime_parse_f32(&input, &float32) != 3) {
        return 7;
    }
    if (setlocale(LC_NUMERIC, "en_DK.utf8") != NULL) {
        if (!parses_f64("1.5", 1.5) || !rejects_f64("1,5", 2)) {
            return 8;
        }
        if (setlocale(LC_NUMERIC, "C") == NULL) {
            return 9;
        }
    }
    if (!parses_duration("0", 0) || !parses_duration("+0", 0) ||
        !parses_duration("-0", 0) || !parses_duration("5.s", INT64_C(5000000000)) ||
        !parses_duration(".5s", INT64_C(500000000)) ||
        !parses_duration("1h2m3s4ms5us6ns", INT64_C(3723004005006)) ||
        !parses_duration("12\xc2\xb5" "s", INT64_C(12000)) ||
        !parses_duration("12\xce\xbc" "s", INT64_C(12000)) ||
        !parses_duration("0.3333333333333333333h", INT64_C(1200000000000)) ||
        !parses_duration("9223372036854775807ns", INT64_MAX) ||
        !parses_duration("-9223372036854775808ns", INT64_MIN) ||
        !parses_duration("-2562047h47m16.854775808s", INT64_MIN)) {
        return 10;
    }
    if (!rejects_duration("", 1) || !rejects_duration("+", 2) ||
        !rejects_duration("1", 2) || !rejects_duration(".s", 2) ||
        !rejects_duration("1_0s", 2) || !rejects_duration(" 1s", 2) ||
        !rejects_duration("1d", 2) ||
        !rejects_duration("9223372036854775808ns", 3) ||
        !rejects_duration("-9223372036854775809ns", 3)) {
        return 11;
    }
    return fdn_live_allocations() == 0 ? 0 : 12;
}
