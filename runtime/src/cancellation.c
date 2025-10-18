#include "foundation/runtime.h"

#include <limits.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdatomic.h>
#endif

typedef struct fdn_cancellation_state {
#if defined(_WIN32)
    volatile LONG references;
    volatile LONG requested;
#else
    atomic_size_t references;
    atomic_bool requested;
#endif
} fdn_cancellation_state;

#if defined(_WIN32)
static volatile LONG fdn_cancellation_count;
#else
static atomic_uint_fast64_t fdn_cancellation_count;
#endif

static void fdn_cancellation_count_add(void) {
#if defined(_WIN32)
    if (InterlockedIncrement(&fdn_cancellation_count) <= 0) {
        fdn_panic_cstr("cancellation state count overflow");
    }
#else
    const uint64_t previous = atomic_fetch_add_explicit(&fdn_cancellation_count, 1,
                                                         memory_order_relaxed);
    if (previous == UINT64_MAX) {
        fdn_panic_cstr("cancellation state count overflow");
    }
#endif
}

static void fdn_cancellation_count_remove(void) {
#if defined(_WIN32)
    if (InterlockedDecrement(&fdn_cancellation_count) < 0) {
        fdn_panic_cstr("cancellation state count underflow");
    }
#else
    const uint64_t previous = atomic_fetch_sub_explicit(&fdn_cancellation_count, 1,
                                                         memory_order_relaxed);
    if (previous == 0) {
        fdn_panic_cstr("cancellation state count underflow");
    }
#endif
}

static fdn_cancellation_state *fdn_cancellation_from_handle(uint64_t handle) {
    if (handle == 0) {
        fdn_panic_cstr("invalid cancellation handle");
    }
    return (fdn_cancellation_state *)(uintptr_t)handle;
}

#if defined(_WIN32)
static void fdn_cancellation_retain(fdn_cancellation_state *state) {
    LONG current = state->references;
    for (;;) {
        if (current <= 0 || current == LONG_MAX) {
            fdn_panic_cstr("invalid cancellation reference count");
        }
        const LONG observed = InterlockedCompareExchange(&state->references, current + 1, current);
        if (observed == current) {
            return;
        }
        current = observed;
    }
}

static bool fdn_cancellation_release(fdn_cancellation_state *state) {
    LONG current = state->references;
    for (;;) {
        if (current <= 0) {
            fdn_panic_cstr("invalid cancellation reference count");
        }
        const LONG observed = InterlockedCompareExchange(&state->references, current - 1, current);
        if (observed == current) {
            return current == 1;
        }
        current = observed;
    }
}
#else
static void fdn_cancellation_retain(fdn_cancellation_state *state) {
    size_t current = atomic_load_explicit(&state->references, memory_order_relaxed);
    for (;;) {
        if (current == 0 || current == SIZE_MAX) {
            fdn_panic_cstr("invalid cancellation reference count");
        }
        if (atomic_compare_exchange_weak_explicit(&state->references, &current, current + 1,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static bool fdn_cancellation_release(fdn_cancellation_state *state) {
    size_t current = atomic_load_explicit(&state->references, memory_order_relaxed);
    for (;;) {
        if (current == 0) {
            fdn_panic_cstr("invalid cancellation reference count");
        }
        if (atomic_compare_exchange_weak_explicit(&state->references, &current, current - 1,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed)) {
            return current == 1;
        }
    }
}
#endif

uint64_t foundation_runtime_cancellation_open(void) {
    fdn_cancellation_state *state = fdn_alloc(sizeof(*state));
#if defined(_WIN32)
    state->references = 1;
    state->requested = 0;
#else
    atomic_init(&state->references, 1);
    atomic_init(&state->requested, false);
#endif
    fdn_cancellation_count_add();
    return (uint64_t)(uintptr_t)state;
}

uint64_t foundation_runtime_cancellation_retain(uint64_t handle) {
    fdn_cancellation_retain(fdn_cancellation_from_handle(handle));
    return handle;
}

void foundation_runtime_cancellation_request(uint64_t handle) {
    fdn_cancellation_state *state = fdn_cancellation_from_handle(handle);
#if defined(_WIN32)
    InterlockedExchange(&state->requested, 1);
#else
    atomic_store_explicit(&state->requested, true, memory_order_release);
#endif
}

bool foundation_runtime_cancellation_requested(uint64_t handle) {
    fdn_cancellation_state *state = fdn_cancellation_from_handle(handle);
#if defined(_WIN32)
    const bool requested = InterlockedCompareExchange(&state->requested, 0, 0) != 0;
#else
    const bool requested = atomic_load_explicit(&state->requested, memory_order_acquire);
#endif
    return requested || fdn_task_cancellation_current();
}

void foundation_runtime_cancellation_release(uint64_t handle) {
    fdn_cancellation_state *state;
    if (handle == 0) {
        return;
    }
    state = fdn_cancellation_from_handle(handle);
    if (fdn_cancellation_release(state)) {
        fdn_cancellation_count_remove();
        fdn_dealloc(state);
    }
}

uint64_t foundation_runtime_cancellation_live_states(void) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange(&fdn_cancellation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_cancellation_count, memory_order_relaxed);
#endif
}
