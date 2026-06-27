#include "foundation/runtime.h"

#include <stdint.h>
#include <stdio.h>

static int version(uint64_t high) { return (int)((high >> 12U) & UINT64_C(15)); }

static int variant(uint64_t low) { return (int)(low >> 62U); }

int main(void) {
    uint64_t v4_high = 0;
    uint64_t v4_low = 0;
    uint64_t first_high = 0;
    uint64_t first_low = 0;
    uint64_t second_high = 0;
    uint64_t second_low = 0;

    foundation_runtime_uuid_v4(&v4_high, &v4_low);
    if (version(v4_high) != 4 || variant(v4_low) != 2) {
        fprintf(stderr, "uuid: invalid v4 layout\n");
        return 1;
    }

    foundation_runtime_uuid_v7(&first_high, &first_low);
    foundation_runtime_uuid_v7(&second_high, &second_low);
    if (version(first_high) != 7 || version(second_high) != 7 ||
        variant(first_low) != 2 || variant(second_low) != 2) {
        fprintf(stderr, "uuid: invalid v7 layout\n");
        return 2;
    }
    if (second_high < first_high ||
        (second_high == first_high && second_low <= first_low)) {
        fprintf(stderr, "uuid: v7 generation is not monotonic\n");
        return 3;
    }

    return 0;
}
