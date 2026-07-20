#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int32_t retry_calls;
static int32_t state_changes;

int32_t foundation_native_sleep_double(int32_t value, uint64_t delay) {
#if defined(_WIN32)
    Sleep((DWORD)delay);
#else
    struct timespec remaining;
    remaining.tv_sec = (time_t)(delay / UINT64_C(1000));
    remaining.tv_nsec = (long)(delay % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&remaining, &remaining) != 0) {
    }
#endif
    return value * 2;
}

void foundation_native_retry_reset(void) {
    retry_calls = 0;
}

int32_t foundation_native_retry_next(void) {
    ++retry_calls;
    return retry_calls;
}

int32_t foundation_native_retry_calls(void) {
    return retry_calls;
}

void foundation_native_state_change(void) {
    ++state_changes;
}

void foundation_native_state_reset(void) {
    state_changes = 0;
}

int32_t foundation_native_state_changes(void) {
    return state_changes;
}
