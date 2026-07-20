#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
               "resiliency handles require a 64-bit carrier");

typedef struct fdn_bulkhead_waiter {
    struct fdn_bulkhead_waiter *next;
    fdn_reactor_operation *operation;
} fdn_bulkhead_waiter;

typedef struct fdn_bulkhead {
    struct fdn_bulkhead *next;
    fdn_bulkhead_waiter *head;
    fdn_bulkhead_waiter *tail;
    uint64_t references;
    uint64_t running;
    uint64_t queued;
    uint64_t max_concurrent;
    uint64_t max_queue;
} fdn_bulkhead;

#if defined(_WIN32)
static SRWLOCK fdn_bulkhead_lock = SRWLOCK_INIT;
#else
static pthread_mutex_t fdn_bulkhead_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static fdn_bulkhead *fdn_bulkheads;
static uint64_t fdn_bulkhead_count;
static uint64_t fdn_bulkhead_waiter_count;

static void fdn_bulkhead_enter(void) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&fdn_bulkhead_lock);
#else
    if (pthread_mutex_lock(&fdn_bulkhead_lock) != 0) {
        fdn_panic_cstr("bulkhead lock failed");
    }
#endif
}

static void fdn_bulkhead_leave(void) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&fdn_bulkhead_lock);
#else
    if (pthread_mutex_unlock(&fdn_bulkhead_lock) != 0) {
        fdn_panic_cstr("bulkhead unlock failed");
    }
#endif
}

static fdn_bulkhead *fdn_bulkhead_from_handle(uint64_t handle) {
    return (fdn_bulkhead *)(uintptr_t)handle;
}

static bool fdn_bulkhead_registered(fdn_bulkhead *candidate) {
    for (fdn_bulkhead *current = fdn_bulkheads; current != NULL;
         current = current->next) {
        if (current == candidate) {
            return true;
        }
    }
    return false;
}

static void fdn_bulkhead_remove(fdn_bulkhead *bulkhead) {
    fdn_bulkhead **current = &fdn_bulkheads;
    while (*current != NULL && *current != bulkhead) {
        current = &(*current)->next;
    }
    if (*current != bulkhead) {
        fdn_panic_cstr("bulkhead registry corruption");
    }
    *current = bulkhead->next;
    if (fdn_bulkhead_count == 0) {
        fdn_panic_cstr("bulkhead count underflow");
    }
    --fdn_bulkhead_count;
}

bool foundation_runtime_resiliency_finite(double value) {
    return isfinite(value) != 0;
}

int32_t foundation_runtime_resiliency_retry_delay(
    int64_t initial_nanoseconds, int64_t max_nanoseconds, double factor,
    double jitter, uint64_t attempt, int64_t *result) {
    if (result == NULL || initial_nanoseconds < 0 ||
        max_nanoseconds < initial_nanoseconds || !isfinite(factor) || factor < 1.0 ||
        !isfinite(jitter) || jitter < 0.0 || jitter > 1.0) {
        return 1;
    }

    double delay = (double)initial_nanoseconds;
    const double maximum = (double)max_nanoseconds;
    for (uint64_t index = 0; index < attempt && delay < maximum; ++index) {
        if (factor > 0.0 && delay > maximum / factor) {
            delay = maximum;
            break;
        }
        delay *= factor;
    }
    if (delay > maximum) {
        delay = maximum;
    }

    if (jitter > 0.0 && delay > 0.0) {
        uint64_t bits = fdn_monotonic_nanoseconds() ^
                        ((attempt + UINT64_C(1)) * UINT64_C(0x9e3779b97f4a7c15));
        bits ^= bits >> 12;
        bits ^= bits << 25;
        bits ^= bits >> 27;
        bits *= UINT64_C(0x2545f4914f6cdd1d);
        const double fraction = (double)(bits >> 11) * (1.0 / 9007199254740992.0);
        delay += delay * jitter * fraction;
    }

    if (delay >= (double)INT64_MAX) {
        *result = INT64_MAX;
    } else {
        *result = (int64_t)delay;
    }
    return 0;
}

uint64_t foundation_runtime_bulkhead_open(uint64_t max_concurrent,
                                          uint64_t max_queue) {
    if (max_concurrent == 0) {
        return 0;
    }
    fdn_bulkhead *bulkhead = fdn_alloc(sizeof(*bulkhead));
    bulkhead->head = NULL;
    bulkhead->tail = NULL;
    bulkhead->references = 1;
    bulkhead->running = 0;
    bulkhead->queued = 0;
    bulkhead->max_concurrent = max_concurrent;
    bulkhead->max_queue = max_queue;

    fdn_bulkhead_enter();
    bulkhead->next = fdn_bulkheads;
    fdn_bulkheads = bulkhead;
    if (fdn_bulkhead_count == UINT64_MAX) {
        fdn_bulkhead_leave();
        fdn_panic_cstr("bulkhead count overflow");
    }
    ++fdn_bulkhead_count;
    fdn_bulkhead_leave();
    return (uint64_t)(uintptr_t)bulkhead;
}

void foundation_runtime_bulkhead_retain(uint64_t handle) {
    fdn_bulkhead *bulkhead = fdn_bulkhead_from_handle(handle);
    fdn_bulkhead_enter();
    if (!fdn_bulkhead_registered(bulkhead) || bulkhead->references == UINT64_MAX) {
        fdn_bulkhead_leave();
        fdn_panic_cstr("invalid bulkhead retain");
    }
    ++bulkhead->references;
    fdn_bulkhead_leave();
}

