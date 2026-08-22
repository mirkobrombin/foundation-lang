#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#endif

typedef struct fdn_watch_snapshot {
    bool exists;
    uint64_t size;
    uint64_t modified_high;
    uint64_t modified_low;
} fdn_watch_snapshot;

typedef struct fdn_watch_job {
    struct fdn_watch_job *next;
    fdn_string path;
    uint64_t interval_milliseconds;
    uint32_t *kind;
    fdn_string *event_path;
    fdn_reactor_operation *operation;
    bool cancelled;
} fdn_watch_job;

enum { FDN_WATCH_MAX_JOBS = 64 };

static fdn_watch_job *fdn_watch_jobs;
static uint64_t fdn_watch_job_count;

#if defined(_WIN32)
static SRWLOCK fdn_watch_lock_value = SRWLOCK_INIT;

static void fdn_watch_lock(void) {
    AcquireSRWLockExclusive(&fdn_watch_lock_value);
}

static void fdn_watch_unlock(void) {
    ReleaseSRWLockExclusive(&fdn_watch_lock_value);
}
#else
static pthread_mutex_t fdn_watch_lock_value = PTHREAD_MUTEX_INITIALIZER;

static void fdn_watch_lock(void) {
    if (pthread_mutex_lock(&fdn_watch_lock_value) != 0) {
        fdn_panic_cstr("watch lock failed");
    }
}

static void fdn_watch_unlock(void) {
    if (pthread_mutex_unlock(&fdn_watch_lock_value) != 0) {
        fdn_panic_cstr("watch unlock failed");
    }
}
#endif

static bool fdn_watch_path_valid(const fdn_string *path) {
    if (path == NULL || path->data == NULL || path->length == 0) {
        return false;
    }
#if !defined(_WIN32)
    if (path->length == SIZE_MAX) {
        return false;
    }
#endif
    return memchr(path->data, 0, path->length) == NULL;
}

#if defined(_WIN32)
static wchar_t *fdn_watch_windows_path(const fdn_string *path) {
    if (path->length > INT32_MAX) {
        return NULL;
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                                           (int)path->length, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    wchar_t *result = fdn_alloc(((size_t)length + 1) * sizeof(*result));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                            (int)path->length, result, length) != length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[length] = L'\0';
    return result;
}

static int32_t fdn_watch_snapshot_read(const fdn_string *path,
                                       fdn_watch_snapshot *snapshot) {
    wchar_t *native_path = fdn_watch_windows_path(path);
    if (native_path == NULL) {
        return 3;
    }
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    const BOOL found = GetFileAttributesExW(native_path, GetFileExInfoStandard, &attributes);
    const DWORD error = found ? ERROR_SUCCESS : GetLastError();
    fdn_dealloc(native_path);
    if (!found && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return 0;
    }
    if (!found) {
        return 1;
    }
    snapshot->exists = true;
    snapshot->size = ((uint64_t)attributes.nFileSizeHigh << 32) |
                     (uint64_t)attributes.nFileSizeLow;
    snapshot->modified_high = attributes.ftLastWriteTime.dwHighDateTime;
    snapshot->modified_low = attributes.ftLastWriteTime.dwLowDateTime;
    return 0;
}

static void fdn_watch_sleep(uint64_t milliseconds) {
    Sleep((DWORD)(milliseconds > UINT32_MAX ? UINT32_MAX : milliseconds));
}
#else
static int32_t fdn_watch_snapshot_read(const fdn_string *path,
                                       fdn_watch_snapshot *snapshot) {
    char *native_path = fdn_alloc(path->length + 1);
    memcpy(native_path, path->data, path->length);
    native_path[path->length] = '\0';
    struct stat attributes;
    const int found = stat(native_path, &attributes);
    const int error = errno;
    fdn_dealloc(native_path);
    if (found != 0 && (error == ENOENT || error == ENOTDIR)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return 0;
    }
    if (found != 0) {
        return 1;
    }
    snapshot->exists = true;
    snapshot->size = attributes.st_size < 0 ? 0 : (uint64_t)attributes.st_size;
#if defined(__APPLE__)
    snapshot->modified_high = (uint64_t)attributes.st_mtimespec.tv_sec;
    snapshot->modified_low = (uint64_t)attributes.st_mtimespec.tv_nsec;
#else
    snapshot->modified_high = (uint64_t)attributes.st_mtim.tv_sec;
    snapshot->modified_low = (uint64_t)attributes.st_mtim.tv_nsec;
#endif
    return 0;
}

static void fdn_watch_sleep(uint64_t milliseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / UINT64_C(1000));
    delay.tv_nsec = (long)(milliseconds % UINT64_C(1000)) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}
#endif

static bool fdn_watch_snapshot_equal(const fdn_watch_snapshot *left,
                                     const fdn_watch_snapshot *right) {
    return left->exists == right->exists && left->size == right->size &&
           left->modified_high == right->modified_high &&
           left->modified_low == right->modified_low;
}

