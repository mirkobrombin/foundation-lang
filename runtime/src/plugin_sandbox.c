#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
               "sandbox handles require a 64-bit carrier");

enum {
    FDN_SANDBOX_OK = 0,
    FDN_SANDBOX_INVALID_PATH = 1,
    FDN_SANDBOX_INVALID_ARGUMENT = 2,
    FDN_SANDBOX_ALREADY_STARTED = 3,
    FDN_SANDBOX_NOT_STARTED = 4,
    FDN_SANDBOX_SPAWN_FAILED = 5,
    FDN_SANDBOX_READY_TIMEOUT = 6,
    FDN_SANDBOX_READY_TOO_LARGE = 7,
    FDN_SANDBOX_READY_IO = 8,
    FDN_SANDBOX_STOP_WRITE_FAILED = 9,
    FDN_SANDBOX_STOP_TIMEOUT = 10,
    FDN_SANDBOX_EXIT_FAILED = 11,
    FDN_SANDBOX_CLOSED = 12,
};

enum {
    FDN_SANDBOX_CREATED = 0,
    FDN_SANDBOX_RUNNING = 1,
    FDN_SANDBOX_FINISHED = 2,
    FDN_SANDBOX_FAILED = 3,
};

enum {
    FDN_SANDBOX_MAX_ARGUMENTS = 256,
    FDN_SANDBOX_MAX_ARGUMENT_BYTES = 65536,
    FDN_SANDBOX_MAX_TOTAL_ARGUMENT_BYTES = 1048576,
    FDN_SANDBOX_MAX_READY_BYTES = 65536,
};

typedef struct fdn_plugin_sandbox {
    char *path;
    char **arguments;
    size_t argument_count;
    size_t argument_capacity;
    size_t argument_bytes;
    int state;
#if defined(_WIN32)
    HANDLE process;
    HANDLE input;
    HANDLE output;
#else
    pid_t process;
    int input;
    int output;
    int wait_status;
    int reaped;
#endif
} fdn_plugin_sandbox;

#if defined(_WIN32)
static volatile LONG64 fdn_sandbox_live_handle_count;
static volatile LONG64 fdn_sandbox_live_process_count;
#else
static atomic_uint_fast64_t fdn_sandbox_live_handle_count;
static atomic_uint_fast64_t fdn_sandbox_live_process_count;
#endif

