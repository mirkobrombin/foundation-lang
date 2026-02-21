#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdint.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void foundation_native_sleep(uint64_t milliseconds) {
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / UINT64_C(1000));
    delay.tv_nsec = (long)(milliseconds % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&delay, &delay) != 0) {
    }
#endif
}

int32_t foundation_native_sleep_double(int32_t value, uint64_t delay) {
    foundation_native_sleep(delay);
    return value * 2;
}

fdn_string foundation_native_delayed_text(uint64_t delay) {
    foundation_native_sleep(delay);
    return fdn_string_static("blocking text", 13);
}
