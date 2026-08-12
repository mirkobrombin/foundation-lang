#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

enum {
    FDN_PARSE_OK = 0,
    FDN_PARSE_EMPTY = 1,
    FDN_PARSE_INVALID = 2,
    FDN_PARSE_OUT_OF_RANGE = 3,
};

#if defined(__MINGW32__)
extern float __cdecl fdn_mingw_strtof_l(const char *value, char **end, _locale_t locale)
    __asm__("_strtof_l");
#endif

static unsigned char fdn_ascii_lower(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static bool fdn_ascii_equal(const fdn_string *value, const char *expected) {
    const size_t length = strlen(expected);
    if (value->length != length) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (fdn_ascii_lower((unsigned char)value->data[index]) !=
            (unsigned char)expected[index]) {
            return false;
        }
    }
    return true;
}

static bool fdn_float_digit(unsigned char value, bool hexadecimal) {
    return (value >= '0' && value <= '9') ||
           (hexadecimal && fdn_ascii_lower(value) >= 'a' && fdn_ascii_lower(value) <= 'f');
}

static bool fdn_float_digits(const fdn_string *value, size_t *offset, bool hexadecimal,
                             bool prefix_allows_separator, bool *saw_digit) {
    bool previous_digit = prefix_allows_separator;
    while (*offset < value->length) {
        const unsigned char current = (unsigned char)value->data[*offset];
        if (fdn_float_digit(current, hexadecimal)) {
            *saw_digit = true;
            previous_digit = true;
            ++*offset;
            continue;
        }
        if (current != '_') {
            break;
        }
        if (!previous_digit || *offset + 1 >= value->length ||
            !fdn_float_digit((unsigned char)value->data[*offset + 1], hexadecimal)) {
            return false;
        }
        previous_digit = false;
        ++*offset;
    }
    return true;
}

static bool fdn_float_syntax(const fdn_string *value) {
    size_t offset = 0;
    bool hexadecimal = false;
    bool saw_digit = false;
    if (offset < value->length &&
        (value->data[offset] == '+' || value->data[offset] == '-')) {
        ++offset;
    }
    if (offset + 2 <= value->length && value->data[offset] == '0' &&
        fdn_ascii_lower((unsigned char)value->data[offset + 1]) == 'x') {
        hexadecimal = true;
        offset += 2;
    }
    if (!fdn_float_digits(value, &offset, hexadecimal, hexadecimal, &saw_digit)) {
        return false;
    }
    if (offset < value->length && value->data[offset] == '.') {
        ++offset;
        if (!fdn_float_digits(value, &offset, hexadecimal, false, &saw_digit)) {
            return false;
        }
    }
    if (!saw_digit) {
        return false;
    }
    const unsigned char exponent = hexadecimal ? 'p' : 'e';
    if (offset < value->length &&
        fdn_ascii_lower((unsigned char)value->data[offset]) == exponent) {
        bool exponent_digit = false;
        ++offset;
        if (offset < value->length &&
            (value->data[offset] == '+' || value->data[offset] == '-')) {
            ++offset;
        }
        if (!fdn_float_digits(value, &offset, false, false, &exponent_digit) ||
            !exponent_digit) {
            return false;
        }
    } else if (hexadecimal) {
        return false;
    }
    return offset == value->length;
}

static char *fdn_float_copy(const fdn_string *value) {
    if (value->length == SIZE_MAX) {
        return NULL;
    }
    char *copy = fdn_alloc(value->length + 1);
    size_t output = 0;
    for (size_t input = 0; input < value->length; ++input) {
        if (value->data[input] != '_') {
            copy[output++] = value->data[input];
        }
    }
    copy[output] = '\0';
    return copy;
}

typedef struct {
    const char *source;
    size_t exponent_offset;
    size_t first_nonzero;
    size_t digit_count;
    size_t integer_digits;
    unsigned first_value;
    unsigned first_bits;
    int64_t exponent;
    bool negative;
} fdn_hex_number;