static void fdn_sandbox_count_add(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    if (InterlockedIncrement64(count) <= 0) {
        fdn_panic_cstr("sandbox handle count overflow");
    }
#else
    if (atomic_fetch_add_explicit(count, 1, memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("sandbox handle count overflow");
    }
#endif
}

static void fdn_sandbox_count_remove(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    if (InterlockedDecrement64(count) < 0) {
        fdn_panic_cstr("sandbox handle count underflow");
    }
#else
    if (atomic_fetch_sub_explicit(count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("sandbox handle count underflow");
    }
#endif
}

static uint64_t fdn_sandbox_count_read(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(count, 0, 0);
#else
    return atomic_load_explicit(count, memory_order_relaxed);
#endif
}

uint64_t foundation_runtime_plugin_sandbox_live_handles(void) {
    return fdn_sandbox_count_read(&fdn_sandbox_live_handle_count);
}

uint64_t foundation_runtime_plugin_sandbox_live_processes(void) {
    return fdn_sandbox_count_read(&fdn_sandbox_live_process_count);
}

static void fdn_sandbox_set_string(fdn_string *output, const char *value,
                                   size_t length) {
    char *copy;
    if (output == NULL) {
        return;
    }
    fdn_string_drop(output);
    if (length == 0) {
        *output = fdn_string_static("", 0);
        return;
    }
    copy = fdn_alloc(length);
    (void)memcpy(copy, value, length);
    output->data = copy;
    output->length = length;
    output->owned = 1;
}

static void fdn_sandbox_set_detail(fdn_string *detail, const char *value) {
    fdn_sandbox_set_string(detail, value, strlen(value));
}

static int fdn_sandbox_text_valid(const fdn_string *value, size_t maximum,
                                  int allow_empty) {
    if (value == NULL || value->length > maximum ||
        (!allow_empty && value->length == 0) ||
        (value->length != 0 && value->data == NULL) ||
        (value->length != 0 && memchr(value->data, '\0', value->length) != NULL)) {
        return 0;
    }
    return fdn_utf8_valid(value->data, value->length) ? 1 : 0;
}

static char *fdn_sandbox_copy_text(const fdn_string *value) {
    char *copy = fdn_alloc(value->length + 1);
    if (value->length != 0) {
        (void)memcpy(copy, value->data, value->length);
    }
    copy[value->length] = '\0';
    return copy;
}

static fdn_plugin_sandbox *fdn_sandbox_from_handle(uint64_t handle) {
    return (fdn_plugin_sandbox *)(uintptr_t)handle;
}

static uint64_t fdn_sandbox_deadline(uint64_t timeout_nanoseconds) {
    const uint64_t now = fdn_monotonic_nanoseconds();
    if (timeout_nanoseconds > UINT64_MAX - now) {
        return UINT64_MAX;
    }
    return now + timeout_nanoseconds;
}

static uint64_t fdn_sandbox_remaining(uint64_t deadline) {
    const uint64_t now = fdn_monotonic_nanoseconds();
    return now >= deadline ? 0 : deadline - now;
}

static void fdn_sandbox_drop_arguments(fdn_plugin_sandbox *sandbox) {
    size_t index;
    if (sandbox->path != NULL) {
        fdn_dealloc(sandbox->path);
        sandbox->path = NULL;
    }
    for (index = 0; index < sandbox->argument_count; ++index) {
        fdn_dealloc(sandbox->arguments[index]);
    }
    if (sandbox->arguments != NULL) {
        fdn_dealloc(sandbox->arguments);
        sandbox->arguments = NULL;
    }
    sandbox->argument_count = 0;
    sandbox->argument_capacity = 0;
    sandbox->argument_bytes = 0;
}

#if defined(_WIN32)

static wchar_t *fdn_sandbox_wide(const char *value) {
    const size_t length = strlen(value);
    int wide_length;
    wchar_t *result;
    if (length == 0) {
        result = fdn_alloc(sizeof(*result));
        result[0] = L'\0';
        return result;
    }
    if (length > (size_t)INT_MAX) {
        return NULL;
    }
    wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
                                      (int)length, NULL, 0);
    if (wide_length <= 0) {
        return NULL;
    }
    result = fdn_alloc(((size_t)wide_length + 1) * sizeof(*result));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)length,
                            result, wide_length) != wide_length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[wide_length] = L'\0';
    return result;
}

static int fdn_sandbox_windows_needs_quotes(const wchar_t *value) {
    if (*value == L'\0') {
        return 1;
    }
    while (*value != L'\0') {
        if (*value == L' ' || *value == L'\t' || *value == L'\n' ||
            *value == L'\v' || *value == L'"') {
            return 1;
        }
        ++value;
    }
    return 0;
}

static size_t fdn_sandbox_windows_quoted_length(const wchar_t *value) {
    size_t length = 0;
    size_t slashes = 0;
    if (!fdn_sandbox_windows_needs_quotes(value)) {
        return wcslen(value);
    }
    length = 2;
    while (*value != L'\0') {
        if (*value == L'\\') {
            ++slashes;
            ++value;
            continue;
        }
        if (*value == L'"') {
            length += slashes * 2 + 2;
        } else {
            length += slashes + 1;
        }
        slashes = 0;
        ++value;
    }
    return length + slashes * 2;
}

static wchar_t *fdn_sandbox_windows_append_quoted(wchar_t *output,
                                                   const wchar_t *value) {
    size_t slashes = 0;
    if (!fdn_sandbox_windows_needs_quotes(value)) {
        while (*value != L'\0') {
            *output++ = *value++;
        }
        return output;
    }
    *output++ = L'"';
    while (*value != L'\0') {
        if (*value == L'\\') {
            ++slashes;
            ++value;
            continue;
        }
        if (*value == L'"') {
            size_t count = slashes * 2 + 1;
            while (count-- != 0) {
                *output++ = L'\\';
            }
            *output++ = L'"';
        } else {
            while (slashes != 0) {
                *output++ = L'\\';
                --slashes;
            }
            *output++ = *value;
        }
        slashes = 0;
        ++value;
    }
    while (slashes != 0) {
        *output++ = L'\\';
        *output++ = L'\\';
        --slashes;
    }
    *output++ = L'"';
    return output;
}

