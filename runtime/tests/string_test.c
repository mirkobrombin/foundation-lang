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

    if (joined.length != sizeof(expected_data) || joined.owned == 0 ||
        memcmp(joined.data, expected_data, sizeof(expected_data)) != 0) {
        return 1;
    }
    if (!fdn_string_equal(joined, fdn_string_static(expected_data, sizeof(expected_data)))) {
        return 2;
    }

    fdn_string moved = fdn_string_move(&joined);
    if (joined.data != NULL || joined.length != 0 || joined.owned != 0) {
        return 3;
    }
    fdn_string_drop(&moved);
    if (moved.data != NULL || moved.length != 0 || moved.owned != 0) {
        return 4;
    }
    if (fdn_total_allocations() != allocations_before + 1 ||
        fdn_total_deallocations() != deallocations_before + 1 ||
        fdn_live_allocations() != 0) {
        return 5;
    }
    return 0;
}
