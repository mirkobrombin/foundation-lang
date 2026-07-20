#include "foundation/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static void check(bool condition) {
    if (!condition) {
        abort();
    }
}

int main(void) {
    check(foundation_runtime_resiliency_finite(1.0));
    check(!foundation_runtime_resiliency_finite(INFINITY));
    check(!foundation_runtime_resiliency_finite(NAN));

    int64_t delay = -1;
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 2.0, 0.0, 0,
                                                     &delay) == 0);
    check(delay == 100);
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 2.0, 0.0, 1,
                                                     &delay) == 0);
    check(delay == 200);
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 2.0, 0.0, 8,
                                                     &delay) == 0);
    check(delay == 1000);
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 2.0, 0.5, 1,
                                                     &delay) == 0);
    check(delay >= 200 && delay <= 300);
    check(foundation_runtime_resiliency_retry_delay(-1, 1000, 2.0, 0.0, 0,
                                                     &delay) != 0);
    check(foundation_runtime_resiliency_retry_delay(100, 99, 2.0, 0.0, 0,
                                                     &delay) != 0);
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 0.5, 0.0, 0,
                                                     &delay) != 0);
    check(foundation_runtime_resiliency_retry_delay(100, 1000, 2.0, 1.5, 0,
                                                     &delay) != 0);

    check(foundation_runtime_bulkhead_live_handles() == 0);
    check(foundation_runtime_bulkhead_live_waiters() == 0);
    check(foundation_runtime_bulkhead_open(0, 1) == 0);
    const uint64_t handle = foundation_runtime_bulkhead_open(2, 3);
    check(handle != 0);
    check(foundation_runtime_bulkhead_live_handles() == 1);
    foundation_runtime_bulkhead_retain(handle);
    foundation_runtime_bulkhead_release(handle);
    check(foundation_runtime_bulkhead_live_handles() == 1);
    foundation_runtime_bulkhead_release(handle);
    check(foundation_runtime_bulkhead_live_handles() == 0);
    check(foundation_runtime_bulkhead_live_waiters() == 0);
    check(fdn_live_allocations() == 0);
    return 0;
}