static wchar_t *fdn_sandbox_windows_command(const fdn_plugin_sandbox *sandbox) {
    wchar_t **values;
    wchar_t *command;
    wchar_t *cursor;
    size_t count = sandbox->argument_count + 1;
    size_t length = count - 1;
    size_t index;
    values = fdn_alloc(count * sizeof(*values));
    values[0] = fdn_sandbox_wide(sandbox->path);
    if (values[0] == NULL) {
        fdn_dealloc(values);
        return NULL;
    }
    length += fdn_sandbox_windows_quoted_length(values[0]);
    for (index = 0; index < sandbox->argument_count; ++index) {
        values[index + 1] = fdn_sandbox_wide(sandbox->arguments[index]);
        if (values[index + 1] == NULL) {
            size_t cleanup;
            for (cleanup = 0; cleanup <= index; ++cleanup) {
                fdn_dealloc(values[cleanup]);
            }
            fdn_dealloc(values);
            return NULL;
        }
        length += fdn_sandbox_windows_quoted_length(values[index + 1]);
    }
    if (length >= 32767) {
        for (index = 0; index < count; ++index) {
            fdn_dealloc(values[index]);
        }
        fdn_dealloc(values);
        return NULL;
    }
    command = fdn_alloc((length + 1) * sizeof(*command));
    cursor = command;
    for (index = 0; index < count; ++index) {
        if (index != 0) {
            *cursor++ = L' ';
        }
        cursor = fdn_sandbox_windows_append_quoted(cursor, values[index]);
        fdn_dealloc(values[index]);
    }
    *cursor = L'\0';
    fdn_dealloc(values);
    return command;
}

static void fdn_sandbox_close_pipes(fdn_plugin_sandbox *sandbox) {
    if (sandbox->input != NULL) {
        (void)CloseHandle(sandbox->input);
        sandbox->input = NULL;
    }
    if (sandbox->output != NULL) {
        (void)CloseHandle(sandbox->output);
        sandbox->output = NULL;
    }
}

static void fdn_sandbox_reap_process(fdn_plugin_sandbox *sandbox) {
    if (sandbox->process == NULL) {
        return;
    }
    (void)WaitForSingleObject(sandbox->process, INFINITE);
    (void)CloseHandle(sandbox->process);
    sandbox->process = NULL;
    fdn_sandbox_count_remove(&fdn_sandbox_live_process_count);
}

static void fdn_sandbox_abort_process(fdn_plugin_sandbox *sandbox) {
    if (sandbox->process != NULL) {
        if (WaitForSingleObject(sandbox->process, 0) == WAIT_TIMEOUT) {
            (void)TerminateProcess(sandbox->process, 255);
        }
        fdn_sandbox_reap_process(sandbox);
    }
    fdn_sandbox_close_pipes(sandbox);
    sandbox->state = FDN_SANDBOX_FAILED;
}

static int fdn_sandbox_spawn(fdn_plugin_sandbox *sandbox) {
    SECURITY_ATTRIBUTES attributes;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HANDLE child_input = NULL;
    HANDLE child_output = NULL;
    wchar_t *command = NULL;
    BOOL created;
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    (void)memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    (void)memset(&process, 0, sizeof(process));

    if (!CreatePipe(&child_input, &sandbox->input, &attributes, 0) ||
        !SetHandleInformation(sandbox->input, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&sandbox->output, &child_output, &attributes, 0) ||
        !SetHandleInformation(sandbox->output, HANDLE_FLAG_INHERIT, 0)) {
        if (child_input != NULL) {
            (void)CloseHandle(child_input);
        }
        if (child_output != NULL) {
            (void)CloseHandle(child_output);
        }
        fdn_sandbox_close_pipes(sandbox);
        return 0;
    }

    command = fdn_sandbox_windows_command(sandbox);
    if (command == NULL) {
        (void)CloseHandle(child_input);
        (void)CloseHandle(child_output);
        fdn_sandbox_close_pipes(sandbox);
        return 0;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input;
    startup.hStdOutput = child_output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    created = CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL,
                             &startup, &process);
    fdn_dealloc(command);
    (void)CloseHandle(child_input);
    (void)CloseHandle(child_output);
    if (!created) {
        fdn_sandbox_close_pipes(sandbox);
        return 0;
    }
    (void)CloseHandle(process.hThread);
    sandbox->process = process.hProcess;
    fdn_sandbox_count_add(&fdn_sandbox_live_process_count);
    return 1;
}

