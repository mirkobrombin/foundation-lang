#include "foundation/runtime.h"

#include <stdint.h>

int main(void) {
    fdn_frame frame;
    volatile size_t impossible_size = SIZE_MAX;
    fdn_frame_enter(&frame, "main", "allocate", "allocation.fdn", 8, 12);
    fdn_frame_location(&frame, 9, 5);
    (void)fdn_alloc(impossible_size);
    return 0;
}
