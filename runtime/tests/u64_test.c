#include "foundation/runtime.h"

#include <stdint.h>

int main(void) {
    if (fdn_u64_add(UINT64_MAX - UINT64_C(1), UINT64_C(1)) != UINT64_MAX) {
        return 1;
    }
    if (fdn_u64_subtract(UINT64_C(9), UINT64_C(4)) != UINT64_C(5)) {
        return 2;
    }
    if (fdn_u64_multiply(UINT64_C(6), UINT64_C(7)) != UINT64_C(42)) {
        return 3;
    }
    if (fdn_u64_divide(UINT64_C(42), UINT64_C(2)) != UINT64_C(21)) {
        return 4;
    }
    if (fdn_u64_remainder(UINT64_C(42), UINT64_C(5)) != UINT64_C(2)) {
        return 5;
    }
    return 0;
}
