#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "bytes_internal.h"
#include "foundation/process_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||               \
    defined(__OpenBSD__)
#include <util.h>
#else
#include <pty.h>
#endif

enum {
    FDN_PTY_NOT_FOUND = 1,
    FDN_PTY_PERMISSION = 2,
    FDN_PTY_INVALID_ARGUMENT = 3,
    FDN_PTY_RESOURCE_LIMIT = 7,
    FDN_PTY_IO = 8,
    FDN_PTY_CLOSED = 9,
    FDN_PTY_EOF = 11,
};

typedef struct fdn_pty {
    int descriptor;
    pid_t child;
    size_t references;
    bool waited;
    int32_t exit_code;
    pthread_mutex_t lock;
} fdn_pty;

static atomic_uint_fast64_t fdn_live_pty_count;

static int fdn_pty_size_add(size_t *total, size_t value) {
    if (value > SIZE_MAX - *total) {
        return 0;
    }
    *total += value;
    return 1;
}

static void fdn_pty_count_add(void) {
    if (atomic_fetch_add_explicit(&fdn_live_pty_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("pty handle count overflow");
    }
}

static void fdn_pty_count_remove(void) {
    if (atomic_fetch_sub_explicit(&fdn_live_pty_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("pty handle count underflow");
    }
}

uint64_t foundation_runtime_process_pty_live_handles(void) {
    return atomic_load_explicit(&fdn_live_pty_count, memory_order_relaxed);
}

static void fdn_pty_enter(fdn_pty *pty) {
    if (pthread_mutex_lock(&pty->lock) != 0) {
        fdn_panic_cstr("pty lock failed");
    }
}

static void fdn_pty_leave(fdn_pty *pty) {
    if (pthread_mutex_unlock(&pty->lock) != 0) {
        fdn_panic_cstr("pty unlock failed");
    }
}

static void fdn_pty_kill(fdn_pty *pty) {
    fdn_pty_enter(pty);
    if (!pty->waited) {
        if (kill(-pty->child, SIGKILL) != 0 && errno != ESRCH) {
            (void)kill(pty->child, SIGKILL);
        }
    }
    fdn_pty_leave(pty);
}

static int32_t fdn_pty_wait_child(fdn_pty *pty, int32_t *exit_code) {
    pid_t child;
    int status = 0;
    fdn_pty_enter(pty);
    if (pty->waited) {
        *exit_code = pty->exit_code;
        fdn_pty_leave(pty);
        return 0;
    }
    child = pty->child;
    fdn_pty_leave(pty);
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return FDN_PTY_IO;
        }
    }
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        return FDN_PTY_IO;
    }
    fdn_pty_enter(pty);
    pty->waited = true;
    pty->exit_code = *exit_code;
    fdn_pty_leave(pty);
    return 0;
}

static void fdn_pty_destroy(fdn_pty *pty) {
    int32_t ignored = 0;
    fdn_pty_kill(pty);
    (void)fdn_pty_wait_child(pty, &ignored);
    (void)close(pty->descriptor);
    if (pthread_mutex_destroy(&pty->lock) != 0) {
        fdn_panic_cstr("pty destroy failed");
    }
    fdn_dealloc(pty);
    fdn_pty_count_remove();
}

static void fdn_pty_release(uint64_t handle) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    bool destroy;
    if (pty == NULL) {
        return;
    }
    fdn_pty_enter(pty);
    if (pty->references == 0) {
        fdn_pty_leave(pty);
        fdn_panic_cstr("pty reference underflow");
    }
    --pty->references;
    destroy = pty->references == 0;
    fdn_pty_leave(pty);
    if (destroy) {
        fdn_pty_destroy(pty);
    }
}

static const char *fdn_pty_environment_value(char **environment, const char *name) {
    const size_t name_length = strlen(name);
    size_t index = 0;
    while (environment[index] != NULL) {
        if (strncmp(environment[index], name, name_length) == 0 &&
            environment[index][name_length] == '=') {
            return environment[index] + name_length + 1;
        }
        ++index;
    }
    return NULL;
}

