#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static wchar_t *foundation_watch_path;
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
static char *foundation_watch_path;
#endif

typedef struct foundation_watch_change {
    uint64_t delay_milliseconds;
    fdn_reactor_operation *operation;
} foundation_watch_change;

static void foundation_watch_sleep(uint64_t milliseconds) {
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / UINT64_C(1000));
    delay.tv_nsec = (long)(milliseconds % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}

static int32_t foundation_watch_write_changed(void) {
    static const char contents[] = "changed contents";
#if defined(_WIN32)
    HANDLE file = CreateFileW(foundation_watch_path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 1;
    }
    DWORD written = 0;
    const BOOL success = WriteFile(file, contents, (DWORD)(sizeof(contents) - 1),
                                   &written, NULL);
    CloseHandle(file);
    return success && written == sizeof(contents) - 1 ? 0 : 1;
#else
    const int file = open(foundation_watch_path, O_WRONLY | O_TRUNC);
    if (file < 0) {
        return 1;
    }
    const ssize_t written = write(file, contents, sizeof(contents) - 1);
    close(file);
    return written == (ssize_t)(sizeof(contents) - 1) ? 0 : 1;
#endif
}

static void foundation_watch_change_run(foundation_watch_change *change) {
    foundation_watch_sleep(change->delay_milliseconds);
    const int32_t status = foundation_watch_write_changed();
    fdn_reactor_complete(change->operation, status);
    fdn_dealloc(change);
}

#if defined(_WIN32)
static DWORD WINAPI foundation_watch_change_thread(void *raw) {
    foundation_watch_change_run(raw);
    return 0;
}
#else
static void *foundation_watch_change_thread(void *raw) {
    foundation_watch_change_run(raw);
    return NULL;
}
#endif

int32_t foundation_watch_fixture_prepare(fdn_string *path) {
    if (path == NULL || foundation_watch_path != NULL) {
        return 1;
    }
#if defined(_WIN32)
    wchar_t directory[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length > MAX_PATH) {
        return 1;
    }
    foundation_watch_path = fdn_alloc((MAX_PATH + 1) * sizeof(*foundation_watch_path));
    if (GetTempFileNameW(directory, L"fdn", 0, foundation_watch_path) == 0) {
        fdn_dealloc(foundation_watch_path);
        foundation_watch_path = NULL;
        return 1;
    }
    const int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                 foundation_watch_path, -1,
                                                 NULL, 0, NULL, NULL);
    if (utf8_length <= 1) {
        DeleteFileW(foundation_watch_path);
        fdn_dealloc(foundation_watch_path);
        foundation_watch_path = NULL;
        return 1;
    }
    char *utf8 = fdn_alloc((size_t)utf8_length);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, foundation_watch_path, -1,
                            utf8, utf8_length, NULL, NULL) != utf8_length) {
        fdn_dealloc(utf8);
        DeleteFileW(foundation_watch_path);
        fdn_dealloc(foundation_watch_path);
        foundation_watch_path = NULL;
        return 1;
    }
    *path = (fdn_string){utf8, (size_t)utf8_length - 1, 1};
#else
    char pattern[] = "/tmp/foundation-watch-XXXXXX";
    const int file = mkstemp(pattern);
    if (file < 0) {
        return 1;
    }
    close(file);
    const size_t length = strlen(pattern);
    foundation_watch_path = fdn_alloc(length + 1);
    memcpy(foundation_watch_path, pattern, length + 1);
    fdn_string source = fdn_string_static(pattern, length);
    *path = foundation_runtime_string_copy(&source);
#endif
    static const char initial[] = "initial";
#if defined(_WIN32)
    HANDLE file = CreateFileW(foundation_watch_path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 1;
    }
    DWORD written = 0;
    const BOOL success = WriteFile(file, initial, (DWORD)(sizeof(initial) - 1),
                                   &written, NULL);
    CloseHandle(file);
    return success && written == sizeof(initial) - 1 ? 0 : 1;
#else
    const int output = open(foundation_watch_path, O_WRONLY | O_TRUNC);
    if (output < 0) {
        return 1;
    }
    const ssize_t written = write(output, initial, sizeof(initial) - 1);
    close(output);
    return written == (ssize_t)(sizeof(initial) - 1) ? 0 : 1;
#endif
}

void foundation_watch_fixture_change_start(uint64_t delay_milliseconds,
                                           fdn_reactor_operation *operation) {
    foundation_watch_change *change = fdn_alloc(sizeof(*change));
    change->delay_milliseconds = delay_milliseconds;
    change->operation = operation;
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, foundation_watch_change_thread, change, 0, NULL);
    if (thread == NULL) {
        fdn_dealloc(change);
        fdn_reactor_complete(operation, 1);
        return;
    }
    CloseHandle(thread);
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, foundation_watch_change_thread, change) != 0) {
        fdn_dealloc(change);
        fdn_reactor_complete(operation, 1);
        return;
    }
    if (pthread_detach(thread) != 0) {
        fdn_panic_cstr("cannot detach watch fixture thread");
    }
#endif
}

int32_t foundation_watch_fixture_cleanup(void) {
    if (foundation_watch_path == NULL) {
        return 1;
    }
#if defined(_WIN32)
    const BOOL removed = DeleteFileW(foundation_watch_path);
#else
    const int removed = unlink(foundation_watch_path) == 0;
#endif
    fdn_dealloc(foundation_watch_path);
    foundation_watch_path = NULL;
    return removed ? 0 : 1;
}
