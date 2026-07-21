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

typedef struct channel_send_frame {
    fdn_channel *sender;
    int32_t value;
    fdn_channel_status status;
    test_counter *completed;
    test_counter *parked;
    bool reported_pending;
} channel_send_frame;

typedef struct channel_receive_frame {
    fdn_channel *receiver;
    int32_t value;
    fdn_channel_status status;
} channel_receive_frame;

typedef struct channel_select_frame {
    fdn_channel *receiver;
    int32_t value;
    size_t selected;
    fdn_channel_status status;
    int context;
} channel_select_frame;

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

static fdn_task_poll poll_channel_send(void *context,
                                       bool cancellation_requested) {
    channel_send_frame *frame = context;
    (void)cancellation_requested;
    frame->status = fdn_channel_poll_send(frame->sender, &frame->value);
    if (frame->status == FDN_CHANNEL_PENDING) {
        if (!frame->reported_pending && frame->parked != NULL) {
            frame->reported_pending = true;
            (void)counter_add(frame->parked, 1);
        }
        return FDN_TASK_PENDING;
    }
    if (frame->status == FDN_CHANNEL_READY && frame->completed != NULL) {
        (void)counter_add(frame->completed, 1);
    }
    return FDN_TASK_READY;
}

static void drop_channel_send(void *context) {
    channel_send_frame *frame = context;
    fdn_channel_drop_sender(&frame->sender);
    fdn_dealloc(frame);
}

static fdn_task *spawn_channel_send(fdn_channel *sender, int32_t value,
                                    test_counter *completed,
                                    test_counter *parked) {
    channel_send_frame *frame = fdn_alloc(sizeof(*frame));
    frame->sender = sender;
    frame->value = value;
    frame->status = FDN_CHANNEL_PENDING;
    frame->completed = completed;
    frame->parked = parked;
    frame->reported_pending = false;
    return fdn_task_spawn(frame, poll_channel_send, move_void, drop_channel_send);
}

static fdn_task_poll poll_channel_receive(void *context,
                                          bool cancellation_requested) {
    channel_receive_frame *frame = context;
    (void)cancellation_requested;
    frame->status = fdn_channel_poll_receive(frame->receiver, &frame->value);
    return frame->status == FDN_CHANNEL_PENDING ? FDN_TASK_PENDING
                                                : FDN_TASK_READY;
}

static void move_channel_receive(void *context, void *result) {
    channel_receive_frame *frame = context;
    *(int32_t *)result = frame->value;
}

static void drop_channel_receive(void *context) {
    channel_receive_frame *frame = context;
    fdn_channel_drop_receiver(&frame->receiver);
    fdn_dealloc(frame);
}

static fdn_task *spawn_channel_receive(fdn_channel *receiver) {
    channel_receive_frame *frame = fdn_alloc(sizeof(*frame));
    frame->receiver = receiver;
    frame->value = 0;
    frame->status = FDN_CHANNEL_PENDING;
    return fdn_task_spawn(frame, poll_channel_receive, move_channel_receive,
                          drop_channel_receive);
}

static fdn_task_poll poll_channel_select(void *context,
                                         bool cancellation_requested) {
    channel_select_frame *frame = context;
    fdn_channel_select_case selection = {
        frame->receiver,
        &frame->value,
        FDN_CHANNEL_SELECT_RECEIVE,
    };
    (void)cancellation_requested;
    frame->status = fdn_channel_poll_select(
        &frame->context, &selection, 1, UINT64_MAX, &frame->selected);
    return frame->status == FDN_CHANNEL_PENDING ? FDN_TASK_PENDING
                                                : FDN_TASK_READY;
}

static void move_channel_select(void *context, void *result) {
    channel_select_frame *frame = context;
    *(int32_t *)result = frame->value;
}

static void drop_channel_select(void *context) {
    channel_select_frame *frame = context;
    fdn_channel_drop_receiver(&frame->receiver);
    fdn_dealloc(frame);
}