static unsigned fdn_hex_digit(unsigned char value) {
    if (value >= '0' && value <= '9') {
        return (unsigned)(value - '0');
    }
    return (unsigned)(fdn_ascii_lower(value) - 'a' + 10);
}

static int64_t fdn_saturating_add_i64(int64_t left, int64_t right) {
    if (right > 0 && left > INT64_MAX - right) {
        return INT64_MAX;
    }
    if (right < 0 && left < INT64_MIN - right) {
        return INT64_MIN;
    }
    return left + right;
}

static int64_t fdn_hex_exponent(const char *source, size_t offset) {
    bool negative = false;
    if (source[offset] == '+' || source[offset] == '-') {
        negative = source[offset] == '-';
        ++offset;
    }
    uint64_t magnitude = 0;
    while (source[offset] != '\0') {
        const uint64_t digit = (uint64_t)(source[offset] - '0');
        if (magnitude > ((uint64_t)INT64_MAX - digit) / UINT64_C(10)) {
            return negative ? INT64_MIN : INT64_MAX;
        }
        magnitude = magnitude * UINT64_C(10) + digit;
        ++offset;
    }
    return negative ? -(int64_t)magnitude : (int64_t)magnitude;
}

static fdn_hex_number fdn_hex_read(const char *source) {
    fdn_hex_number number = {0};
    number.source = source;
    number.first_nonzero = SIZE_MAX;
    size_t offset = 0;
    if (source[offset] == '+' || source[offset] == '-') {
        number.negative = source[offset] == '-';
        ++offset;
    }
    offset += 2;
    bool fractional = false;
    while (fdn_ascii_lower((unsigned char)source[offset]) != 'p') {
        if (source[offset] == '.') {
            fractional = true;
            ++offset;
            continue;
        }
        const unsigned digit = fdn_hex_digit((unsigned char)source[offset]);
        if (digit != 0 && number.first_nonzero == SIZE_MAX) {
            number.first_nonzero = number.digit_count;
            number.first_value = digit;
            number.first_bits = digit >= 8 ? 4U : digit >= 4 ? 3U : digit >= 2 ? 2U : 1U;
        }
        ++number.digit_count;
        if (!fractional) {
            ++number.integer_digits;
        }
        ++offset;
    }
    number.exponent_offset = offset;
    number.exponent = fdn_hex_exponent(source, offset + 1);
    return number;
}

static unsigned fdn_hex_digit_at(const fdn_hex_number *number, size_t ordinal) {
    size_t current = 0;
    size_t offset = number->negative || number->source[0] == '+' ? 3 : 2;
    while (offset < number->exponent_offset) {
        if (number->source[offset] != '.') {
            if (current == ordinal) {
                return fdn_hex_digit((unsigned char)number->source[offset]);
            }
            ++current;
        }
        ++offset;
    }
    return 0;
}

static bool fdn_hex_bit_at(const fdn_hex_number *number, size_t index) {
    if (index < number->first_bits) {
        return (number->first_value & (1U << (number->first_bits - index - 1))) != 0;
    }
    const size_t remaining = index - number->first_bits;
    const size_t ordinal = number->first_nonzero + 1 + remaining / 4;
    const unsigned bit = 3U - (unsigned)(remaining % 4);
    return (fdn_hex_digit_at(number, ordinal) & (1U << bit)) != 0;
}

static uint64_t fdn_hex_prefix(const fdn_hex_number *number, size_t count) {
    uint64_t result = 0;
    for (size_t index = 0; index < count; ++index) {
        result = (result << 1) | (fdn_hex_bit_at(number, index) ? UINT64_C(1) : UINT64_C(0));
    }
    return result;
}

