#include "foundation/runtime.h"

size_t foundation_ring_total_allocations(void) {
    return fdn_total_allocations();
}
