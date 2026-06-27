#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <assert.h>
#include <stdint.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef volatile LONG test_counter;
#else
#include <stdatomic.h>
typedef atomic_int test_counter;
#endif

typedef struct parallel_frame {
    test_counter *entered;
    test_counter *completed;
} parallel_frame;

typedef struct cancellation_frame {
    test_counter *seen;
} cancellation_frame;

static int counter_add(test_counter *counter, int value) {
#if defined(_WIN32)
    return (int)InterlockedAdd(counter, (LONG)value);
#else
    return atomic_fetch_add_explicit(counter, value, memory_order_acq_rel) + value;
#endif
}

static int counter_read(test_counter *counter) {
#if defined(_WIN32)
    return (int)InterlockedCompareExchange(counter, 0, 0);
#else
    return atomic_load_explicit(counter, memory_order_acquire);
#endif
}

static void sleep_millisecond(void) {
#if defined(_WIN32)
    Sleep(1);
#else
    struct timespec delay = {0, 1000000L};
    while (nanosleep(&delay, &delay) != 0) {
    }
#endif
}

static fdn_task_poll poll_parallel(void *context, bool cancellation_requested) {
    parallel_frame *frame = context;
    const uint64_t deadline = fdn_monotonic_nanoseconds() + UINT64_C(2000000000);
    (void)cancellation_requested;
    (void)counter_add(frame->entered, 1);
    while (counter_read(frame->entered) < 2 &&
           fdn_monotonic_nanoseconds() < deadline) {
        sleep_millisecond();
    }
    assert(counter_read(frame->entered) >= 2);
    (void)counter_add(frame->completed, 1);
    return FDN_TASK_READY;
}

static fdn_task_poll poll_cancellation(void *context, bool cancellation_requested) {
    cancellation_frame *frame = context;
    if (!cancellation_requested) {
        return FDN_TASK_PENDING;
    }
    (void)counter_add(frame->seen, 1);
    return FDN_TASK_READY;
}

static void move_void(void *context, void *result) {
    (void)context;
    (void)result;
}

static void drop_frame(void *context) { fdn_dealloc(context); }

static fdn_task *spawn_parallel(test_counter *entered, test_counter *completed) {
    parallel_frame *frame = fdn_alloc(sizeof(*frame));
    frame->entered = entered;
    frame->completed = completed;
    return fdn_task_spawn(frame, poll_parallel, move_void, drop_frame);
}

static fdn_task *spawn_cancellation(test_counter *seen) {
    cancellation_frame *frame = fdn_alloc(sizeof(*frame));
    frame->seen = seen;
    return fdn_task_spawn(frame, poll_cancellation, move_void, drop_frame);
}

int main(void) {
#if defined(_WIN32)
    test_counter entered = 0;
    test_counter completed = 0;
    test_counter cancellation_seen = 0;
#else
    test_counter entered = ATOMIC_VAR_INIT(0);
    test_counter completed = ATOMIC_VAR_INIT(0);
    test_counter cancellation_seen = ATOMIC_VAR_INIT(0);
#endif

    uint64_t pool = foundation_runtime_pool_open(2);
    foundation_runtime_pool_submit(pool, spawn_parallel(&entered, &completed));
    foundation_runtime_pool_submit(pool, spawn_parallel(&entered, &completed));
    assert(fdn_task_live_count() == 0);
    foundation_runtime_pool_wait(pool);
    foundation_runtime_pool_release(pool);
    assert(counter_read(&completed) == 2);
    assert(foundation_runtime_pool_live_count() == 0);

    pool = foundation_runtime_pool_open(1);
    foundation_runtime_pool_submit(pool, spawn_cancellation(&cancellation_seen));
    foundation_runtime_pool_submit(pool, spawn_cancellation(&cancellation_seen));
    foundation_runtime_pool_cancel(pool);
    foundation_runtime_pool_release(pool);
    assert(counter_read(&cancellation_seen) == 2);
    assert(foundation_runtime_pool_live_count() == 0);
    assert(fdn_task_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