static bool fdn_watch_cancelled(fdn_watch_job *job) {
    fdn_watch_lock();
    const bool cancelled = job->cancelled;
    fdn_watch_unlock();
    return cancelled;
}

static bool fdn_watch_wait(fdn_watch_job *job) {
    uint64_t remaining = job->interval_milliseconds;
    while (remaining != 0) {
        if (fdn_watch_cancelled(job)) {
            return false;
        }
        const uint64_t slice = remaining > 10 ? 10 : remaining;
        fdn_watch_sleep(slice);
        remaining -= slice;
    }
    return !fdn_watch_cancelled(job);
}

static void fdn_watch_remove(fdn_watch_job *job) {
    fdn_watch_lock();
    fdn_watch_job **current = &fdn_watch_jobs;
    while (*current != NULL && *current != job) {
        current = &(*current)->next;
    }
    if (*current == job) {
        *current = job->next;
        if (fdn_watch_job_count == 0) {
            fdn_watch_unlock();
            fdn_panic_cstr("watch job count underflow");
        }
        --fdn_watch_job_count;
    }
    fdn_watch_unlock();
}

static void fdn_watch_finish(fdn_watch_job *job, int32_t status) {
    fdn_reactor_operation *operation = job->operation;
    fdn_watch_remove(job);
    fdn_string_drop(&job->path);
    fdn_dealloc(job);
    fdn_reactor_complete(operation, status);
}

static void fdn_watch_run(fdn_watch_job *job) {
    fdn_watch_snapshot previous;
    int32_t status = fdn_watch_snapshot_read(&job->path, &previous);
    while (status == 0 && fdn_watch_wait(job)) {
        fdn_watch_snapshot current;
        status = fdn_watch_snapshot_read(&job->path, &current);
        if (status != 0) {
            break;
        }
        if (fdn_watch_snapshot_equal(&previous, &current)) {
            continue;
        }
        *job->kind = !previous.exists && current.exists ? 1U
                     : previous.exists && !current.exists ? 3U
                                                          : 2U;
        *job->event_path = foundation_runtime_string_copy(&job->path);
        fdn_watch_finish(job, 0);
        return;
    }
    if (status == 0) {
        status = 2;
    }
    fdn_watch_finish(job, status);
}

#if defined(_WIN32)
static DWORD WINAPI fdn_watch_thread(void *raw) {
    fdn_watch_run(raw);
    return 0;
}
#else
static void *fdn_watch_thread(void *raw) {
    fdn_watch_run(raw);
    return NULL;
}
#endif

void foundation_runtime_fs_watch_start(const fdn_string *path,
                                       uint64_t interval_milliseconds,
                                       uint32_t *kind, fdn_string *event_path,
                                       fdn_reactor_operation *operation) {
    if (!fdn_watch_path_valid(path) || interval_milliseconds == 0 || kind == NULL ||
        event_path == NULL || operation == NULL) {
        if (operation != NULL) {
            fdn_reactor_complete(operation, 3);
        }
        return;
    }
    fdn_watch_job *job = fdn_alloc(sizeof(*job));
    job->next = NULL;
    job->path = foundation_runtime_string_copy(path);
    job->interval_milliseconds = interval_milliseconds;
    job->kind = kind;
    job->event_path = event_path;
    job->operation = operation;
    job->cancelled = false;
    fdn_watch_lock();
    if (fdn_watch_job_count >= FDN_WATCH_MAX_JOBS) {
        fdn_watch_unlock();
        fdn_string_drop(&job->path);
        fdn_dealloc(job);
        fdn_reactor_complete(operation, 4);
        return;
    }
    job->next = fdn_watch_jobs;
    fdn_watch_jobs = job;
    ++fdn_watch_job_count;
    fdn_watch_unlock();
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, fdn_watch_thread, job, 0, NULL);
    if (thread == NULL) {
        fdn_watch_finish(job, 1);
        return;
    }
    CloseHandle(thread);
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, fdn_watch_thread, job) != 0) {
        fdn_watch_finish(job, 1);
        return;
    }
    if (pthread_detach(thread) != 0) {
        fdn_panic_cstr("cannot detach watch thread");
    }
#endif
}

void foundation_runtime_fs_watch_cancel(fdn_reactor_operation *operation) {
    fdn_watch_lock();
    for (fdn_watch_job *job = fdn_watch_jobs; job != NULL; job = job->next) {
        if (job->operation == operation) {
            job->cancelled = true;
            break;
        }
    }
    fdn_watch_unlock();
}

uint64_t foundation_runtime_fs_watch_live_jobs(void) {
    fdn_watch_lock();
    const uint64_t result = fdn_watch_job_count;
    fdn_watch_unlock();
    return result;
}