static char *fdn_pty_join_path(const char *directory, size_t directory_length,
                               const char *program) {
    const size_t program_length = strlen(program);
    const bool separator =
        directory_length != 0 && directory[directory_length - 1] != '/';
    size_t total = 0;
    char *result;
    if (!fdn_pty_size_add(&total, directory_length) ||
        !fdn_pty_size_add(&total, program_length) ||
        !fdn_pty_size_add(&total, separator ? 2 : 1)) {
        return NULL;
    }
    result = fdn_alloc(total);
    if (directory_length != 0) {
        (void)memcpy(result, directory, directory_length);
    }
    if (separator) {
        result[directory_length++] = '/';
    }
    (void)memcpy(result + directory_length, program, program_length + 1);
    return result;
}

static char *fdn_pty_join_working_path(const char *working_directory,
                                       const char *directory, size_t directory_length,
                                       const char *program) {
    const size_t working_length = strlen(working_directory);
    const size_t program_length = strlen(program);
    const bool working_separator =
        working_length != 0 && working_directory[working_length - 1] != '/';
    const bool directory_separator =
        directory_length != 0 && directory[directory_length - 1] != '/';
    const size_t separators =
        (working_separator ? 1 : 0) + (directory_separator ? 1 : 0);
    size_t total = 0;
    char *result;
    size_t offset = 0;
    if (!fdn_pty_size_add(&total, working_length) ||
        !fdn_pty_size_add(&total, directory_length) ||
        !fdn_pty_size_add(&total, program_length) ||
        !fdn_pty_size_add(&total, separators) || !fdn_pty_size_add(&total, 1)) {
        return NULL;
    }
    result = fdn_alloc(total);
    if (working_length != 0) {
        (void)memcpy(result, working_directory, working_length);
        offset = working_length;
    }
    if (working_separator) {
        result[offset++] = '/';
    }
    if (directory_length != 0) {
        (void)memcpy(result + offset, directory, directory_length);
        offset += directory_length;
    }
    if (directory_separator) {
        result[offset++] = '/';
    }
    (void)memcpy(result + offset, program, program_length + 1);
    return result;
}

static void fdn_pty_executables_close(char **executables, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        fdn_dealloc(executables[index]);
    }
    fdn_dealloc(executables);
}

static char **fdn_pty_executables(const char *program, char **environment,
                                  const char *working_directory, size_t *count) {
    const char *path;
    const char *cursor;
    size_t capacity = 1;
    size_t index = 0;
    char **executables;
    *count = 0;
    if (strchr(program, '/') != NULL) {
        executables = fdn_alloc(sizeof(*executables));
        executables[0] = fdn_pty_join_path("", 0, program);
        if (executables[0] == NULL) {
            fdn_dealloc(executables);
            return NULL;
        }
        *count = 1;
        return executables;
    }
    path = fdn_pty_environment_value(environment, "PATH");
    if (path == NULL) {
        path = "/bin:/usr/bin";
    }
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == ':') {
            if (capacity == SIZE_MAX) {
                return NULL;
            }
            ++capacity;
        }
    }
    if (capacity > SIZE_MAX / sizeof(*executables)) {
        return NULL;
    }
    executables = fdn_alloc(capacity * sizeof(*executables));
    cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        const size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        if (length == 0 && working_directory != NULL) {
            executables[index] = fdn_pty_join_path(working_directory,
                                                   strlen(working_directory), program);
        } else if (length != 0 && cursor[0] != '/' && working_directory != NULL) {
            executables[index] =
                fdn_pty_join_working_path(working_directory, cursor, length, program);
        } else {
            const char *directory = length == 0 ? "." : cursor;
            const size_t directory_length = length == 0 ? 1 : length;
            executables[index] =
                fdn_pty_join_path(directory, directory_length, program);
        }
        if (executables[index] == NULL) {
            fdn_pty_executables_close(executables, index);
            return NULL;
        }
        ++index;
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
    }
    *count = index;
    return executables;
}

