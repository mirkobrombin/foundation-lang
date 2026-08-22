#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#endif

typedef struct foundation_native_read {
    uint64_t value;
    uint64_t delay;
    int32_t *result;
    fdn_reactor_operation *operation;
} foundation_native_read;

#if defined(_WIN32)
static LONG foundation_native_cancellations;

static void foundation_native_sleep(uint64_t milliseconds) {
    Sleep((DWORD)milliseconds);
}
#else
static _Atomic int32_t foundation_native_cancellations;

static void foundation_native_sleep(uint64_t milliseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / UINT64_C(1000));
    delay.tv_nsec = (long)(milliseconds % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&delay, &delay) != 0) {
    }
}
#endif

static void foundation_native_finish_read(foundation_native_read *read) {
    foundation_native_sleep(read->delay);
    *read->result = (int32_t)(read->value * UINT64_C(2));
    fdn_reactor_operation *operation = read->operation;
    fdn_dealloc(read);
    fdn_reactor_complete(operation, 2);
}

#if defined(_WIN32)
static DWORD WINAPI foundation_native_read_thread(void *raw) {
    foundation_native_finish_read(raw);
    return 0;
}
#else
static void *foundation_native_read_thread(void *raw) {
    foundation_native_finish_read(raw);
    return NULL;
}
#endif

void foundation_native_read_start(uint64_t value, uint64_t delay, int32_t *result,
                                  fdn_reactor_operation *operation) {
    foundation_native_read *read = fdn_alloc(sizeof(*read));
    read->value = value;
    read->delay = delay;
    read->result = result;
    read->operation = operation;
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, foundation_native_read_thread, read, 0, NULL);
    if (thread == NULL) {
        fdn_dealloc(read);
        fdn_reactor_complete(operation, -1);
        return;
    }
    CloseHandle(thread);
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, foundation_native_read_thread, read) != 0) {
        fdn_dealloc(read);
        fdn_reactor_complete(operation, -1);
        return;
    }
    if (pthread_detach(thread) != 0) {
        fdn_panic_cstr("cannot detach native callback thread");
    }
#endif
}

void foundation_native_read_cancel(fdn_reactor_operation *operation) {
    (void)operation;
#if defined(_WIN32)
    InterlockedIncrement(&foundation_native_cancellations);
#else
    atomic_fetch_add_explicit(&foundation_native_cancellations, 1,
                              memory_order_relaxed);
#endif
}

void foundation_native_read_without_cancel_start(
    uint64_t value, uint64_t delay, int32_t *result,
    fdn_reactor_operation *operation) {
    foundation_native_read_start(value, delay, result, operation);
}

int32_t foundation_native_cancellation_count(void) {
#if defined(_WIN32)
    return (int32_t)InterlockedCompareExchange(&foundation_native_cancellations, 0, 0);
#else
    return atomic_load_explicit(&foundation_native_cancellations,
                                memory_order_relaxed);
#endif
}