static bool fdn_hex_any_from(const fdn_hex_number *number, size_t index, size_t bit_count) {
    if (index >= bit_count) {
        return false;
    }
    size_t ordinal = number->first_nonzero;
    unsigned bit = number->first_bits - 1U;
    if (index >= number->first_bits) {
        const size_t remaining = index - number->first_bits;
        ordinal += 1 + remaining / 4;
        bit = 3U - (unsigned)(remaining % 4);
    } else {
        bit -= (unsigned)index;
    }
    const unsigned first = fdn_hex_digit_at(number, ordinal);
    const unsigned mask = (1U << (bit + 1U)) - 1U;
    if ((first & mask) != 0) {
        return true;
    }
    size_t current = 0;
    size_t offset = number->negative || number->source[0] == '+' ? 3 : 2;
    while (offset < number->exponent_offset) {
        if (number->source[offset] != '.') {
            if (current > ordinal && fdn_hex_digit((unsigned char)number->source[offset]) != 0) {
                return true;
            }
            ++current;
        }
        ++offset;
    }
    return false;
}

static int64_t fdn_hex_binary_exponent(const fdn_hex_number *number) {
    int64_t position = 0;
    if (number->integer_digits > number->first_nonzero) {
        const size_t digits = number->integer_digits - number->first_nonzero - 1;
        position = digits > (size_t)(INT64_MAX / 4) ? INT64_MAX : (int64_t)digits * 4;
    } else {
        const size_t digits = number->first_nonzero + 1 - number->integer_digits;
        position = digits > (size_t)(INT64_MAX / 4) ? INT64_MIN : -(int64_t)digits * 4;
    }
    int64_t exponent = fdn_saturating_add_i64(number->exponent, position);
    exponent = fdn_saturating_add_i64(exponent, (int64_t)number->first_bits - 1);
    return exponent;
}

static size_t fdn_hex_bit_count(const fdn_hex_number *number) {
    const size_t remaining = number->digit_count - number->first_nonzero - 1;
    if (remaining > (SIZE_MAX - number->first_bits) / 4) {
        return SIZE_MAX;
    }
    return number->first_bits + remaining * 4;
}

static int32_t fdn_hex_bits(const char *source, unsigned precision, int64_t minimum_exponent,
                            int64_t maximum_exponent, uint64_t exponent_bias,
                            unsigned sign_bit, uint64_t *result) {
    const fdn_hex_number number = fdn_hex_read(source);
    const uint64_t sign = number.negative ? UINT64_C(1) << sign_bit : UINT64_C(0);
    if (number.first_nonzero == SIZE_MAX) {
        *result = sign;
        return FDN_PARSE_OK;
    }
    const size_t bit_count = fdn_hex_bit_count(&number);
    int64_t exponent = fdn_hex_binary_exponent(&number);
    const uint64_t leading = UINT64_C(1) << (precision - 1U);
    uint64_t significand = 0;
    if (exponent >= minimum_exponent) {
        if (exponent > maximum_exponent) {
            return FDN_PARSE_OUT_OF_RANGE;
        }
        if (bit_count <= precision) {
            significand = fdn_hex_prefix(&number, bit_count) << (precision - bit_count);
        } else {
            significand = fdn_hex_prefix(&number, precision);
            const bool round = fdn_hex_bit_at(&number, precision);
            const bool sticky = fdn_hex_any_from(&number, precision + 1U, bit_count);
            if (round && (sticky || (significand & UINT64_C(1)) != 0)) {
                ++significand;
            }
            if (significand == (leading << 1U)) {
                significand = leading;
                ++exponent;
                if (exponent > maximum_exponent) {
                    return FDN_PARSE_OUT_OF_RANGE;
                }
            }
        }
        const uint64_t exponent_field = (uint64_t)(exponent + (int64_t)exponent_bias);
        *result = sign | (exponent_field << (precision - 1U)) | (significand - leading);
        return FDN_PARSE_OK;
    }
    const int64_t keep = fdn_saturating_add_i64(
        fdn_saturating_add_i64(exponent, -minimum_exponent), (int64_t)precision);
    bool round = false;
    bool sticky = false;
    if (keep > 0) {
        const size_t kept = (size_t)keep;
        if (kept >= bit_count) {
            significand = fdn_hex_prefix(&number, bit_count) << (kept - bit_count);
        } else {
            significand = fdn_hex_prefix(&number, kept);
            round = fdn_hex_bit_at(&number, kept);
            sticky = fdn_hex_any_from(&number, kept + 1U, bit_count);
        }
    } else if (keep == 0) {
        round = true;
        sticky = fdn_hex_any_from(&number, 1, bit_count);
    }
    if (round && (sticky || (significand & UINT64_C(1)) != 0)) {
        ++significand;
    }
    if (significand == leading) {
        *result = sign | (UINT64_C(1) << (precision - 1U));
    } else {
        *result = sign | significand;
    }
    return FDN_PARSE_OK;
}