static int fdn_sandbox_read_ready(fdn_plugin_sandbox *sandbox, uint64_t deadline,
                                  fdn_string *ready) {
    char *buffer = fdn_alloc(FDN_SANDBOX_MAX_READY_BYTES + 1);
    size_t length = 0;
    int status = FDN_SANDBOX_READY_IO;
    for (;;) {
        DWORD available = 0;
        DWORD copied = 0;
        size_t index;
        if (PeekNamedPipe(sandbox->output, NULL, 0, NULL, &available, NULL) &&
            available != 0) {
            const DWORD wanted = available > 4096 ? 4096 : available;
            char chunk[4096];
            if (!ReadFile(sandbox->output, chunk, wanted, &copied, NULL) || copied == 0) {
                break;
            }
            for (index = 0; index < copied; ++index) {
                if (chunk[index] == '\n') {
                    if (length != 0 && buffer[length - 1] == '\r') {
                        --length;
                    }
                    if (!fdn_utf8_valid(buffer, length)) {
                        status = FDN_SANDBOX_READY_IO;
                    } else {
                        fdn_sandbox_set_string(ready, buffer, length);
                        status = FDN_SANDBOX_OK;
                    }
                    fdn_dealloc(buffer);
                    return status;
                }
                if (length == FDN_SANDBOX_MAX_READY_BYTES) {
                    fdn_dealloc(buffer);
                    return FDN_SANDBOX_READY_TOO_LARGE;
                }
                buffer[length++] = chunk[index];
            }
            continue;
        }
        if (WaitForSingleObject(sandbox->process, 0) == WAIT_OBJECT_0) {
            break;
        }
        if (fdn_sandbox_remaining(deadline) == 0) {
            status = FDN_SANDBOX_READY_TIMEOUT;
            break;
        }
        Sleep(1);
    }
    fdn_dealloc(buffer);
    return status;
}

static int fdn_sandbox_wait_stop(fdn_plugin_sandbox *sandbox, uint64_t deadline,
                                 int32_t *exit_code) {
    DWORD wait_milliseconds;
    DWORD native_exit = 0;
    const uint64_t remaining = fdn_sandbox_remaining(deadline);
    if (remaining == 0) {
        wait_milliseconds = 0;
    } else if (remaining / UINT64_C(1000000) >= (uint64_t)(INFINITE - 1)) {
        wait_milliseconds = INFINITE - 1;
    } else {
        wait_milliseconds = (DWORD)((remaining + UINT64_C(999999)) / UINT64_C(1000000));
    }
    if (WaitForSingleObject(sandbox->process, wait_milliseconds) != WAIT_OBJECT_0) {
        (void)TerminateProcess(sandbox->process, 255);
        fdn_sandbox_reap_process(sandbox);
        fdn_sandbox_close_pipes(sandbox);
        return FDN_SANDBOX_STOP_TIMEOUT;
    }
    if (!GetExitCodeProcess(sandbox->process, &native_exit)) {
        fdn_sandbox_reap_process(sandbox);
        fdn_sandbox_close_pipes(sandbox);
        return FDN_SANDBOX_EXIT_FAILED;
    }
    *exit_code = native_exit > INT32_MAX ? INT32_MAX : (int32_t)native_exit;
    fdn_sandbox_reap_process(sandbox);
    fdn_sandbox_close_pipes(sandbox);
    return native_exit == 0 ? FDN_SANDBOX_OK : FDN_SANDBOX_EXIT_FAILED;
}