void foundation_runtime_bulkhead_release(uint64_t handle) {
    if (handle == 0) {
        return;
    }
    fdn_bulkhead *bulkhead = fdn_bulkhead_from_handle(handle);
    bool destroy = false;
    fdn_bulkhead_enter();
    if (!fdn_bulkhead_registered(bulkhead) || bulkhead->references == 0) {
        fdn_bulkhead_leave();
        fdn_panic_cstr("invalid bulkhead release");
    }
    --bulkhead->references;
    if (bulkhead->references == 0) {
        if (bulkhead->running != 0 || bulkhead->queued != 0 ||
            bulkhead->head != NULL || bulkhead->tail != NULL) {
            fdn_bulkhead_leave();
            fdn_panic_cstr("cannot destroy active bulkhead");
        }
        fdn_bulkhead_remove(bulkhead);
        destroy = true;
    }
    fdn_bulkhead_leave();
    if (destroy) {
        fdn_dealloc(bulkhead);
    }
}

void foundation_runtime_bulkhead_acquire(uint64_t handle,
                                         fdn_reactor_operation *operation) {
    fdn_bulkhead *bulkhead = fdn_bulkhead_from_handle(handle);
    int32_t status = 0;
    bool complete = true;
    fdn_bulkhead_enter();
    if (!fdn_bulkhead_registered(bulkhead)) {
        status = 3;
    } else if (bulkhead->running < bulkhead->max_concurrent) {
        ++bulkhead->running;
    } else if (bulkhead->queued >= bulkhead->max_queue) {
        status = 1;
    } else {
        fdn_bulkhead_waiter *waiter = fdn_alloc(sizeof(*waiter));
        waiter->next = NULL;
        waiter->operation = operation;
        if (bulkhead->tail == NULL) {
            bulkhead->head = waiter;
        } else {
            bulkhead->tail->next = waiter;
        }
        bulkhead->tail = waiter;
        ++bulkhead->queued;
        if (fdn_bulkhead_waiter_count == UINT64_MAX) {
            fdn_bulkhead_leave();
            fdn_panic_cstr("bulkhead waiter count overflow");
        }
        ++fdn_bulkhead_waiter_count;
        complete = false;
    }
    fdn_bulkhead_leave();
    if (complete) {
        fdn_reactor_complete(operation, status);
    }
}

void foundation_runtime_bulkhead_cancel(fdn_reactor_operation *operation) {
    fdn_bulkhead_waiter *removed = NULL;
    fdn_bulkhead_enter();
    for (fdn_bulkhead *bulkhead = fdn_bulkheads;
         bulkhead != NULL && removed == NULL; bulkhead = bulkhead->next) {
        fdn_bulkhead_waiter **current = &bulkhead->head;
        while (*current != NULL && (*current)->operation != operation) {
            current = &(*current)->next;
        }
        if (*current == NULL) {
            continue;
        }
        removed = *current;
        *current = removed->next;
        if (bulkhead->tail == removed) {
            bulkhead->tail = NULL;
            for (fdn_bulkhead_waiter *tail = bulkhead->head; tail != NULL;
                 tail = tail->next) {
                bulkhead->tail = tail;
            }
        }
        if (bulkhead->queued == 0 || fdn_bulkhead_waiter_count == 0) {
            fdn_bulkhead_leave();
            fdn_panic_cstr("bulkhead waiter count underflow");
        }
        --bulkhead->queued;
        --fdn_bulkhead_waiter_count;
    }
    fdn_bulkhead_leave();
    if (removed == NULL) {
        fdn_panic_cstr("bulkhead cancellation has no waiter");
    }
    fdn_dealloc(removed);
    fdn_reactor_complete(operation, 2);
}

void foundation_runtime_bulkhead_permit_release(uint64_t handle) {
    fdn_bulkhead *bulkhead = fdn_bulkhead_from_handle(handle);
    fdn_bulkhead_waiter *waiter = NULL;
    fdn_bulkhead_enter();
    if (!fdn_bulkhead_registered(bulkhead) || bulkhead->running == 0) {
        fdn_bulkhead_leave();
        fdn_panic_cstr("invalid bulkhead permit release");
    }
    waiter = bulkhead->head;
    if (waiter == NULL) {
        --bulkhead->running;
    } else {
        bulkhead->head = waiter->next;
        if (bulkhead->head == NULL) {
            bulkhead->tail = NULL;
        }
        if (bulkhead->queued == 0 || fdn_bulkhead_waiter_count == 0) {
            fdn_bulkhead_leave();
            fdn_panic_cstr("bulkhead waiter count underflow");
        }
        --bulkhead->queued;
        --fdn_bulkhead_waiter_count;
    }
    fdn_bulkhead_leave();
    if (waiter != NULL) {
        fdn_reactor_operation *operation = waiter->operation;
        fdn_dealloc(waiter);
        fdn_reactor_complete(operation, 0);
    }
}

uint64_t foundation_runtime_bulkhead_live_handles(void) {
    fdn_bulkhead_enter();
    const uint64_t result = fdn_bulkhead_count;
    fdn_bulkhead_leave();
    return result;
}

uint64_t foundation_runtime_bulkhead_live_waiters(void) {
    fdn_bulkhead_enter();
    const uint64_t result = fdn_bulkhead_waiter_count;
    fdn_bulkhead_leave();
    return result;
}