static int32_t fdn_hex_f32(const char *source, float *result) {
    uint64_t parsed = 0;
    const int32_t status = fdn_hex_bits(source, 24, -126, 127, 127, 31, &parsed);
    const uint32_t bits = (uint32_t)parsed;
    memcpy(result, &bits, sizeof(bits));
    return status;
}

static int32_t fdn_hex_f64(const char *source, double *result) {
    uint64_t bits = 0;
    const int32_t status = fdn_hex_bits(source, 53, -1022, 1023, 1023, 63, &bits);
    memcpy(result, &bits, sizeof(bits));
    return status;
}

static bool fdn_float_is_hexadecimal(const char *value) {
    size_t offset = value[0] == '+' || value[0] == '-' ? 1 : 0;
    return value[offset] == '0' && fdn_ascii_lower((unsigned char)value[offset + 1]) == 'x';
}

static float fdn_strtof_c(const char *value, char **end) {
#if defined(_WIN32)
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (locale == NULL) {
        fdn_panic_cstr("C numeric locale is unavailable");
    }
#if defined(__MINGW32__)
    const float result = fdn_mingw_strtof_l(value, end, locale);
#else
    const float result = _strtof_l(value, end, locale);
#endif
    _free_locale(locale);
    return result;
#else
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (locale == (locale_t)0) {
        fdn_panic_cstr("C numeric locale is unavailable");
    }
    const float result = strtof_l(value, end, locale);
    freelocale(locale);
    return result;
#endif
}

static double fdn_strtod_c(const char *value, char **end) {
#if defined(_WIN32)
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (locale == NULL) {
        fdn_panic_cstr("C numeric locale is unavailable");
    }
    const double result = _strtod_l(value, end, locale);
    _free_locale(locale);
    return result;
#else
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (locale == (locale_t)0) {
        fdn_panic_cstr("C numeric locale is unavailable");
    }
    const double result = strtod_l(value, end, locale);
    freelocale(locale);
    return result;
#endif
}

static int32_t fdn_float_special_f32(const fdn_string *value, float *result) {
    if (fdn_ascii_equal(value, "nan")) {
        *result = NAN;
        return FDN_PARSE_OK;
    }
    if (fdn_ascii_equal(value, "inf") || fdn_ascii_equal(value, "+inf") ||
        fdn_ascii_equal(value, "infinity") || fdn_ascii_equal(value, "+infinity")) {
        *result = INFINITY;
        return FDN_PARSE_OK;
    }
    if (fdn_ascii_equal(value, "-inf") || fdn_ascii_equal(value, "-infinity")) {
        *result = -INFINITY;
        return FDN_PARSE_OK;
    }
    return FDN_PARSE_INVALID;
}

static int32_t fdn_float_special_f64(const fdn_string *value, double *result) {
    if (fdn_ascii_equal(value, "nan")) {
        *result = NAN;
        return FDN_PARSE_OK;
    }
    if (fdn_ascii_equal(value, "inf") || fdn_ascii_equal(value, "+inf") ||
        fdn_ascii_equal(value, "infinity") || fdn_ascii_equal(value, "+infinity")) {
        *result = INFINITY;
        return FDN_PARSE_OK;
    }
    if (fdn_ascii_equal(value, "-inf") || fdn_ascii_equal(value, "-infinity")) {
        *result = -INFINITY;
        return FDN_PARSE_OK;
    }
    return FDN_PARSE_INVALID;
}

