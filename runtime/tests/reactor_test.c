#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

typedef struct async_frame {
    fdn_reactor_operation *operation;
    int32_t input;
    int32_t result;
    uint64_t delay;
    int *polls;
    bool *cancellation_seen;
    bool worker_started;
#if defined(_WIN32)
    HANDLE worker;
#else
    pthread_t worker;
#endif
} async_frame;

typedef struct blocking_frame {
    fdn_blocking_job *job;
    int32_t input;
    int32_t result;
    uint64_t delay;
} blocking_frame;

typedef struct ready_frame {
    int *polls;
} ready_frame;

static void require(bool condition) {
    if (!condition) {
        abort();
    }
}

static void sleep_milliseconds(uint64_t milliseconds) {
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / UINT64_C(1000));
    delay.tv_nsec = (long)(milliseconds % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&delay, &delay) != 0) {
        require(errno == EINTR);
    }
#endif
}

#if defined(_WIN32)
static DWORD WINAPI async_worker(void *context)
#else
static void *async_worker(void *context)
#endif
{
    async_frame *frame = context;
    sleep_milliseconds(frame->delay);
    frame->result = frame->input * 2;
    fdn_reactor_complete(frame->operation, 7);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void start_async(void *context, fdn_reactor_operation *operation) {
    async_frame *frame = context;
    require(frame->operation == operation);
    frame->worker_started = true;
#if defined(_WIN32)
    frame->worker = CreateThread(NULL, 0, async_worker, frame, 0, NULL);
    require(frame->worker != NULL);
#else
    require(pthread_create(&frame->worker, NULL, async_worker, frame) == 0);
#endif
}

static void cancel_async(void *context) {
    async_frame *frame = context;
    if (frame->cancellation_seen != NULL) {
        *frame->cancellation_seen = true;
    }
}

static void join_async(async_frame *frame) {
    require(frame->worker_started);
#if defined(_WIN32)
    require(WaitForSingleObject(frame->worker, INFINITE) == WAIT_OBJECT_0);
    require(CloseHandle(frame->worker) != 0);
#else
    require(pthread_join(frame->worker, NULL) == 0);
#endif
    frame->worker_started = false;
}

static fdn_task_poll poll_async(void *context, bool cancellation_requested) {
    async_frame *frame = context;
    int32_t status = 0;
    (void)cancellation_requested;
    ++*frame->polls;
    if (!fdn_reactor_poll(&frame->operation, frame, start_async, cancel_async,
                          &status)) {
        return FDN_TASK_PENDING;
    }
    require(status == 7);
    join_async(frame);
    return FDN_TASK_READY;
}

static void move_async_result(void *context, void *result) {
    async_frame *frame = context;
    *(int32_t *)result = frame->result;
}

static void drop_async_frame(void *context) {
    async_frame *frame = context;
    require(frame->operation == NULL);
    require(!frame->worker_started);
    fdn_dealloc(frame);
}

static fdn_task *spawn_async(int32_t input, uint64_t delay, int *polls,
                             bool *cancellation_seen) {
    async_frame *frame = fdn_alloc(sizeof(*frame));
    frame->operation = NULL;
    frame->input = input;
    frame->result = 0;
    frame->delay = delay;
    frame->polls = polls;
    frame->cancellation_seen = cancellation_seen;
    frame->worker_started = false;
#if defined(_WIN32)
    frame->worker = NULL;
#endif
    return fdn_task_spawn(frame, poll_async, move_async_result, drop_async_frame);
}

static void run_blocking(void *context) {
    blocking_frame *frame = context;
    sleep_milliseconds(frame->delay);
    frame->result = frame->input * 3;
}

static fdn_task_poll poll_blocking(void *context, bool cancellation_requested) {
    blocking_frame *frame = context;
    (void)cancellation_requested;
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
    require(frame->job == NULL);
    fdn_dealloc(frame);
}

static fdn_task *spawn_blocking(int32_t input, uint64_t delay) {
    blocking_frame *frame = fdn_alloc(sizeof(*frame));
    frame->job = NULL;
    frame->input = input;
    frame->result = 0;
    frame->delay = delay;
    return fdn_task_spawn(frame, poll_blocking, move_blocking_result,
                          drop_blocking_frame);
}

static fdn_task_poll poll_ready(void *context, bool cancellation_requested) {
    ready_frame *frame = context;
    (void)cancellation_requested;
    ++*frame->polls;
    return FDN_TASK_READY;
}

static void move_ready_result(void *context, void *result) {
    (void)context;
    (void)result;
}

static void drop_ready_frame(void *context) { fdn_dealloc(context); }

static fdn_task *spawn_ready(int *polls) {
    ready_frame *frame = fdn_alloc(sizeof(*frame));
    frame->polls = polls;
    return fdn_task_spawn(frame, poll_ready, move_ready_result, drop_ready_frame);
}

int main(void) {
    const size_t baseline = fdn_live_allocations();
    int async_polls = 0;
    int ready_polls = 0;
    int32_t result = 0;
    fdn_task *operation = spawn_async(21, 20, &async_polls, NULL);
    fdn_task *ready = spawn_ready(&ready_polls);

    fdn_task_wait(&ready, NULL);
    require(ready_polls == 1);
    fdn_task_wait(&operation, &result);
    require(result == 42);
    require(async_polls == 2);

    bool cancellation_seen = false;
    async_polls = 0;
    operation = spawn_async(5, 10, &async_polls, &cancellation_seen);
    fdn_task_drop(&operation);
    require(cancellation_seen);
    require(async_polls == 2);

    int first_polls = 0;
    int second_polls = 0;
    fdn_task *first = spawn_async(2, 20, &first_polls, NULL);
    fdn_task *second = spawn_async(3, 5, &second_polls, NULL);
    int32_t first_result = 0;
    int32_t second_result = 0;
    fdn_task_wait(&second, &second_result);
    fdn_task_wait(&first, &first_result);
    require(first_result == 4);
    require(second_result == 6);
    require(first_polls == 2);
    require(second_polls == 2);

    async_polls = 0;
    operation = spawn_async(9, 10, &async_polls, NULL);
    fdn_task *blocking = spawn_blocking(7, 20);
    int32_t blocking_result = 0;
    fdn_task_wait(&operation, &result);
    fdn_task_wait(&blocking, &blocking_result);
    require(result == 18);
    require(blocking_result == 21);
    require(fdn_reactor_live_operations() == 0);
    require(fdn_blocking_live_jobs() == 0);
    require(fdn_task_live_count() == 0);
    require(fdn_live_allocations() == baseline);
    return 0;
}
