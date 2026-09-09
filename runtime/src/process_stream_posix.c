#if !defined(_WIN32)
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "bytes_internal.h"
#include "foundation/process_internal.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    FDN_STREAM_NOT_FOUND = 1,
    FDN_STREAM_PERMISSION = 2,
    FDN_STREAM_INVALID_ARGUMENT = 3,
    FDN_STREAM_RESOURCE_LIMIT = 7,
    FDN_STREAM_IO = 8,
    FDN_STREAM_CLOSED = 9,
    FDN_STREAM_EOF = 11,
};

enum {
    FDN_STREAM_INPUT = 1,
    FDN_STREAM_OUTPUT = 2,
    FDN_STREAM_ERROR = 3,
    FDN_STREAM_CONTROLLER = 4,
    FDN_STREAM_WAITER = 5,
};

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct fdn_stream_process {
    int input;
    int output;
    int error;
    pid_t child;
    size_t references;
    bool waited;
    int32_t exit_code;
    pthread_mutex_t lock;
} fdn_stream_process;

typedef struct fdn_stream_handle {
    fdn_stream_process* process;
    uint8_t kind;
} fdn_stream_handle;

static atomic_uint_fast64_t fdn_live_stream_count;

static void fdn_stream_count_add(void) {
    if (atomic_fetch_add_explicit(&fdn_live_stream_count, 1, memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("process stream count overflow");
    }
}

static void fdn_stream_count_remove(void) {
    if (atomic_fetch_sub_explicit(&fdn_live_stream_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("process stream count underflow");
    }
}

uint64_t foundation_runtime_process_stream_live_handles(void) {
    return atomic_load_explicit(&fdn_live_stream_count, memory_order_relaxed);
}

static void fdn_stream_enter(fdn_stream_process* process) {
    if (pthread_mutex_lock(&process->lock) != 0) {
        fdn_panic_cstr("process stream lock failed");
    }
}

static void fdn_stream_leave(fdn_stream_process* process) {
    if (pthread_mutex_unlock(&process->lock) != 0) {
        fdn_panic_cstr("process stream unlock failed");
    }
}

static void fdn_stream_close_descriptor(int* descriptor) {
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static void fdn_stream_abort_process(fdn_stream_process* process) {
    fdn_stream_enter(process);
    if (!process->waited) {
        if (kill(-process->child, SIGKILL) != 0 && errno != ESRCH) {
            (void)kill(process->child, SIGKILL);
        }
    }
    fdn_stream_leave(process);
}

static int32_t fdn_stream_wait_process(fdn_stream_process* process, int32_t* exit_code) {
    pid_t child;
    siginfo_t information = {0};
    int status = 0;
    fdn_stream_enter(process);
    if (process->waited) {
        *exit_code = process->exit_code;
        fdn_stream_leave(process);
        return 0;
    }
    child = process->child;
    fdn_stream_leave(process);
    while (waitid(P_PID, (id_t)child, &information, WEXITED | WNOWAIT) < 0) {
        if (errno != EINTR) {
            return FDN_STREAM_IO;
        }
    }
    fdn_stream_enter(process);
    if (process->waited) {
        *exit_code = process->exit_code;
        fdn_stream_leave(process);
        return 0;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            fdn_stream_leave(process);
            return FDN_STREAM_IO;
        }
    }
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        fdn_stream_leave(process);
        return FDN_STREAM_IO;
    }
    process->waited = true;
    process->exit_code = *exit_code;
    fdn_stream_leave(process);
    return 0;
}

static void fdn_stream_close_endpoint(fdn_stream_process* process, uint8_t kind) {
    if (kind == FDN_STREAM_INPUT) {
        fdn_stream_close_descriptor(&process->input);
    } else if (kind == FDN_STREAM_OUTPUT) {
        fdn_stream_close_descriptor(&process->output);
    } else if (kind == FDN_STREAM_ERROR) {
        fdn_stream_close_descriptor(&process->error);
    }
}

static void fdn_stream_destroy(fdn_stream_process* process) {
    int32_t ignored = 0;
    fdn_stream_abort_process(process);
    (void)fdn_stream_wait_process(process, &ignored);
    fdn_stream_close_endpoint(process, FDN_STREAM_INPUT);
    fdn_stream_close_endpoint(process, FDN_STREAM_OUTPUT);
    fdn_stream_close_endpoint(process, FDN_STREAM_ERROR);
    if (pthread_mutex_destroy(&process->lock) != 0) {
        fdn_panic_cstr("process stream destroy failed");
    }
    fdn_dealloc(process);
    fdn_stream_count_remove();
}

static uint64_t fdn_stream_new_handle(fdn_stream_process* process, uint8_t kind) {
    fdn_stream_handle* handle = fdn_alloc(sizeof(*handle));
    handle->process = process;
    handle->kind = kind;
    return (uint64_t)(uintptr_t)handle;
}

static void fdn_stream_release(uint64_t value) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    fdn_stream_process* process;
    bool destroy;
    if (handle == NULL) {
        return;
    }
    process = handle->process;
    fdn_stream_enter(process);
    fdn_stream_close_endpoint(process, handle->kind);
    if (process->references == 0) {
        fdn_stream_leave(process);
        fdn_panic_cstr("process stream reference underflow");
    }
    --process->references;
    destroy = process->references == 0;
    fdn_stream_leave(process);
    fdn_dealloc(handle);
    if (destroy) {
        fdn_stream_destroy(process);
    }
}