int32_t foundation_runtime_parse_f32(const fdn_string *value, float *result) {
    if (value == NULL || result == NULL ||
        (value->data == NULL && value->length != 0)) {
        return FDN_PARSE_INVALID;
    }
    if (value->length == 0) {
        return FDN_PARSE_EMPTY;
    }
    const int32_t special = fdn_float_special_f32(value, result);
    if (special == FDN_PARSE_OK) {
        return FDN_PARSE_OK;
    }
    if (!fdn_float_syntax(value)) {
        return FDN_PARSE_INVALID;
    }
    char *copy = fdn_float_copy(value);
    if (copy == NULL) {
        return FDN_PARSE_OUT_OF_RANGE;
    }
    char *end = NULL;
    if (fdn_float_is_hexadecimal(copy)) {
        const int32_t status = fdn_hex_f32(copy, result);
        fdn_dealloc(copy);
        return status;
    }
    const float parsed = fdn_strtof_c(copy, &end);
    const bool complete = end != NULL && *end == '\0';
    fdn_dealloc(copy);
    if (!complete) {
        return FDN_PARSE_INVALID;
    }
    if (isinf(parsed)) {
        return FDN_PARSE_OUT_OF_RANGE;
    }
    *result = parsed;
    return FDN_PARSE_OK;
}

int32_t foundation_runtime_parse_f64(const fdn_string *value, double *result) {
    if (value == NULL || result == NULL ||
        (value->data == NULL && value->length != 0)) {
        return FDN_PARSE_INVALID;
    }
    if (value->length == 0) {
        return FDN_PARSE_EMPTY;
    }
    const int32_t special = fdn_float_special_f64(value, result);
    if (special == FDN_PARSE_OK) {
        return FDN_PARSE_OK;
    }
    if (!fdn_float_syntax(value)) {
        return FDN_PARSE_INVALID;
    }
    char *copy = fdn_float_copy(value);
    if (copy == NULL) {
        return FDN_PARSE_OUT_OF_RANGE;
    }
    char *end = NULL;
    if (fdn_float_is_hexadecimal(copy)) {
        const int32_t status = fdn_hex_f64(copy, result);
        fdn_dealloc(copy);
        return status;
    }
    const double parsed = fdn_strtod_c(copy, &end);
    const bool complete = end != NULL && *end == '\0';
    fdn_dealloc(copy);
    if (!complete) {
        return FDN_PARSE_INVALID;
    }
    if (isinf(parsed)) {
        return FDN_PARSE_OUT_OF_RANGE;
    }
    *result = parsed;
    return FDN_PARSE_OK;
}

static bool fdn_duration_unit(const char *value, size_t length, uint64_t *unit) {
    if (length == 2 && value[0] == 'n' && value[1] == 's') {
        *unit = UINT64_C(1);
    } else if (length == 2 && value[0] == 'u' && value[1] == 's') {
        *unit = UINT64_C(1000);
    } else if (length == 3 && (unsigned char)value[0] == UINT8_C(0xc2) &&
               (unsigned char)value[1] == UINT8_C(0xb5) && value[2] == 's') {
        *unit = UINT64_C(1000);
    } else if (length == 3 && (unsigned char)value[0] == UINT8_C(0xce) &&
               (unsigned char)value[1] == UINT8_C(0xbc) && value[2] == 's') {
        *unit = UINT64_C(1000);
    } else if (length == 2 && value[0] == 'm' && value[1] == 's') {
        *unit = UINT64_C(1000000);
    } else if (length == 1 && value[0] == 's') {
        *unit = UINT64_C(1000000000);
    } else if (length == 1 && value[0] == 'm') {
        *unit = UINT64_C(60000000000);
    } else if (length == 1 && value[0] == 'h') {
        *unit = UINT64_C(3600000000000);
    } else {
        return false;
    }
    return true;
}