static void fdn_pty_child_fail(int descriptor, int error) {
    const int saved = error;
    const char *data = (const char *)&saved;
    size_t offset = 0;
    while (offset < sizeof(saved)) {
        const ssize_t count = write(descriptor, data + offset, sizeof(saved) - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && errno != EINTR) {
            break;
        }
    }
    _exit(127);
}

static void fdn_pty_exec_child(int master, int slave, int error_read, int error_write,
                               char **arguments, char **environment, char **executables,
                               size_t executable_count, const char *working_directory) {
    size_t index;
    int failure = ENOENT;
    (void)close(master);
    (void)close(error_read);
    if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 ||
        dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
        dup2(slave, STDERR_FILENO) < 0) {
        fdn_pty_child_fail(error_write, errno);
    }
    if (slave > STDERR_FILENO) {
        (void)close(slave);
    }
    if (working_directory != NULL && chdir(working_directory) != 0) {
        fdn_pty_child_fail(error_write, errno);
    }
    for (index = 0; index < executable_count; ++index) {
        execve(executables[index], arguments, environment);
        if (errno == EACCES) {
            failure = EACCES;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            failure = errno;
            break;
        }
    }
    fdn_pty_child_fail(error_write, failure);
}

static int32_t fdn_pty_exec_status(int descriptor) {
    int child_error = 0;
    char *data = (char *)&child_error;
    size_t offset = 0;
    while (offset < sizeof(child_error)) {
        const ssize_t count =
            read(descriptor, data + offset, sizeof(child_error) - offset);
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count == 0) {
            return offset == 0 ? 0 : FDN_PTY_IO;
        }
        if (errno != EINTR) {
            return FDN_PTY_IO;
        }
    }
    return fdn_process_posix_status(child_error);
}