#else

static void fdn_sandbox_close_descriptor(int *descriptor) {
    if (*descriptor >= 0) {
        while (close(*descriptor) != 0 && errno == EINTR) {
        }
        *descriptor = -1;
    }
}

static void fdn_sandbox_close_pipes(fdn_plugin_sandbox *sandbox) {
    fdn_sandbox_close_descriptor(&sandbox->input);
    fdn_sandbox_close_descriptor(&sandbox->output);
}

static void fdn_sandbox_reap_process(fdn_plugin_sandbox *sandbox) {
    pid_t waited;
    if (sandbox->process <= 0 || sandbox->reaped) {
        return;
    }
    do {
        waited = waitpid(sandbox->process, &sandbox->wait_status, 0);
    } while (waited < 0 && errno == EINTR);
    sandbox->reaped = 1;
    sandbox->process = 0;
    fdn_sandbox_count_remove(&fdn_sandbox_live_process_count);
}

static void fdn_sandbox_abort_process(fdn_plugin_sandbox *sandbox) {
    if (sandbox->process > 0 && !sandbox->reaped) {
        if (kill(sandbox->process, SIGKILL) != 0 && errno != ESRCH) {
        }
        fdn_sandbox_reap_process(sandbox);
    }
    fdn_sandbox_close_pipes(sandbox);
    sandbox->state = FDN_SANDBOX_FAILED;
}

static int fdn_sandbox_spawn(fdn_plugin_sandbox *sandbox) {
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    char **arguments;
    size_t index;
    int status;
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        fdn_sandbox_close_descriptor(&input_pipe[0]);
        fdn_sandbox_close_descriptor(&input_pipe[1]);
        fdn_sandbox_close_descriptor(&output_pipe[0]);
        fdn_sandbox_close_descriptor(&output_pipe[1]);
        return 0;
    }
    arguments = fdn_alloc((sandbox->argument_count + 2) * sizeof(*arguments));
    arguments[0] = sandbox->path;
    for (index = 0; index < sandbox->argument_count; ++index) {
        arguments[index + 1] = sandbox->arguments[index];
    }
    arguments[sandbox->argument_count + 1] = NULL;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        fdn_dealloc(arguments);
        fdn_sandbox_close_descriptor(&input_pipe[0]);
        fdn_sandbox_close_descriptor(&input_pipe[1]);
        fdn_sandbox_close_descriptor(&output_pipe[0]);
        fdn_sandbox_close_descriptor(&output_pipe[1]);
        return 0;
    }
    status = posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO);
    if (status == 0) {
        status = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    }
    if (status == 0 && input_pipe[0] != STDIN_FILENO) {
        status = posix_spawn_file_actions_addclose(&actions, input_pipe[0]);
    }
    if (status == 0 && output_pipe[1] != STDOUT_FILENO) {
        status = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
    }
    if (status == 0) {
        status = posix_spawn_file_actions_addclose(&actions, input_pipe[1]);
    }
    if (status == 0) {
        status = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    }
    if (status == 0) {
        status = posix_spawnp(&sandbox->process, sandbox->path, &actions, NULL,
                              arguments, environ);
    }
    (void)posix_spawn_file_actions_destroy(&actions);
    fdn_dealloc(arguments);
    fdn_sandbox_close_descriptor(&input_pipe[0]);
    fdn_sandbox_close_descriptor(&output_pipe[1]);
    if (status != 0) {
        fdn_sandbox_close_descriptor(&input_pipe[1]);
        fdn_sandbox_close_descriptor(&output_pipe[0]);
        sandbox->process = 0;
        return 0;
    }
    sandbox->input = input_pipe[1];
    sandbox->output = output_pipe[0];
    sandbox->reaped = 0;
    sandbox->wait_status = 0;
    fdn_sandbox_count_add(&fdn_sandbox_live_process_count);
    if (fcntl(sandbox->output, F_SETFL,
              fcntl(sandbox->output, F_GETFL, 0) | O_NONBLOCK) < 0) {
        fdn_sandbox_abort_process(sandbox);
        return 0;
    }
    return 1;
}

