#include "foundation/runtime.h"

#include <string.h>

int main(void) {
    static const char left_data[] = {'A', '\0'};
    static const char expected_data[] = {'A', '\0', 'B'};
    const size_t allocations_before = fdn_total_allocations();
    const size_t deallocations_before = fdn_total_deallocations();
    fdn_string left = fdn_string_static(left_data, sizeof(left_data));
    fdn_string right = fdn_string_static("B", 1);
    fdn_string joined = fdn_string_concat(left, right);
    fdn_string source = fdn_string_static("h\xc3\xa9llo session", 14);
    fdn_string prefix = fdn_string_static("h", 1);
    fdn_string part = fdn_string_static("session", 7);
    fdn_string copied;
    fdn_string sliced = fdn_string_static("", 0);
    uint64_t index = 99;
    uint64_t byte = 0;
    uint64_t builder;
    fdn_string built;
    fdn_string formatted_i32;
    fdn_string formatted_u64;

    if (joined.length != sizeof(expected_data) || joined.owned == 0 ||
        memcmp(joined.data, expected_data, sizeof(expected_data)) != 0) {
        return 1;
    }
    if (!fdn_string_equal(joined, fdn_string_static(expected_data, sizeof(expected_data)))) {
        return 2;
    }
    copied = foundation_runtime_string_copy(&source);
    if (!fdn_string_equal(copied, source) || copied.owned == 0 ||
        foundation_runtime_string_byte_length(&copied) != 14 ||
        !foundation_runtime_string_contains(&copied, &part) ||
        !foundation_runtime_string_starts_with(&copied, &prefix) ||
        !foundation_runtime_string_ends_with(&copied, &part) ||
        foundation_runtime_string_compare(&prefix, &part) >= 0 ||
        foundation_runtime_string_compare(&part, &prefix) <= 0 ||
        foundation_runtime_string_compare(&part, &part) != 0) {
        return 6;
    }
    if (!foundation_runtime_string_find(&copied, &part, &index) || index != 7 ||
        foundation_runtime_string_byte_at(&copied, 1, &byte) != 0 || byte != 0xc3 ||
        foundation_runtime_string_byte_at(&copied, 14, &byte) != 1) {
        return 7;
    }
    if (foundation_runtime_string_slice(&copied, 1, 3, &sliced) != 0 ||
        !fdn_string_equal(sliced, fdn_string_static("\xc3\xa9", 2))) {
        return 8;
    }
    fdn_string_drop(&sliced);
    if (foundation_runtime_string_slice(&copied, 2, 3, &sliced) != 2 ||
        sliced.length != 0 || sliced.owned != 0) {
        return 9;
    }
    fdn_string_drop(&copied);

    fdn_string moved = fdn_string_move(&joined);
    if (joined.data != NULL || joined.length != 0 || joined.owned != 0) {
        return 3;
    }
    fdn_string_drop(&moved);
    if (moved.data != NULL || moved.length != 0 || moved.owned != 0) {
        return 4;
    }
    builder = foundation_runtime_string_builder_open();
    foundation_runtime_string_builder_append(
        builder, &(fdn_string){"foundation ", 11, 0});
    if (!foundation_runtime_string_builder_append_code_point(builder, 0x1f642) ||
        foundation_runtime_string_builder_append_code_point(builder, 0xd800)) {
        return 5;
    }
    built = foundation_runtime_string_builder_finish(builder);
    if (!fdn_string_equal(built, fdn_string_static("foundation \xf0\x9f\x99\x82", 15)) ||
        foundation_runtime_string_builder_live_handles() != 0) {
        fdn_string_drop(&built);
        return 10;
    }
    fdn_string_drop(&built);
    builder = foundation_runtime_string_builder_open();
    foundation_runtime_string_builder_close(builder);
    if (foundation_runtime_string_builder_live_handles() != 0) {
        return 11;
    }
    formatted_i32 = foundation_runtime_format_i32(INT32_MIN);
    formatted_u64 = foundation_runtime_format_u64(UINT64_MAX);
    if (!fdn_string_equal(formatted_i32, fdn_string_static("-2147483648", 11)) ||
        !fdn_string_equal(formatted_u64, fdn_string_static("18446744073709551615", 20))) {
        fdn_string_drop(&formatted_i32);
        fdn_string_drop(&formatted_u64);
        return 12;
    }
    fdn_string_drop(&formatted_i32);
    fdn_string_drop(&formatted_u64);
    if (fdn_total_allocations() - allocations_before !=
            fdn_total_deallocations() - deallocations_before ||
        fdn_live_allocations() != 0) {
        return 13;
    }
    return 0;
}
