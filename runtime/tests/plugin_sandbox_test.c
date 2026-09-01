#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <errno.h>
#include <time.h>
#include <unistd.h>
#endif

#define require(condition)                                                        \
    do {                                                                          \
        if (!(condition)) {                                                        \
            (void)fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__,    \
                          __LINE__, #condition);                                   \
            abort();                                                              \
        }                                                                         \
    } while (0)

static void pause_milliseconds(unsigned int milliseconds) {
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}

static int child_ready(int argc, char **argv) {
    char command[64];
    if (argc != 4 || strcmp(argv[2], "hello world") != 0 || argv[3][0] != '\0') {
        return 20;
    }
    (void)fputs("{\"ready\":true,\"version\":1}\n", stdout);
    (void)fflush(stdout);
    if (fgets(command, sizeof(command), stdin) == NULL ||
        strcmp(command, "{\"cmd\":\"stop\"}\n") != 0) {
        return 21;
    }
    return 0;
}

static int child_too_large(void) {
    size_t index;
    for (index = 0; index < 65537; ++index) {
        (void)fputc('a', stdout);
    }
    (void)fputc('\n', stdout);
    (void)fflush(stdout);
    pause_milliseconds(2000);
    return 0;
}

static int child_stop_timeout(void) {
    char command[64];
    (void)fputs("{\"ready\":true}\n", stdout);
    (void)fflush(stdout);
    if (fgets(command, sizeof(command), stdin) == NULL) {
        return 22;
    }
    pause_milliseconds(2000);
    return 0;
}

static int child_nonzero(void) {
    char command[64];
    (void)fputs("{\"ready\":true}\n", stdout);
    (void)fflush(stdout);
    if (fgets(command, sizeof(command), stdin) == NULL) {
        return 22;
    }
    return 9;
}

static int child_closed_input(void) {
#if defined(_WIN32)
    (void)_close(_fileno(stdin));
#else
    (void)close(STDIN_FILENO);
#endif
    (void)fputs("{\"ready\":true}\n", stdout);
    (void)fflush(stdout);
    pause_milliseconds(2000);
    return 0;
}

static int child_mode(int argc, char **argv) {
    if (argc < 2) {
        return -1;
    }
    if (strcmp(argv[1], "child-ready") == 0) {
        return child_ready(argc, argv);
    }
    if (strcmp(argv[1], "child-no-ready") == 0) {
        pause_milliseconds(2000);
        return 0;
    }
    if (strcmp(argv[1], "child-too-large") == 0) {
        return child_too_large();
    }
    if (strcmp(argv[1], "child-exit") == 0) {
        return 7;
    }
    if (strcmp(argv[1], "child-stop-timeout") == 0) {
        return child_stop_timeout();
    }
    if (strcmp(argv[1], "child-nonzero") == 0) {
        return child_nonzero();
    }
    if (strcmp(argv[1], "child-closed-input") == 0) {
        return child_closed_input();
    }
    return -1;
}

static uint64_t open_sandbox(const char *path) {
    fdn_string source = fdn_string_static(path, strlen(path));
    fdn_string detail = fdn_string_static("", 0);
    uint64_t handle = 0;
    require(foundation_runtime_plugin_sandbox_open(&source, &handle, &detail) == 0);
    require(handle != 0);
    require(detail.length == 0);
    return handle;
}

static void add_argument(uint64_t handle, const char *value) {
    fdn_string argument = fdn_string_static(value, strlen(value));
    fdn_string detail = fdn_string_static("", 0);
    require(foundation_runtime_plugin_sandbox_add_argument(handle, &argument, &detail) == 0);
    require(detail.length == 0);
}

static int32_t start_sandbox(uint64_t handle, uint64_t timeout,
                             fdn_string *ready, fdn_string *detail) {
    return foundation_runtime_plugin_sandbox_start(handle, timeout, ready, detail);
}

static void require_no_runtime_leaks(void) {
    require(foundation_runtime_plugin_sandbox_live_processes() == 0);
    require(foundation_runtime_plugin_sandbox_live_handles() == 0);
    require(fdn_live_allocations() == 0);
}