static int fdn_stream_pipe(int descriptors[2]) {
    if (pipe(descriptors) != 0) {
        return 0;
    }
    if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
        fdn_stream_close_descriptor(&descriptors[0]);
        fdn_stream_close_descriptor(&descriptors[1]);
        return 0;
    }
    return 1;
}

static int fdn_stream_add_close(posix_spawn_file_actions_t* actions, int descriptor) {
    if (descriptor <= STDERR_FILENO) {
        return 0;
    }
    return posix_spawn_file_actions_addclose(actions, descriptor);
}

int32_t foundation_runtime_process_stream_start(uint64_t process_handle, uint64_t* input,
                                                uint64_t* output, uint64_t* error,
                                                uint64_t* controller, uint64_t* waiter) {
    const fdn_process* configuration = fdn_process_from_handle(process_handle);
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    char** arguments = NULL;
    char** environment = NULL;
    char* working_directory = NULL;
    pid_t child = 0;
    fdn_stream_process* process = NULL;
    int32_t status = 0;
    int actions_ready = 0;
    int attributes_ready = 0;
    int spawned = 0;
    if (input == NULL || output == NULL || error == NULL || controller == NULL || waiter == NULL) {
        fdn_panic_cstr("process stream output is null");
    }
    *input = 0;
    *output = 0;
    *error = 0;
    *controller = 0;
    *waiter = 0;
    if (configuration == NULL) {
        return FDN_STREAM_CLOSED;
    }
    arguments = fdn_process_posix_argv(configuration);
    environment = fdn_process_posix_environment(configuration);
    status = fdn_process_posix_cwd(configuration, &working_directory);
    if (arguments == NULL || environment == NULL) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (status != 0) {
        goto cleanup;
    }
    if (!fdn_stream_pipe(input_pipe) || !fdn_stream_pipe(output_pipe) ||
        !fdn_stream_pipe(error_pipe)) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    actions_ready = 1;
    if (posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, error_pipe[1], STDERR_FILENO) != 0 ||
        fdn_stream_add_close(&actions, input_pipe[0]) != 0 ||
        fdn_stream_add_close(&actions, input_pipe[1]) != 0 ||
        fdn_stream_add_close(&actions, output_pipe[0]) != 0 ||
        fdn_stream_add_close(&actions, output_pipe[1]) != 0 ||
        fdn_stream_add_close(&actions, error_pipe[0]) != 0 ||
        fdn_stream_add_close(&actions, error_pipe[1]) != 0 ||
        (working_directory != NULL &&
         posix_spawn_file_actions_addchdir_np(&actions, working_directory) != 0)) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (posix_spawnattr_init(&attributes) != 0) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    attributes_ready = 1;
    if (posix_spawnattr_setpgroup(&attributes, 0) != 0 ||
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) != 0) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    status = posix_spawnp(&child, arguments[0], &actions, &attributes, arguments, environment);
    if (status != 0) {
        status = fdn_process_posix_status(status);
        goto cleanup;
    }
    spawned = 1;
    fdn_stream_close_descriptor(&input_pipe[0]);
    fdn_stream_close_descriptor(&output_pipe[1]);
    fdn_stream_close_descriptor(&error_pipe[1]);
    process = fdn_alloc(sizeof(*process));
    process->input = input_pipe[1];
    process->output = output_pipe[0];
    process->error = error_pipe[0];
    process->child = child;
    process->references = 5;
    process->waited = false;
    process->exit_code = 0;
    if (pthread_mutex_init(&process->lock, NULL) != 0) {
        fdn_dealloc(process);
        process = NULL;
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    input_pipe[1] = -1;
    output_pipe[0] = -1;
    error_pipe[0] = -1;
    spawned = 0;
    *input = fdn_stream_new_handle(process, FDN_STREAM_INPUT);
    *output = fdn_stream_new_handle(process, FDN_STREAM_OUTPUT);
    *error = fdn_stream_new_handle(process, FDN_STREAM_ERROR);
    *controller = fdn_stream_new_handle(process, FDN_STREAM_CONTROLLER);
    *waiter = fdn_stream_new_handle(process, FDN_STREAM_WAITER);
    fdn_stream_count_add();
    status = 0;

cleanup:
    if (spawned) {
        (void)kill(-child, SIGKILL);
        (void)kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    fdn_stream_close_descriptor(&input_pipe[0]);
    fdn_stream_close_descriptor(&input_pipe[1]);
    fdn_stream_close_descriptor(&output_pipe[0]);
    fdn_stream_close_descriptor(&output_pipe[1]);
    fdn_stream_close_descriptor(&error_pipe[0]);
    fdn_stream_close_descriptor(&error_pipe[1]);
    if (attributes_ready) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    if (actions_ready) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    fdn_process_posix_argv_close(configuration, arguments);
    fdn_process_posix_environment_close(configuration, environment);
    fdn_dealloc(working_directory);
    return status;
}

int32_t foundation_runtime_process_stream_read(uint64_t value, uint64_t limit, uint64_t* result) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    int descriptor;
    uint8_t* data;
    ssize_t count;
    if (result == NULL) {
        fdn_panic_cstr("process stream read output is null");
    }
    *result = 0;
    if (handle == NULL || (handle->kind != FDN_STREAM_OUTPUT && handle->kind != FDN_STREAM_ERROR)) {
        return FDN_STREAM_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > (uint64_t)SIZE_MAX) {
        return FDN_STREAM_INVALID_ARGUMENT;
    }
    descriptor =
        handle->kind == FDN_STREAM_OUTPUT ? handle->process->output : handle->process->error;
    if (descriptor < 0) {
        return FDN_STREAM_CLOSED;
    }
    data = fdn_alloc((size_t)limit);
    do {
        count = read(descriptor, data, (size_t)limit);
    } while (count < 0 && errno == EINTR);
    if (count == 0) {
        fdn_dealloc(data);
        return FDN_STREAM_EOF;
    }
    if (count < 0) {
        const int native_error = errno;
        fdn_dealloc(data);
        return native_error == EBADF ? FDN_STREAM_CLOSED : FDN_STREAM_IO;
    }
    if (fdn_bytes_adopt(data, (size_t)count, (size_t)limit, result) != 0) {
        fdn_dealloc(data);
        return FDN_STREAM_IO;
    }
    return 0;
}