int32_t foundation_runtime_process_pty_start(uint64_t process_handle, uint16_t columns,
                                             uint16_t rows, uint64_t *reader,
                                             uint64_t *writer, uint64_t *controller,
                                             uint64_t *waiter) {
    const fdn_process *process = fdn_process_from_handle(process_handle);
    struct winsize size;
    int master = -1;
    int slave = -1;
    int errors[2] = {-1, -1};
    char **arguments = NULL;
    char **environment = NULL;
    char **executables = NULL;
    char *working_directory = NULL;
    size_t executable_count = 0;
    pid_t child = -1;
    fdn_pty *pty = NULL;
    int32_t status = 0;
    if (reader == NULL || writer == NULL || controller == NULL || waiter == NULL) {
        fdn_panic_cstr("pty output is null");
    }
    *reader = 0;
    *writer = 0;
    *controller = 0;
    *waiter = 0;
    if (process == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (columns == 0 || rows == 0) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    arguments = fdn_process_posix_argv(process);
    environment = fdn_process_posix_environment(process);
    status = fdn_process_posix_cwd(process, &working_directory);
    if (arguments == NULL || environment == NULL) {
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (status != 0) {
        goto cleanup;
    }
    executables = fdn_pty_executables(arguments[0], environment, working_directory,
                                      &executable_count);
    if (executables == NULL) {
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    (void)memset(&size, 0, sizeof(size));
    size.ws_col = columns;
    size.ws_row = rows;
    if (openpty(&master, &slave, NULL, NULL, &size) != 0 || pipe(errors) != 0 ||
        fcntl(errors[1], F_SETFD, FD_CLOEXEC) != 0) {
        status = fdn_process_posix_status(errno);
        goto cleanup;
    }
    child = fork();
    if (child < 0) {
        status = fdn_process_posix_status(errno);
        goto cleanup;
    }
    if (child == 0) {
        fdn_pty_exec_child(master, slave, errors[0], errors[1], arguments, environment,
                           executables, executable_count, working_directory);
    }
    (void)close(slave);
    slave = -1;
    (void)close(errors[1]);
    errors[1] = -1;
    status = fdn_pty_exec_status(errors[0]);
    if (status != 0) {
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
        child = -1;
        goto cleanup;
    }
    pty = fdn_alloc(sizeof(*pty));
    pty->descriptor = master;
    pty->child = child;
    pty->references = 4;
    pty->waited = false;
    pty->exit_code = 0;
    if (pthread_mutex_init(&pty->lock, NULL) != 0) {
        fdn_dealloc(pty);
        pty = NULL;
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    master = -1;
    child = -1;
    *reader = (uint64_t)(uintptr_t)pty;
    *writer = (uint64_t)(uintptr_t)pty;
    *controller = (uint64_t)(uintptr_t)pty;
    *waiter = (uint64_t)(uintptr_t)pty;
    fdn_pty_count_add();

cleanup:
    if (child > 0) {
        (void)kill(-child, SIGKILL);
        (void)kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    if (master >= 0) {
        (void)close(master);
    }
    if (slave >= 0) {
        (void)close(slave);
    }
    if (errors[0] >= 0) {
        (void)close(errors[0]);
    }
    if (errors[1] >= 0) {
        (void)close(errors[1]);
    }
    fdn_process_posix_argv_close(process, arguments);
    fdn_process_posix_environment_close(process, environment);
    fdn_pty_executables_close(executables, executable_count);
    fdn_dealloc(working_directory);
    return status;
}

int32_t foundation_runtime_process_pty_read(uint64_t handle, uint64_t limit,
                                            uint64_t *result) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    uint8_t *data;
    ssize_t count;
    if (result == NULL) {
        fdn_panic_cstr("pty read output is null");
    }
    *result = 0;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > (uint64_t)SIZE_MAX) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    data = fdn_alloc((size_t)limit);
    do {
        count = read(pty->descriptor, data, (size_t)limit);
    } while (count < 0 && errno == EINTR);
    if (count == 0 || (count < 0 && errno == EIO)) {
        fdn_dealloc(data);
        return FDN_PTY_EOF;
    }
    if (count < 0) {
        const int error = errno;
        fdn_dealloc(data);
        return error == EBADF ? FDN_PTY_CLOSED : FDN_PTY_IO;
    }
    if (fdn_bytes_adopt(data, (size_t)count, (size_t)limit, result) != 0) {
        fdn_dealloc(data);
        return FDN_PTY_IO;
    }
    return 0;
}

int32_t foundation_runtime_process_pty_write(uint64_t handle, uint64_t bytes_handle) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    const uint8_t *data = NULL;
    size_t length = 0;
    size_t offset = 0;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (fdn_bytes_view(bytes_handle, &data, &length) != 0) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    while (offset < length) {
        const ssize_t count = write(pty->descriptor, data + offset, length - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return errno == EPIPE || errno == EBADF ? FDN_PTY_CLOSED : FDN_PTY_IO;
        }
    }
    return 0;
}

int32_t foundation_runtime_process_pty_resize(uint64_t handle, uint16_t columns,
                                              uint16_t rows) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    struct winsize size;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (columns == 0 || rows == 0) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    (void)memset(&size, 0, sizeof(size));
    size.ws_col = columns;
    size.ws_row = rows;
    return ioctl(pty->descriptor, TIOCSWINSZ, &size) == 0 ? 0 : FDN_PTY_IO;
}

int32_t foundation_runtime_process_pty_wait(uint64_t handle, int32_t *exit_code) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    if (exit_code == NULL) {
        fdn_panic_cstr("pty exit output is null");
    }
    *exit_code = 0;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    return fdn_pty_wait_child(pty, exit_code);
}

void foundation_runtime_process_pty_abort(uint64_t handle) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    if (pty != NULL) {
        fdn_pty_kill(pty);
    }
}

void foundation_runtime_process_pty_reader_close(uint64_t handle) {
    fdn_pty_release(handle);
}

void foundation_runtime_process_pty_writer_close(uint64_t handle) {
    fdn_pty_release(handle);
}

void foundation_runtime_process_pty_controller_close(uint64_t handle) {
    fdn_pty_release(handle);
}

void foundation_runtime_process_pty_waiter_close(uint64_t handle) {
    fdn_pty_release(handle);
}

#else
typedef int fdn_pty_posix_translation_unit;
#endif