static bool fdn_duration_integer(const fdn_string *value, size_t *offset, uint64_t *result,
                                 bool *consumed) {
    const uint64_t limit = UINT64_C(1) << 63;
    uint64_t parsed = 0;
    *consumed = false;
    while (*offset < value->length) {
        const unsigned char current = (unsigned char)value->data[*offset];
        if (current < '0' || current > '9') {
            break;
        }
        const uint64_t digit = (uint64_t)(current - '0');
        if (parsed > (limit - digit) / UINT64_C(10)) {
            return false;
        }
        parsed = parsed * UINT64_C(10) + digit;
        *consumed = true;
        ++*offset;
    }
    *result = parsed;
    return true;
}

static void fdn_duration_fraction(const fdn_string *value, size_t *offset, uint64_t *result,
                                  double *scale, bool *consumed) {
    const uint64_t limit = UINT64_C(1) << 63;
    bool overflow = false;
    *result = 0;
    *scale = 1.0;
    *consumed = false;
    while (*offset < value->length) {
        const unsigned char current = (unsigned char)value->data[*offset];
        if (current < '0' || current > '9') {
            break;
        }
        *consumed = true;
        ++*offset;
        if (overflow) {
            continue;
        }
        const uint64_t digit = (uint64_t)(current - '0');
        if (*result > (limit - digit) / UINT64_C(10)) {
            overflow = true;
            continue;
        }
        *result = *result * UINT64_C(10) + digit;
        *scale *= 10.0;
    }
}

int32_t foundation_runtime_time_parse_duration(const fdn_string *value, int64_t *result) {
    if (value == NULL || result == NULL ||
        (value->data == NULL && value->length != 0)) {
        return FDN_PARSE_INVALID;
    }
    if (value->length == 0) {
        return FDN_PARSE_EMPTY;
    }
    size_t offset = 0;
    bool negative = false;
    if (value->data[offset] == '+' || value->data[offset] == '-') {
        negative = value->data[offset] == '-';
        ++offset;
    }
    if (offset + 1 == value->length && value->data[offset] == '0') {
        *result = 0;
        return FDN_PARSE_OK;
    }
    if (offset == value->length) {
        return FDN_PARSE_INVALID;
    }
    uint64_t total = 0;
    while (offset < value->length) {
        if (value->data[offset] != '.' &&
            (value->data[offset] < '0' || value->data[offset] > '9')) {
            return FDN_PARSE_INVALID;
        }
        uint64_t integer = 0;
        bool before_point = false;
        if (!fdn_duration_integer(value, &offset, &integer, &before_point)) {
            return FDN_PARSE_OUT_OF_RANGE;
        }
        uint64_t fraction = 0;
        double scale = 1.0;
        bool after_point = false;
        if (offset < value->length && value->data[offset] == '.') {
            ++offset;
            fdn_duration_fraction(value, &offset, &fraction, &scale, &after_point);
        }
        if (!before_point && !after_point) {
            return FDN_PARSE_INVALID;
        }
        const size_t unit_start = offset;
        while (offset < value->length && value->data[offset] != '.' &&
               (value->data[offset] < '0' || value->data[offset] > '9')) {
            ++offset;
        }
        if (unit_start == offset) {
            return FDN_PARSE_INVALID;
        }
        uint64_t unit = 0;
        if (!fdn_duration_unit(value->data + unit_start, offset - unit_start, &unit)) {
            return FDN_PARSE_INVALID;
        }
        const uint64_t limit = UINT64_C(1) << 63;
        if (integer > limit / unit) {
            return FDN_PARSE_OUT_OF_RANGE;
        }
        uint64_t part = integer * unit;
        if (fraction != 0) {
            part += (uint64_t)((double)fraction * ((double)unit / scale));
            if (part > limit) {
                return FDN_PARSE_OUT_OF_RANGE;
            }
        }
        if (total > limit - part) {
            return FDN_PARSE_OUT_OF_RANGE;
        }
        total += part;
    }
    if (!negative && total > (uint64_t)INT64_MAX) {
        return FDN_PARSE_OUT_OF_RANGE;
    }
    if (negative && total == (UINT64_C(1) << 63)) {
        *result = INT64_MIN;
    } else {
        *result = negative ? -(int64_t)total : (int64_t)total;
    }
    return FDN_PARSE_OK;
}