static ssize_t fdn_sandbox_write_no_sigpipe(int descriptor, const void *data,
                                            size_t length) {
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

static int fdn_sandbox_process_exited(fdn_plugin_sandbox *sandbox) {
    pid_t result;
    if (sandbox->reaped || sandbox->process <= 0) {
        return 1;
    }
    do {
        result = waitpid(sandbox->process, &sandbox->wait_status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == sandbox->process || (result < 0 && errno == ECHILD)) {
        sandbox->reaped = 1;
        sandbox->process = 0;
        fdn_sandbox_count_remove(&fdn_sandbox_live_process_count);
        return 1;
    }
    return 0;
}

static int fdn_sandbox_read_ready(fdn_plugin_sandbox *sandbox, uint64_t deadline,
                                  fdn_string *ready) {
    char *buffer = fdn_alloc(FDN_SANDBOX_MAX_READY_BYTES + 1);
    size_t length = 0;
    int status = FDN_SANDBOX_READY_IO;
    for (;;) {
        struct pollfd descriptor;
        const uint64_t remaining = fdn_sandbox_remaining(deadline);
        int timeout;
        int result;
        descriptor.fd = sandbox->output;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        if (remaining == 0) {
            timeout = 0;
        } else if (remaining / UINT64_C(1000000) > (uint64_t)INT_MAX) {
            timeout = INT_MAX;
        } else {
            timeout = (int)((remaining + UINT64_C(999999)) / UINT64_C(1000000));
        }
        do {
            result = poll(&descriptor, 1, timeout);
        } while (result < 0 && errno == EINTR);
        if (result == 0) {
            status = FDN_SANDBOX_READY_TIMEOUT;
            break;
        }
        if (result < 0) {
            break;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char chunk[4096];
            ssize_t copied;
            size_t index;
            do {
                copied = read(sandbox->output, chunk, sizeof(chunk));
            } while (copied < 0 && errno == EINTR);
            if (copied > 0) {
                for (index = 0; index < (size_t)copied; ++index) {
                    if (chunk[index] == '\n') {
                        if (length != 0 && buffer[length - 1] == '\r') {
                            --length;
                        }
                        if (!fdn_utf8_valid(buffer, length)) {
                            status = FDN_SANDBOX_READY_IO;
                        } else {
                            fdn_sandbox_set_string(ready, buffer, length);
                            status = FDN_SANDBOX_OK;
                        }
                        fdn_dealloc(buffer);
                        return status;
                    }
                    if (length == FDN_SANDBOX_MAX_READY_BYTES) {
                        fdn_dealloc(buffer);
                        return FDN_SANDBOX_READY_TOO_LARGE;
                    }
                    buffer[length++] = chunk[index];
                }
                continue;
            }
            if (copied == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;
            }
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0 ||
            fdn_sandbox_process_exited(sandbox)) {
            break;
        }
    }
    fdn_dealloc(buffer);
    return status;
}

static void fdn_sandbox_pause(void) {
    struct timespec delay = {0, 1000000L};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static int fdn_sandbox_wait_stop(fdn_plugin_sandbox *sandbox, uint64_t deadline,
                                 int32_t *exit_code) {
    while (!fdn_sandbox_process_exited(sandbox)) {
        if (fdn_sandbox_remaining(deadline) == 0) {
            fdn_sandbox_abort_process(sandbox);
            return FDN_SANDBOX_STOP_TIMEOUT;
        }
        fdn_sandbox_pause();
    }
    fdn_sandbox_close_pipes(sandbox);
    if (!WIFEXITED(sandbox->wait_status)) {
        *exit_code = -1;
        return FDN_SANDBOX_EXIT_FAILED;
    }
    *exit_code = WEXITSTATUS(sandbox->wait_status);
    return *exit_code == 0 ? FDN_SANDBOX_OK : FDN_SANDBOX_EXIT_FAILED;
}

#endif

int32_t foundation_runtime_plugin_sandbox_open(const fdn_string *path,
                                               uint64_t *handle,
                                               fdn_string *detail) {
    fdn_plugin_sandbox *sandbox;
    if (handle == NULL || *handle != 0 ||
        !fdn_sandbox_text_valid(path, FDN_SANDBOX_MAX_ARGUMENT_BYTES, 0)) {
        fdn_sandbox_set_detail(detail, "invalid sandbox executable path");
        return FDN_SANDBOX_INVALID_PATH;
    }
    sandbox = fdn_alloc(sizeof(*sandbox));
    (void)memset(sandbox, 0, sizeof(*sandbox));
    sandbox->path = fdn_sandbox_copy_text(path);
    sandbox->state = FDN_SANDBOX_CREATED;
#if !defined(_WIN32)
    sandbox->input = -1;
    sandbox->output = -1;
#endif
    fdn_sandbox_count_add(&fdn_sandbox_live_handle_count);
    *handle = (uint64_t)(uintptr_t)sandbox;
    return FDN_SANDBOX_OK;
}

int32_t foundation_runtime_plugin_sandbox_add_argument(uint64_t handle,
                                                       const fdn_string *argument,
                                                       fdn_string *detail) {
    fdn_plugin_sandbox *sandbox = fdn_sandbox_from_handle(handle);
    char **grown;
    size_t capacity;
    if (sandbox == NULL) {
        fdn_sandbox_set_detail(detail, "sandbox is closed");
        return FDN_SANDBOX_CLOSED;
    }
    if (sandbox->state != FDN_SANDBOX_CREATED) {
        fdn_sandbox_set_detail(detail, "sandbox arguments are frozen after start");
        return FDN_SANDBOX_ALREADY_STARTED;
    }
    if (!fdn_sandbox_text_valid(argument, FDN_SANDBOX_MAX_ARGUMENT_BYTES, 1) ||
        sandbox->argument_count == FDN_SANDBOX_MAX_ARGUMENTS ||
        argument->length > FDN_SANDBOX_MAX_TOTAL_ARGUMENT_BYTES -
                               sandbox->argument_bytes) {
        fdn_sandbox_set_detail(detail, "invalid or excessive sandbox argument");
        return FDN_SANDBOX_INVALID_ARGUMENT;
    }
    if (sandbox->argument_count == sandbox->argument_capacity) {
        capacity = sandbox->argument_capacity == 0 ? 4 : sandbox->argument_capacity * 2;
        grown = fdn_alloc(capacity * sizeof(*grown));
        if (sandbox->argument_count != 0) {
            (void)memcpy(grown, sandbox->arguments,
                         sandbox->argument_count * sizeof(*grown));
        }
        if (sandbox->arguments != NULL) {
            fdn_dealloc(sandbox->arguments);
        }
        sandbox->arguments = grown;
        sandbox->argument_capacity = capacity;
    }
    sandbox->arguments[sandbox->argument_count++] = fdn_sandbox_copy_text(argument);
    sandbox->argument_bytes += argument->length;
    return FDN_SANDBOX_OK;
}

int32_t foundation_runtime_plugin_sandbox_start(uint64_t handle,
                                                uint64_t timeout_nanoseconds,
                                                fdn_string *ready,
                                                fdn_string *detail) {
    fdn_plugin_sandbox *sandbox = fdn_sandbox_from_handle(handle);
    int status;
    if (sandbox == NULL) {
        fdn_sandbox_set_detail(detail, "sandbox is closed");
        return FDN_SANDBOX_CLOSED;
    }
    if (sandbox->state != FDN_SANDBOX_CREATED) {
        fdn_sandbox_set_detail(detail, "sandbox has already been started");
        return FDN_SANDBOX_ALREADY_STARTED;
    }
    sandbox->state = FDN_SANDBOX_FAILED;
    if (!fdn_sandbox_spawn(sandbox)) {
        fdn_sandbox_set_detail(detail, "could not start sandbox process");
        return FDN_SANDBOX_SPAWN_FAILED;
    }
    status = fdn_sandbox_read_ready(sandbox, fdn_sandbox_deadline(timeout_nanoseconds), ready);
    if (status != FDN_SANDBOX_OK) {
        fdn_sandbox_abort_process(sandbox);
        if (status == FDN_SANDBOX_READY_TIMEOUT) {
            fdn_sandbox_set_detail(detail, "sandbox ready message timed out");
        } else if (status == FDN_SANDBOX_READY_TOO_LARGE) {
            fdn_sandbox_set_detail(detail, "sandbox ready message exceeds 65536 bytes");
        } else {
            fdn_sandbox_set_detail(detail, "could not read sandbox ready message");
        }
        return status;
    }
    sandbox->state = FDN_SANDBOX_RUNNING;
    return FDN_SANDBOX_OK;
}

int32_t foundation_runtime_plugin_sandbox_stop(uint64_t handle,
                                               uint64_t timeout_nanoseconds,
                                               int32_t *exit_code,
                                               fdn_string *detail) {
    static const char command[] = "{\"cmd\":\"stop\"}\n";
    fdn_plugin_sandbox *sandbox = fdn_sandbox_from_handle(handle);
    int status;
    if (exit_code != NULL) {
        *exit_code = INT32_MIN;
    }
    if (sandbox == NULL) {
        fdn_sandbox_set_detail(detail, "sandbox is closed");
        return FDN_SANDBOX_CLOSED;
    }
    if (sandbox->state != FDN_SANDBOX_RUNNING || exit_code == NULL) {
        fdn_sandbox_set_detail(detail, "sandbox is not running");
        return FDN_SANDBOX_NOT_STARTED;
    }
#if defined(_WIN32)
    {
        DWORD written = 0;
        if (!WriteFile(sandbox->input, command, (DWORD)(sizeof(command) - 1),
                       &written, NULL) || written != sizeof(command) - 1) {
            fdn_sandbox_abort_process(sandbox);
            fdn_sandbox_set_detail(detail, "could not write sandbox stop command");
            return FDN_SANDBOX_STOP_WRITE_FAILED;
        }
        (void)CloseHandle(sandbox->input);
        sandbox->input = NULL;
    }
#else
    {
        size_t offset = 0;
        while (offset < sizeof(command) - 1) {
            ssize_t written;
            written = fdn_sandbox_write_no_sigpipe(
                sandbox->input, command + offset, sizeof(command) - 1 - offset);
            if (written <= 0) {
                fdn_sandbox_abort_process(sandbox);
                fdn_sandbox_set_detail(detail, "could not write sandbox stop command");
                return FDN_SANDBOX_STOP_WRITE_FAILED;
            }
            offset += (size_t)written;
        }
        fdn_sandbox_close_descriptor(&sandbox->input);
    }
#endif
    status = fdn_sandbox_wait_stop(sandbox, fdn_sandbox_deadline(timeout_nanoseconds),
                                   exit_code);
    sandbox->state = status == FDN_SANDBOX_OK ? FDN_SANDBOX_FINISHED
                                              : FDN_SANDBOX_FAILED;
    if (status == FDN_SANDBOX_STOP_TIMEOUT) {
        fdn_sandbox_set_detail(detail, "sandbox stop timed out and process was terminated");
    } else if (status == FDN_SANDBOX_EXIT_FAILED) {
        fdn_sandbox_set_detail(detail, "sandbox process exited unsuccessfully");
    }
    return status;
}

void foundation_runtime_plugin_sandbox_abort(uint64_t handle) {
    fdn_plugin_sandbox *sandbox = fdn_sandbox_from_handle(handle);
    if (sandbox != NULL && sandbox->state == FDN_SANDBOX_RUNNING) {
        fdn_sandbox_abort_process(sandbox);
    }
}

void foundation_runtime_plugin_sandbox_close(uint64_t *handle) {
    fdn_plugin_sandbox *sandbox;
    if (handle == NULL || *handle == 0) {
        return;
    }
    sandbox = fdn_sandbox_from_handle(*handle);
    *handle = 0;
    if (sandbox->state == FDN_SANDBOX_RUNNING) {
        fdn_sandbox_abort_process(sandbox);
    } else {
        fdn_sandbox_close_pipes(sandbox);
    }
    fdn_sandbox_drop_arguments(sandbox);
    fdn_dealloc(sandbox);
    fdn_sandbox_count_remove(&fdn_sandbox_live_handle_count);
}