static ssize_t fdn_stream_write_no_sigpipe(int descriptor, const void* data, size_t length) {
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    int already_pending = 0;
    int delivered = 0;
    ssize_t result;
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGPIPE) != 0 ||
        pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0) {
        errno = EIO;
        return -1;
    }
    if (sigpending(&pending) == 0) {
        already_pending = sigismember(&pending, SIGPIPE) == 1;
    }
    do {
        result = write(descriptor, data, length);
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno == EPIPE && !already_pending) {
        (void)sigwait(&blocked, &delivered);
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    return result;
}

int32_t foundation_runtime_process_stream_write(uint64_t value, uint64_t bytes_handle) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    const uint8_t* data = NULL;
    size_t length = 0;
    size_t offset = 0;
    if (handle == NULL || handle->kind != FDN_STREAM_INPUT || handle->process->input < 0) {
        return FDN_STREAM_CLOSED;
    }
    if (fdn_bytes_view(bytes_handle, &data, &length) != 0) {
        return FDN_STREAM_INVALID_ARGUMENT;
    }
    while (offset < length) {
        const ssize_t count =
            fdn_stream_write_no_sigpipe(handle->process->input, data + offset, length - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else {
            return errno == EPIPE || errno == EBADF ? FDN_STREAM_CLOSED : FDN_STREAM_IO;
        }
    }
    return 0;
}

int32_t foundation_runtime_process_stream_wait(uint64_t value, int32_t* exit_code) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    if (exit_code == NULL) {
        fdn_panic_cstr("process stream exit output is null");
    }
    *exit_code = 0;
    if (handle == NULL || handle->kind != FDN_STREAM_WAITER) {
        return FDN_STREAM_CLOSED;
    }
    return fdn_stream_wait_process(handle->process, exit_code);
}

void foundation_runtime_process_stream_abort(uint64_t value) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    if (handle != NULL && handle->kind == FDN_STREAM_CONTROLLER) {
        fdn_stream_abort_process(handle->process);
    }
}

void foundation_runtime_process_stream_close(uint64_t value) { fdn_stream_release(value); }

#else
typedef int fdn_process_stream_posix_translation_unit;
#endif