static fdn_task *spawn_channel_select(fdn_channel *receiver) {
    channel_select_frame *frame = fdn_alloc(sizeof(*frame));
    frame->receiver = receiver;
    frame->value = 0;
    frame->selected = SIZE_MAX;
    frame->status = FDN_CHANNEL_PENDING;
    frame->context = 0;
    return fdn_task_spawn(frame, poll_channel_select, move_channel_select,
                          drop_channel_select);
}

static void test_cross_executor_channels(void) {
#if defined(_WIN32)
    test_counter completed = 0;
#else
    test_counter completed = ATOMIC_VAR_INIT(0);
#endif
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *receiving;
    uint64_t pool;
    int32_t value = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    receiving = spawn_channel_receive(receiver);
    receiver = NULL;
    pool = foundation_runtime_pool_open(1);
    foundation_runtime_pool_submit(
        pool, spawn_channel_send(sender, 41, &completed, NULL));
    sender = NULL;
    fdn_task_wait(&receiving, &value);
    assert(value == 41);
    foundation_runtime_pool_wait(pool);
    foundation_runtime_pool_release(pool);
    assert(counter_read(&completed) == 1);

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    receiving = spawn_channel_select(receiver);
    receiver = NULL;
    pool = foundation_runtime_pool_open(1);
    foundation_runtime_pool_submit(
        pool, spawn_channel_send(sender, 42, &completed, NULL));
    sender = NULL;
    fdn_task_wait(&receiving, &value);
    assert(value == 42);
    foundation_runtime_pool_wait(pool);
    foundation_runtime_pool_release(pool);
    assert(counter_read(&completed) == 2);
}

static void test_cross_executor_channel_contention(void) {
    enum { jobs = 32 };
#if defined(_WIN32)
    test_counter completed = 0;
#else
    test_counter completed = ATOMIC_VAR_INIT(0);
#endif
    fdn_channel *sender;
    fdn_channel *receiver;
    uint64_t pool = foundation_runtime_pool_open(4);
    int64_t sum = 0;

    fdn_channel_open(sizeof(int32_t), 2, NULL, &sender, &receiver);
    for (int32_t value = 1; value <= jobs; ++value) {
        foundation_runtime_pool_submit(
            pool,
            spawn_channel_send(fdn_channel_clone_sender(sender), value,
                               &completed, NULL));
    }
    fdn_channel_drop_sender(&sender);
    for (int index = 0; index < jobs; ++index) {
        fdn_task *receiving =
            spawn_channel_receive(fdn_channel_clone_receiver(receiver));
        int32_t value = 0;
        fdn_task_wait(&receiving, &value);
        sum += value;
    }
    fdn_channel_drop_receiver(&receiver);
    foundation_runtime_pool_wait(pool);
    foundation_runtime_pool_release(pool);
    assert(sum == (int64_t)jobs * (jobs + 1) / 2);
    assert(counter_read(&completed) == jobs);
}

static void test_cross_executor_channel_cancellation(void) {
#if defined(_WIN32)
    test_counter parked = 0;
#else
    test_counter parked = ATOMIC_VAR_INIT(0);
#endif
    fdn_channel *sender;
    fdn_channel *receiver;
    uint64_t pool;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    pool = foundation_runtime_pool_open(1);
    foundation_runtime_pool_submit(pool,
                                   spawn_channel_send(sender, 7, NULL, &parked));
    sender = NULL;
    while (counter_read(&parked) == 0) {
        sleep_millisecond();
    }
    foundation_runtime_pool_cancel(pool);
    foundation_runtime_pool_release(pool);
    fdn_channel_drop_receiver(&receiver);
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
    test_cross_executor_channels();
    test_cross_executor_channel_contention();
    test_cross_executor_channel_cancellation();
    assert(fdn_task_live_count() == 0);
    assert(fdn_channel_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
