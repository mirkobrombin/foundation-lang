#include "foundation/runtime.h"

int main(void) {
    void *first;
    void *second;
    if (fdn_total_allocations() != 0 || fdn_total_deallocations() != 0 ||
        fdn_live_allocations() != 0) {
        return 1;
    }

    first = fdn_alloc(1);
    second = fdn_alloc(16);
    if (fdn_total_allocations() != 2 || fdn_total_deallocations() != 0 ||
        fdn_live_allocations() != 2) {
        return 2;
    }

    fdn_dealloc(first);
    if (fdn_total_allocations() != 2 || fdn_total_deallocations() != 1 ||
        fdn_live_allocations() != 1) {
        return 3;
    }

    fdn_dealloc(second);
    if (fdn_total_allocations() != 2 || fdn_total_deallocations() != 2 ||
        fdn_live_allocations() != 0) {
        return 4;
    }
    return 0;
}