static void test_invalid_inputs(void) {
    fdn_string empty = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    fdn_string invalid = {"x\0y", 3, 0};
    uint64_t handle = 0;
    require(foundation_runtime_plugin_sandbox_open(&empty, &handle, &detail) == 1);
    require(handle == 0);
    require(detail.length != 0);
    fdn_string_drop(&detail);

    handle = open_sandbox("missing-foundation-sandbox-executable");
    require(foundation_runtime_plugin_sandbox_add_argument(handle, &invalid, &detail) == 2);
    require(start_sandbox(handle, UINT64_C(100000000), &empty, &detail) == 5);
    foundation_runtime_plugin_sandbox_close(&handle);
    fdn_string_drop(&detail);
    require_no_runtime_leaks();
}

static void test_happy_path(const char *self) {
    fdn_string ready = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    fdn_string late = fdn_string_static("late", 4);
    int32_t exit_code = -1;
    uint64_t handle = open_sandbox(self);
    add_argument(handle, "child-ready");
    add_argument(handle, "hello world");
    add_argument(handle, "");
    require(start_sandbox(handle, UINT64_C(2000000000), &ready, &detail) == 0);
    require(ready.length == strlen("{\"ready\":true,\"version\":1}"));
    require(memcmp(ready.data, "{\"ready\":true,\"version\":1}", ready.length) == 0);
    require(foundation_runtime_plugin_sandbox_add_argument(handle, &late, &detail) == 3);
    fdn_string_drop(&detail);
    require(foundation_runtime_plugin_sandbox_stop(
                handle, UINT64_C(2000000000), &exit_code, &detail) == 0);
    require(exit_code == 0);
    require(foundation_runtime_plugin_sandbox_stop(
                handle, UINT64_C(2000000000), &exit_code, &detail) == 4);
    fdn_string_drop(&ready);
    fdn_string_drop(&detail);
    foundation_runtime_plugin_sandbox_close(&handle);
    require(handle == 0);
    require_no_runtime_leaks();
}

static void test_start_failure(const char *self, const char *mode, uint64_t timeout,
                               int32_t expected) {
    fdn_string ready = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    int32_t status;
    uint64_t handle = open_sandbox(self);
    add_argument(handle, mode);
    status = start_sandbox(handle, timeout, &ready, &detail);
    if (status != expected) {
        (void)fprintf(stderr, "%s: expected start status %d, got %d\n", mode,
                      (int)expected, (int)status);
        abort();
    }
    require(ready.length == 0);
    require(detail.length != 0);
    require(foundation_runtime_plugin_sandbox_live_processes() == 0);
    fdn_string_drop(&ready);
    fdn_string_drop(&detail);
    foundation_runtime_plugin_sandbox_close(&handle);
    require_no_runtime_leaks();
}

static void test_stop_failure(const char *self, const char *mode, uint64_t timeout,
                              int32_t expected, int32_t expected_exit) {
    fdn_string ready = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    int32_t exit_code = 0;
    uint64_t handle = open_sandbox(self);
    add_argument(handle, mode);
    require(start_sandbox(handle, UINT64_C(2000000000), &ready, &detail) == 0);
    require(foundation_runtime_plugin_sandbox_stop(
                handle, timeout, &exit_code, &detail) == expected);
    require(exit_code == expected_exit);
    require(detail.length != 0);
    fdn_string_drop(&ready);
    fdn_string_drop(&detail);
    foundation_runtime_plugin_sandbox_close(&handle);
    require_no_runtime_leaks();
}

static void test_close_running(const char *self) {
    fdn_string ready = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    uint64_t handle = open_sandbox(self);
    add_argument(handle, "child-stop-timeout");
    require(start_sandbox(handle, UINT64_C(2000000000), &ready, &detail) == 0);
    foundation_runtime_plugin_sandbox_close(&handle);
    fdn_string_drop(&ready);
    fdn_string_drop(&detail);
    require_no_runtime_leaks();
}

int main(int argc, char **argv) {
    const int child = child_mode(argc, argv);
    if (child >= 0) {
        return child;
    }
    test_invalid_inputs();
    test_happy_path(argv[0]);
    test_start_failure(argv[0], "child-no-ready", UINT64_C(20000000), 6);
    test_start_failure(argv[0], "child-too-large", UINT64_C(2000000000), 7);
    test_start_failure(argv[0], "child-exit", UINT64_C(2000000000), 8);
    test_stop_failure(argv[0], "child-stop-timeout", UINT64_C(20000000), 10,
                      INT32_MIN);
    test_stop_failure(argv[0], "child-nonzero", UINT64_C(2000000000), 11, 9);
    test_stop_failure(argv[0], "child-closed-input", UINT64_C(20000000), 9,
                      INT32_MIN);
    test_close_running(argv[0]);
    return 0;
}
