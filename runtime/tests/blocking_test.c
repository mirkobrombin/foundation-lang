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
#endif

typedef struct blocking_frame {
    fdn_blocking_job *job;
    int32_t input;
    int32_t result;
    uint32_t delay_milliseconds;
    int *polls;
    bool *cancellation_seen;
} blocking_frame;

typedef struct ready_frame {
    int *polls;
} ready_frame;

typedef struct timeout_frame {
    fdn_channel *receiver;
    uint64_t deadline;
    fdn_channel_status result;
} timeout_frame;

static void sleep_milliseconds(uint32_t milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0) {
    }
#endif
}

static void run_blocking(void *context) {
    blocking_frame *frame = context;
    void *allocation = fdn_alloc(1);
    sleep_milliseconds(frame->delay_milliseconds);
    frame->result = frame->input * 2;
    fdn_dealloc(allocation);
}

static fdn_task_poll poll_blocking(void *context, bool cancellation_requested) {
    blocking_frame *frame = context;
    ++*frame->polls;
    if (cancellation_requested && frame->cancellation_seen != NULL) {
        *frame->cancellation_seen = true;
    }
    if (!fdn_blocking_poll(&frame->job, frame, run_blocking)) {
        return FDN_TASK_PENDING;
    }
    return FDN_TASK_READY;
}

static void move_blocking_result(void *context, void *result) {
    blocking_frame *frame = context;
    *(int32_t *)result = frame->result;
}

static void drop_blocking_frame(void *context) {
    blocking_frame *frame = context;
    assert(frame->job == NULL);
    fdn_dealloc(frame);
}

static fdn_task *spawn_blocking(int32_t input, uint32_t delay_milliseconds,
                                int *polls, bool *cancellation_seen) {
    blocking_frame *frame = fdn_alloc(sizeof(*frame));
    frame->job = NULL;
    frame->input = input;
    frame->result = 0;
    frame->delay_milliseconds = delay_milliseconds;
    frame->polls = polls;
    frame->cancellation_seen = cancellation_seen;
    return fdn_task_spawn(frame, poll_blocking, move_blocking_result,
                          drop_blocking_frame);
}

static fdn_task_poll poll_ready(void *context, bool cancellation_requested) {
    ready_frame *frame = context;
    (void)cancellation_requested;
    ++*frame->polls;
    return FDN_TASK_READY;
}

static void move_void_result(void *context, void *result) {
    (void)context;
    (void)result;
}

static void drop_ready_frame(void *context) { fdn_dealloc(context); }

static fdn_task *spawn_ready(int *polls) {
    ready_frame *frame = fdn_alloc(sizeof(*frame));
    frame->polls = polls;
    return fdn_task_spawn(frame, poll_ready, move_void_result, drop_ready_frame);
}

static fdn_task_poll poll_timeout(void *context, bool cancellation_requested) {
    timeout_frame *frame = context;
    fdn_channel_select_case selection = {
        frame->receiver,
        NULL,
        FDN_CHANNEL_SELECT_RECEIVE,
    };
    size_t selected = SIZE_MAX;
    (void)cancellation_requested;
    const fdn_channel_status status = fdn_channel_poll_select(
        frame, &selection, 1, frame->deadline, &selected);
    if (status == FDN_CHANNEL_PENDING) {
        return FDN_TASK_PENDING;
    }
    frame->result = status;
    return FDN_TASK_READY;
}

static void move_timeout_result(void *context, void *result) {
    timeout_frame *frame = context;
    *(fdn_channel_status *)result = frame->result;
}

static void drop_timeout_frame(void *context) { fdn_dealloc(context); }

static fdn_task *spawn_timeout(fdn_channel *receiver, uint64_t delay_nanoseconds) {
    timeout_frame *frame = fdn_alloc(sizeof(*frame));
    const uint64_t now = fdn_monotonic_nanoseconds();
    frame->receiver = receiver;
    frame->deadline = now + delay_nanoseconds;
    frame->result = FDN_CHANNEL_PENDING;
    return fdn_task_spawn(frame, poll_timeout, move_timeout_result,
                          drop_timeout_frame);
}

int main(void) {
    int blocking_polls = 0;
    int ready_polls = 0;
    int32_t result = 0;
    fdn_task *blocking = spawn_blocking(21, 20, &blocking_polls, NULL);
    fdn_task *ready = spawn_ready(&ready_polls);

    fdn_task_wait(&blocking, &result);
    assert(result == 42);
    assert(blocking_polls == 2);
    assert(ready_polls == 1);
    fdn_task_wait(&ready, NULL);
    assert(fdn_blocking_live_jobs() == 0);

    bool cancellation_seen = false;
    blocking = spawn_blocking(5, 20, &blocking_polls, &cancellation_seen);
    ready = spawn_ready(&ready_polls);
    fdn_task_wait(&ready, NULL);
    fdn_task_drop(&blocking);
    assert(cancellation_seen);
    assert(fdn_blocking_live_jobs() == 0);

    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    blocking = spawn_blocking(9, 50, &blocking_polls, NULL);
    fdn_task *timeout = spawn_timeout(receiver, UINT64_C(5000000));
    fdn_channel_status timeout_status = FDN_CHANNEL_PENDING;
    fdn_task_wait(&timeout, &timeout_status);
    assert(timeout_status == FDN_CHANNEL_TIMEOUT);
    fdn_task_wait(&blocking, &result);
    assert(result == 18);
    fdn_channel_drop_sender(&sender);
    fdn_channel_drop_receiver(&receiver);

    assert(fdn_blocking_live_jobs() == 0);
    assert(fdn_task_live_count() == 0);
    assert(fdn_channel_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
