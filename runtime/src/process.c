#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "foundation/runtime.h"
#include "bytes_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

enum {
    FDN_PROCESS_NOT_FOUND = 1,
    FDN_PROCESS_PERMISSION = 2,
    FDN_PROCESS_INVALID_ARGUMENT = 3,
    FDN_PROCESS_INVALID_ENVIRONMENT = 4,
    FDN_PROCESS_INVALID_WORKING_DIRECTORY = 5,
    FDN_PROCESS_OUTPUT_LIMIT = 6,
    FDN_PROCESS_RESOURCE_LIMIT = 7,
    FDN_PROCESS_IO = 8,
    FDN_PROCESS_CLOSED = 9,
};

typedef struct fdn_process {
    fdn_string program;
    fdn_string working_directory;
    fdn_string *arguments;
    size_t argument_count;
    size_t argument_capacity;
    fdn_string *environment;
    size_t environment_count;
    size_t environment_capacity;
    size_t output_limit;
    bool has_working_directory;
    bool inherit_environment;
} fdn_process;

typedef struct fdn_process_capture {
    uint8_t *data;
    size_t length;
    size_t capacity;
    size_t limit;
    bool exceeded;
    bool failed;
} fdn_process_capture;

#if defined(_WIN32)
static volatile LONG64 fdn_live_process_count;
#else
static atomic_uint_fast64_t fdn_live_process_count;
#endif

static void fdn_process_count_add(void) {
#if defined(_WIN32)
    if (InterlockedIncrement64(&fdn_live_process_count) <= 0) {
        fdn_panic_cstr("process handle count overflow");
    }
#else
    if (atomic_fetch_add_explicit(&fdn_live_process_count, 1,
                                  memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("process handle count overflow");
    }
#endif
}

static void fdn_process_count_remove(void) {
#if defined(_WIN32)
    if (InterlockedDecrement64(&fdn_live_process_count) < 0) {
        fdn_panic_cstr("process handle count underflow");
    }
#else
    if (atomic_fetch_sub_explicit(&fdn_live_process_count, 1,
                                  memory_order_relaxed) == 0) {
        fdn_panic_cstr("process handle count underflow");
    }
#endif
}

uint64_t foundation_runtime_process_live_handles(void) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(&fdn_live_process_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_live_process_count, memory_order_relaxed);
#endif
}

static int fdn_process_text_valid(const fdn_string *value, bool allow_empty) {
    size_t offset;
    if (value == NULL || value->data == NULL ||
        (!allow_empty && value->length == 0) ||
        !fdn_utf8_valid(value->data, value->length)) {
        return 0;
    }
    for (offset = 0; offset < value->length; ++offset) {
        if (value->data[offset] == '\0') {
            return 0;
        }
    }
    return 1;
}

static int fdn_process_environment_valid(const fdn_string *value) {
    size_t offset;
    if (!fdn_process_text_valid(value, false) || value->data[0] == '=') {
        return 0;
    }
    for (offset = 0; offset < value->length; ++offset) {
        if (value->data[offset] == '=') {
            return offset != 0;
        }
    }
    return 0;
}

static size_t fdn_process_environment_name_length(const fdn_string *value) {
    size_t length = 0;
    while (value->data[length] != '=') {
        ++length;
    }
    return length;
}

static int fdn_process_environment_name_equal(const fdn_string *left,
                                              const fdn_string *right) {
    const size_t left_length = fdn_process_environment_name_length(left);
    const size_t right_length = fdn_process_environment_name_length(right);
    size_t offset;
    if (left_length != right_length) {
        return 0;
    }
    for (offset = 0; offset < left_length; ++offset) {
        uint8_t left_byte = (uint8_t)left->data[offset];
        uint8_t right_byte = (uint8_t)right->data[offset];
#if defined(_WIN32)
        if (left_byte >= (uint8_t)'a' && left_byte <= (uint8_t)'z') {
            left_byte = (uint8_t)(left_byte - ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (right_byte >= (uint8_t)'a' && right_byte <= (uint8_t)'z') {
            right_byte = (uint8_t)(right_byte - ((uint8_t)'a' - (uint8_t)'A'));
        }
#endif
        if (left_byte != right_byte) {
            return 0;
        }
    }
    return 1;
}

static int fdn_process_reserve(fdn_string **values, size_t used,
                               size_t *capacity, size_t required) {
    size_t next;
    fdn_string *replacement;
    if (required <= *capacity) {
        return 1;
    }
    if (required > UINT16_MAX || required > SIZE_MAX / sizeof(**values)) {
        return 0;
    }
    next = *capacity == 0 ? 8 : *capacity;
    while (next < required) {
        if (next > UINT16_MAX / 2) {
            next = UINT16_MAX;
            break;
        }
        next *= 2;
    }
    replacement = fdn_alloc(next * sizeof(*replacement));
    if (*values != NULL) {
        (void)memcpy(replacement, *values, used * sizeof(*replacement));
        fdn_dealloc(*values);
    }
    *values = replacement;
    *capacity = next;
    return 1;
}

int32_t foundation_runtime_process_open(const fdn_string *program,
                                        uint64_t output_limit,
                                        bool inherit_environment,
                                        uint64_t *handle) {
    fdn_process *process;
    if (handle == NULL) {
        fdn_panic_cstr("process handle output is null");
    }
    *handle = 0;
    if (!fdn_process_text_valid(program, false)) {
        return FDN_PROCESS_INVALID_ARGUMENT;
    }
    if (output_limit > (uint64_t)SIZE_MAX) {
        return FDN_PROCESS_OUTPUT_LIMIT;
    }
    process = fdn_alloc(sizeof(*process));
    process->program = foundation_runtime_string_copy(program);
    process->working_directory = fdn_string_static("", 0);
    process->arguments = NULL;
    process->argument_count = 0;
    process->argument_capacity = 0;
    process->environment = NULL;
    process->environment_count = 0;
    process->environment_capacity = 0;
    process->output_limit = (size_t)output_limit;
    process->has_working_directory = false;
    process->inherit_environment = inherit_environment;
    *handle = (uint64_t)(uintptr_t)process;
    fdn_process_count_add();
    return 0;
}

int32_t foundation_runtime_process_add_argument(uint64_t handle,
                                                const fdn_string *argument) {
    fdn_process *process = (fdn_process *)(uintptr_t)handle;
    if (process == NULL) {
        return FDN_PROCESS_CLOSED;
    }
    if (!fdn_process_text_valid(argument, true)) {
        return FDN_PROCESS_INVALID_ARGUMENT;
    }
    if (!fdn_process_reserve(&process->arguments, process->argument_count,
                             &process->argument_capacity,
                             process->argument_count + 1)) {
        return FDN_PROCESS_RESOURCE_LIMIT;
    }
    process->arguments[process->argument_count++] =
        foundation_runtime_string_copy(argument);
    return 0;
}

int32_t foundation_runtime_process_set_working_directory(
    uint64_t handle, const fdn_string *working_directory) {
    fdn_process *process = (fdn_process *)(uintptr_t)handle;
    if (process == NULL) {
        return FDN_PROCESS_CLOSED;
    }
    if (!fdn_process_text_valid(working_directory, false)) {
        return FDN_PROCESS_INVALID_WORKING_DIRECTORY;
    }
    fdn_string_drop(&process->working_directory);
    process->working_directory = foundation_runtime_string_copy(working_directory);
    process->has_working_directory = true;
    return 0;
}

int32_t foundation_runtime_process_add_environment(uint64_t handle,
                                                   const fdn_string *entry) {
    fdn_process *process = (fdn_process *)(uintptr_t)handle;
    size_t index;
    if (process == NULL) {
        return FDN_PROCESS_CLOSED;
    }
    if (process->inherit_environment || !fdn_process_environment_valid(entry)) {
        return FDN_PROCESS_INVALID_ENVIRONMENT;
    }
    for (index = 0; index < process->environment_count; ++index) {
        if (fdn_process_environment_name_equal(&process->environment[index], entry)) {
            return FDN_PROCESS_INVALID_ENVIRONMENT;
        }
    }
    if (!fdn_process_reserve(&process->environment, process->environment_count,
                             &process->environment_capacity,
                             process->environment_count + 1)) {
        return FDN_PROCESS_RESOURCE_LIMIT;
    }
    process->environment[process->environment_count++] =
        foundation_runtime_string_copy(entry);
    return 0;
}

void foundation_runtime_process_close(uint64_t *handle) {
    fdn_process *process;
    size_t index;
    if (handle == NULL || *handle == 0) {
        return;
    }
    process = (fdn_process *)(uintptr_t)*handle;
    *handle = 0;
    fdn_string_drop(&process->program);
    fdn_string_drop(&process->working_directory);
    for (index = 0; index < process->argument_count; ++index) {
        fdn_string_drop(&process->arguments[index]);
    }
    for (index = 0; index < process->environment_count; ++index) {
        fdn_string_drop(&process->environment[index]);
    }
    fdn_dealloc(process->arguments);
    fdn_dealloc(process->environment);
    fdn_dealloc(process);
    fdn_process_count_remove();
}

static int fdn_process_capture_append(fdn_process_capture *capture,
                                      const uint8_t *data, size_t length) {
    size_t required;
    size_t next;
    uint8_t *replacement;
    if (capture->exceeded) {
        return 1;
    }
    if (length > capture->limit - capture->length) {
        capture->exceeded = true;
        return 1;
    }
    required = capture->length + length;
    if (required <= capture->capacity) {
        if (length != 0) {
            (void)memcpy(capture->data + capture->length, data, length);
        }
        capture->length = required;
        return 1;
    }
    next = capture->capacity == 0 ? 4096 : capture->capacity;
    if (next > capture->limit) {
        next = capture->limit;
    }
    while (next < required) {
        next = next > capture->limit / 2 ? capture->limit : next * 2;
    }
    replacement = fdn_alloc(next);
    if (capture->length != 0) {
        (void)memcpy(replacement, capture->data, capture->length);
    }
    fdn_dealloc(capture->data);
    capture->data = replacement;
    capture->capacity = next;
    if (length != 0) {
        (void)memcpy(capture->data + capture->length, data, length);
    }
    capture->length = required;
    return 1;
}

static int32_t fdn_process_finish_capture(fdn_process_capture *capture,
                                          uint64_t *handle) {
    if (fdn_bytes_adopt(capture->data, capture->length, capture->capacity,
                        handle) != 0) {
        fdn_dealloc(capture->data);
        capture->data = NULL;
        return FDN_PROCESS_IO;
    }
    capture->data = NULL;
    capture->length = 0;
    capture->capacity = 0;
    return 0;
}

#if defined(_WIN32)
static int32_t fdn_process_windows_status(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return FDN_PROCESS_NOT_FOUND;
    }
    if (error == ERROR_ACCESS_DENIED || error == ERROR_ELEVATION_REQUIRED) {
        return FDN_PROCESS_PERMISSION;
    }
    if (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY ||
        error == ERROR_NO_SYSTEM_RESOURCES || error == ERROR_TOO_MANY_OPEN_FILES) {
        return FDN_PROCESS_RESOURCE_LIMIT;
    }
    return FDN_PROCESS_IO;
}

static wchar_t *fdn_process_windows_text(const fdn_string *value) {
    wchar_t *result;
    int length;
    if (!fdn_process_text_valid(value, true) || value->length > (size_t)INT_MAX) {
        return NULL;
    }
    if (value->length == 0) {
        result = fdn_alloc(sizeof(*result));
        result[0] = L'\0';
        return result;
    }
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value->data,
                                 (int)value->length, NULL, 0);
    if (length == 0) {
        return NULL;
    }
    result = fdn_alloc(((size_t)length + 1) * sizeof(*result));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value->data,
                            (int)value->length, result, length) != length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[length] = L'\0';
    return result;
}

typedef struct fdn_wide_builder {
    wchar_t *data;
    size_t length;
    size_t capacity;
} fdn_wide_builder;

static int fdn_wide_reserve(fdn_wide_builder *builder, size_t extra) {
    size_t required;
    size_t next;
    wchar_t *replacement;
    if (extra > SIZE_MAX - builder->length) {
        return 0;
    }
    required = builder->length + extra;
    if (required <= builder->capacity) {
        return 1;
    }
    next = builder->capacity == 0 ? 64 : builder->capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            return 0;
        }
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(*replacement)) {
        return 0;
    }
    replacement = fdn_alloc(next * sizeof(*replacement));
    if (builder->length != 0) {
        (void)memcpy(replacement, builder->data,
                     builder->length * sizeof(*replacement));
    }
    fdn_dealloc(builder->data);
    builder->data = replacement;
    builder->capacity = next;
    return 1;
}

static int fdn_wide_append(fdn_wide_builder *builder, wchar_t value) {
    if (!fdn_wide_reserve(builder, 1)) {
        return 0;
    }
    builder->data[builder->length++] = value;
    return 1;
}

static int fdn_wide_argument(fdn_wide_builder *builder, const wchar_t *value) {
    const size_t length = wcslen(value);
    size_t index;
    int quote = length == 0;
    for (index = 0; index < length; ++index) {
        if (value[index] == L' ' || value[index] == L'\t' ||
            value[index] == L'"') {
            quote = 1;
            break;
        }
    }
    if (!quote) {
        if (!fdn_wide_reserve(builder, length)) {
            return 0;
        }
        (void)memcpy(builder->data + builder->length, value,
                     length * sizeof(*value));
        builder->length += length;
        return 1;
    }
    if (!fdn_wide_append(builder, L'"')) {
        return 0;
    }
    index = 0;
    while (index < length) {
        size_t slashes = 0;
        while (index < length && value[index] == L'\\') {
            ++slashes;
            ++index;
        }
        if (index == length) {
            while (slashes-- != 0) {
                if (!fdn_wide_append(builder, L'\\') ||
                    !fdn_wide_append(builder, L'\\')) {
                    return 0;
                }
            }
            break;
        }
        if (value[index] == L'"') {
            while (slashes-- != 0) {
                if (!fdn_wide_append(builder, L'\\') ||
                    !fdn_wide_append(builder, L'\\')) {
                    return 0;
                }
            }
            if (!fdn_wide_append(builder, L'\\') ||
                !fdn_wide_append(builder, L'"')) {
                return 0;
            }
        } else {
            while (slashes-- != 0) {
                if (!fdn_wide_append(builder, L'\\')) {
                    return 0;
                }
            }
            if (!fdn_wide_append(builder, value[index])) {
                return 0;
            }
        }
        ++index;
    }
    return fdn_wide_append(builder, L'"');
}

static wchar_t *fdn_process_windows_command(const fdn_process *process) {
    fdn_wide_builder builder = {NULL, 0, 0};
    wchar_t *value = fdn_process_windows_text(&process->program);
    size_t index;
    if (value == NULL || !fdn_wide_argument(&builder, value)) {
        fdn_dealloc(value);
        fdn_dealloc(builder.data);
        return NULL;
    }
    fdn_dealloc(value);
    for (index = 0; index < process->argument_count; ++index) {
        value = fdn_process_windows_text(&process->arguments[index]);
        if (value == NULL || !fdn_wide_append(&builder, L' ') ||
            !fdn_wide_argument(&builder, value)) {
            fdn_dealloc(value);
            fdn_dealloc(builder.data);
            return NULL;
        }
        fdn_dealloc(value);
    }
    if (!fdn_wide_append(&builder, L'\0')) {
        fdn_dealloc(builder.data);
        return NULL;
    }
    return builder.data;
}

static int fdn_wide_compare(const void *left, const void *right) {
    const wchar_t *const *left_value = left;
    const wchar_t *const *right_value = right;
    return _wcsicmp(*left_value, *right_value);
}

static wchar_t *fdn_process_windows_environment(const fdn_process *process) {
    wchar_t **entries;
    wchar_t *block;
    size_t total = process->environment_count == 0 ? 2 : 1;
    size_t offset = 0;
    size_t index;
    if (process->inherit_environment) {
        return NULL;
    }
    entries = process->environment_count == 0
                  ? NULL
                  : fdn_alloc(process->environment_count * sizeof(*entries));
    for (index = 0; index < process->environment_count; ++index) {
        entries[index] = fdn_process_windows_text(&process->environment[index]);
        if (entries[index] == NULL ||
            wcslen(entries[index]) > SIZE_MAX - total - 1) {
            size_t clean;
            for (clean = 0; clean <= index; ++clean) {
                fdn_dealloc(entries[clean]);
            }
            fdn_dealloc(entries);
            return NULL;
        }
        total += wcslen(entries[index]) + 1;
    }
    if (process->environment_count > 1) {
        qsort(entries, process->environment_count, sizeof(*entries),
              fdn_wide_compare);
    }
    block = fdn_alloc(total * sizeof(*block));
    for (index = 0; index < process->environment_count; ++index) {
        const size_t length = wcslen(entries[index]);
        (void)memcpy(block + offset, entries[index], length * sizeof(*block));
        offset += length;
        block[offset++] = L'\0';
        fdn_dealloc(entries[index]);
    }
    block[offset++] = L'\0';
    if (process->environment_count == 0) {
        block[offset] = L'\0';
    }
    fdn_dealloc(entries);
    return block;
}

static int32_t fdn_process_windows_cwd(const fdn_process *process,
                                       wchar_t **working_directory) {
    DWORD attributes;
    *working_directory = NULL;
    if (!process->has_working_directory) {
        return 0;
    }
    *working_directory = fdn_process_windows_text(&process->working_directory);
    if (*working_directory == NULL) {
        return FDN_PROCESS_INVALID_WORKING_DIRECTORY;
    }
    attributes = GetFileAttributesW(*working_directory);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        fdn_dealloc(*working_directory);
        *working_directory = NULL;
        return FDN_PROCESS_INVALID_WORKING_DIRECTORY;
    }
    return 0;
}

typedef struct fdn_windows_capture {
    HANDLE pipe;
    fdn_process_capture *capture;
} fdn_windows_capture;

static DWORD WINAPI fdn_process_windows_reader(void *context) {
    fdn_windows_capture *reader = context;
    uint8_t buffer[4096];
    for (;;) {
        DWORD count = 0;
        if (ReadFile(reader->pipe, buffer, sizeof(buffer), &count, NULL) == 0) {
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                reader->capture->failed = true;
            }
            break;
        }
        if (count == 0) {
            break;
        }
        (void)fdn_process_capture_append(reader->capture, buffer, (size_t)count);
    }
    return 0;
}

static int32_t fdn_process_run_platform(const fdn_process *process,
                                        int32_t *exit_code,
                                        uint64_t *stdout_handle,
                                        uint64_t *stderr_handle) {
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION information;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    SIZE_T attributes_size = 0;
    HANDLE inherited[3];
    HANDLE stdin_handle = NULL;
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE stderr_read = NULL;
    HANDLE stderr_write = NULL;
    HANDLE stdout_thread = NULL;
    HANDLE stderr_thread = NULL;
    fdn_process_capture stdout_capture = {NULL, 0, 0, process->output_limit, false, false};
    fdn_process_capture stderr_capture = {NULL, 0, 0, process->output_limit, false, false};
    fdn_windows_capture stdout_context = {NULL, &stdout_capture};
    fdn_windows_capture stderr_context = {NULL, &stderr_capture};
    wchar_t *program = fdn_process_windows_text(&process->program);
    wchar_t *command = fdn_process_windows_command(process);
    wchar_t *environment = fdn_process_windows_environment(process);
    wchar_t *working_directory = NULL;
    DWORD child_status = 0;
    int32_t status = fdn_process_windows_cwd(process, &working_directory);
    BOOL created = FALSE;
    BOOL attributes_initialized = FALSE;
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&information, 0, sizeof(information));
    startup.StartupInfo.cb = sizeof(startup);
    if (status != 0) {
        goto cleanup;
    }
    if (program == NULL || command == NULL ||
        (!process->inherit_environment && environment == NULL)) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (CreatePipe(&stdout_read, &stdout_write, &security, 0) == 0 ||
        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) == 0 ||
        CreatePipe(&stderr_read, &stderr_write, &security, 0) == 0 ||
        SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0) == 0) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    {
        const HANDLE current_input = GetStdHandle(STD_INPUT_HANDLE);
        if (current_input != NULL && current_input != INVALID_HANDLE_VALUE) {
            (void)DuplicateHandle(GetCurrentProcess(), current_input,
                                  GetCurrentProcess(), &stdin_handle, 0, TRUE,
                                  DUPLICATE_SAME_ACCESS);
        }
        if (stdin_handle == NULL) {
            stdin_handle = CreateFileW(L"NUL", GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (stdin_handle == INVALID_HANDLE_VALUE) {
            stdin_handle = NULL;
            status = FDN_PROCESS_IO;
            goto cleanup;
        }
    }
    inherited[0] = stdin_handle;
    inherited[1] = stdout_write;
    inherited[2] = stderr_write;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
    if (attributes_size == 0) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    attributes = fdn_alloc(attributes_size);
    if (InitializeProcThreadAttributeList(attributes, 1, 0,
                                          &attributes_size) == 0) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    attributes_initialized = TRUE;
    if (UpdateProcThreadAttribute(
            attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), NULL, NULL) == 0) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    startup.lpAttributeList = attributes;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = stdin_handle;
    startup.StartupInfo.hStdOutput = stdout_write;
    startup.StartupInfo.hStdError = stderr_write;
    created = CreateProcessW(program, command, NULL, NULL, TRUE,
                             CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW |
                                 EXTENDED_STARTUPINFO_PRESENT,
                             process->inherit_environment ? NULL : environment,
                             working_directory, &startup.StartupInfo,
                             &information);
    if (!created) {
        status = fdn_process_windows_status(GetLastError());
        goto cleanup;
    }
    (void)CloseHandle(stdout_write);
    stdout_write = NULL;
    (void)CloseHandle(stderr_write);
    stderr_write = NULL;
    stdout_context.pipe = stdout_read;
    stderr_context.pipe = stderr_read;
    stdout_thread = CreateThread(NULL, 0, fdn_process_windows_reader,
                                 &stdout_context, 0, NULL);
    stderr_thread = CreateThread(NULL, 0, fdn_process_windows_reader,
                                 &stderr_context, 0, NULL);
    if (stdout_thread == NULL || stderr_thread == NULL) {
        (void)TerminateProcess(information.hProcess, 1);
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (WaitForSingleObject(information.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        WaitForSingleObject(stdout_thread, INFINITE) != WAIT_OBJECT_0 ||
        WaitForSingleObject(stderr_thread, INFINITE) != WAIT_OBJECT_0 ||
        GetExitCodeProcess(information.hProcess, &child_status) == 0) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    if (stdout_capture.failed || stderr_capture.failed) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    if (stdout_capture.exceeded || stderr_capture.exceeded) {
        status = FDN_PROCESS_OUTPUT_LIMIT;
        goto cleanup;
    }
    *exit_code = (int32_t)child_status;
    status = fdn_process_finish_capture(&stdout_capture, stdout_handle);
    if (status == 0) {
        status = fdn_process_finish_capture(&stderr_capture, stderr_handle);
        if (status != 0) {
            foundation_runtime_bytes_close(stdout_handle);
        }
    }

cleanup:
    if (created && information.hProcess != NULL && status != 0) {
        (void)WaitForSingleObject(information.hProcess, INFINITE);
    }
    if (stdout_thread != NULL) {
        (void)WaitForSingleObject(stdout_thread, INFINITE);
        (void)CloseHandle(stdout_thread);
    }
    if (stderr_thread != NULL) {
        (void)WaitForSingleObject(stderr_thread, INFINITE);
        (void)CloseHandle(stderr_thread);
    }
    if (stdout_read != NULL) {
        (void)CloseHandle(stdout_read);
    }
    if (stdout_write != NULL) {
        (void)CloseHandle(stdout_write);
    }
    if (stderr_read != NULL) {
        (void)CloseHandle(stderr_read);
    }
    if (stderr_write != NULL) {
        (void)CloseHandle(stderr_write);
    }
    if (stdin_handle != NULL) {
        (void)CloseHandle(stdin_handle);
    }
    if (information.hThread != NULL) {
        (void)CloseHandle(information.hThread);
    }
    if (information.hProcess != NULL) {
        (void)CloseHandle(information.hProcess);
    }
    if (attributes_initialized) {
        DeleteProcThreadAttributeList(attributes);
    }
    if (attributes != NULL) {
        fdn_dealloc(attributes);
    }
    fdn_dealloc(program);
    fdn_dealloc(command);
    fdn_dealloc(environment);
    fdn_dealloc(working_directory);
    if (status != 0) {
        fdn_dealloc(stdout_capture.data);
        fdn_dealloc(stderr_capture.data);
    }
    return status;
}
#else
static int32_t fdn_process_posix_status(int error) {
    if (error == ENOENT) {
        return FDN_PROCESS_NOT_FOUND;
    }
    if (error == EACCES || error == EPERM || error == ENOEXEC) {
        return FDN_PROCESS_PERMISSION;
    }
    if (error == E2BIG || error == EMFILE || error == ENFILE || error == ENOMEM ||
        error == EAGAIN) {
        return FDN_PROCESS_RESOURCE_LIMIT;
    }
    return FDN_PROCESS_IO;
}

static char *fdn_process_posix_text(const fdn_string *value) {
    char *result;
    if (!fdn_process_text_valid(value, true) || value->length == SIZE_MAX) {
        return NULL;
    }
    result = fdn_alloc(value->length + 1);
    if (value->length != 0) {
        (void)memcpy(result, value->data, value->length);
    }
    result[value->length] = '\0';
    return result;
}

static void fdn_process_posix_strings_close(char **values, size_t count) {
    size_t index;
    if (values == NULL) {
        return;
    }
    for (index = 0; index < count; ++index) {
        fdn_dealloc(values[index]);
    }
    fdn_dealloc(values);
}

static char **fdn_process_posix_argv(const fdn_process *process) {
    char **values = fdn_alloc((process->argument_count + 2) * sizeof(*values));
    size_t index;
    values[0] = fdn_process_posix_text(&process->program);
    if (values[0] == NULL) {
        fdn_dealloc(values);
        return NULL;
    }
    for (index = 0; index < process->argument_count; ++index) {
        values[index + 1] = fdn_process_posix_text(&process->arguments[index]);
        if (values[index + 1] == NULL) {
            fdn_process_posix_strings_close(values, index + 1);
            return NULL;
        }
    }
    values[process->argument_count + 1] = NULL;
    return values;
}

static char **fdn_process_posix_environment(const fdn_process *process) {
    char **values;
    size_t index;
    if (process->inherit_environment) {
        return environ;
    }
    values = fdn_alloc((process->environment_count + 1) * sizeof(*values));
    for (index = 0; index < process->environment_count; ++index) {
        values[index] = fdn_process_posix_text(&process->environment[index]);
        if (values[index] == NULL) {
            fdn_process_posix_strings_close(values, index);
            return NULL;
        }
    }
    values[process->environment_count] = NULL;
    return values;
}

static int32_t fdn_process_posix_cwd(const fdn_process *process,
                                     char **working_directory) {
    struct stat info;
    *working_directory = NULL;
    if (!process->has_working_directory) {
        return 0;
    }
    *working_directory = fdn_process_posix_text(&process->working_directory);
    if (*working_directory == NULL || stat(*working_directory, &info) != 0 ||
        !S_ISDIR(info.st_mode)) {
        fdn_dealloc(*working_directory);
        *working_directory = NULL;
        return FDN_PROCESS_INVALID_WORKING_DIRECTORY;
    }
    return 0;
}

static int fdn_process_pipe(int descriptors[2]) {
    if (pipe(descriptors) != 0) {
        return 0;
    }
    if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        descriptors[0] = -1;
        descriptors[1] = -1;
        return 0;
    }
    return 1;
}

static int fdn_process_posix_read(int descriptor,
                                  fdn_process_capture *capture) {
    uint8_t buffer[4096];
    for (;;) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            (void)fdn_process_capture_append(capture, buffer, (size_t)count);
            continue;
        }
        if (count == 0) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        capture->failed = true;
        return 0;
    }
}

static int32_t fdn_process_run_platform(const fdn_process *process,
                                        int32_t *exit_code,
                                        uint64_t *stdout_handle,
                                        uint64_t *stderr_handle) {
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    int actions_ready = 0;
    char **arguments = NULL;
    char **environment = NULL;
    char *working_directory = NULL;
    pid_t child = 0;
    int child_status = 0;
    fdn_process_capture stdout_capture = {NULL, 0, 0, process->output_limit, false, false};
    fdn_process_capture stderr_capture = {NULL, 0, 0, process->output_limit, false, false};
    int32_t status = fdn_process_posix_cwd(process, &working_directory);
    int spawned = 0;
    if (status != 0) {
        goto cleanup;
    }
    arguments = fdn_process_posix_argv(process);
    environment = fdn_process_posix_environment(process);
    if (arguments == NULL || environment == NULL) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (!fdn_process_pipe(stdout_pipe) || !fdn_process_pipe(stderr_pipe)) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    actions_ready = 1;
    if (posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]) != 0 ||
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]) != 0 ||
        (working_directory != NULL &&
         posix_spawn_file_actions_addchdir_np(&actions, working_directory) != 0)) {
        status = FDN_PROCESS_RESOURCE_LIMIT;
        goto cleanup;
    }
    status = posix_spawnp(&child, arguments[0], &actions, NULL, arguments,
                          environment);
    if (status != 0) {
        status = fdn_process_posix_status(status);
        goto cleanup;
    }
    spawned = 1;
    (void)close(stdout_pipe[1]);
    stdout_pipe[1] = -1;
    (void)close(stderr_pipe[1]);
    stderr_pipe[1] = -1;
    if (fcntl(stdout_pipe[0], F_SETFL,
              fcntl(stdout_pipe[0], F_GETFL, 0) | O_NONBLOCK) < 0 ||
        fcntl(stderr_pipe[0], F_SETFL,
              fcntl(stderr_pipe[0], F_GETFL, 0) | O_NONBLOCK) < 0) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    while (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
        struct pollfd descriptors[2];
        nfds_t count = 0;
        int polled;
        if (stdout_pipe[0] >= 0) {
            descriptors[count].fd = stdout_pipe[0];
            descriptors[count].events = POLLIN | POLLHUP;
            descriptors[count].revents = 0;
            ++count;
        }
        if (stderr_pipe[0] >= 0) {
            descriptors[count].fd = stderr_pipe[0];
            descriptors[count].events = POLLIN | POLLHUP;
            descriptors[count].revents = 0;
            ++count;
        }
        polled = poll(descriptors, count, -1);
        if (polled < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = FDN_PROCESS_IO;
            goto cleanup;
        }
        for (count = 0; count < 2; ++count) {
            const int descriptor = count == 0 ? stdout_pipe[0] : stderr_pipe[0];
            fdn_process_capture *capture = count == 0 ? &stdout_capture
                                                       : &stderr_capture;
            size_t poll_index;
            if (descriptor < 0) {
                continue;
            }
            poll_index = descriptors[0].fd == descriptor ? 0 : 1;
            if ((descriptors[poll_index].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            if (!fdn_process_posix_read(descriptor, capture)) {
                (void)close(descriptor);
                if (count == 0) {
                    stdout_pipe[0] = -1;
                } else {
                    stderr_pipe[0] = -1;
                }
            }
        }
    }
    while (waitpid(child, &child_status, 0) < 0) {
        if (errno != EINTR) {
            status = FDN_PROCESS_IO;
            goto cleanup;
        }
    }
    spawned = 0;
    if (stdout_capture.failed || stderr_capture.failed) {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    if (stdout_capture.exceeded || stderr_capture.exceeded) {
        status = FDN_PROCESS_OUTPUT_LIMIT;
        goto cleanup;
    }
    if (WIFEXITED(child_status)) {
        *exit_code = WEXITSTATUS(child_status);
    } else if (WIFSIGNALED(child_status)) {
        *exit_code = 128 + WTERMSIG(child_status);
    } else {
        status = FDN_PROCESS_IO;
        goto cleanup;
    }
    status = fdn_process_finish_capture(&stdout_capture, stdout_handle);
    if (status == 0) {
        status = fdn_process_finish_capture(&stderr_capture, stderr_handle);
        if (status != 0) {
            foundation_runtime_bytes_close(stdout_handle);
        }
    }

cleanup:
    if (stdout_pipe[0] >= 0) {
        (void)close(stdout_pipe[0]);
    }
    if (stdout_pipe[1] >= 0) {
        (void)close(stdout_pipe[1]);
    }
    if (stderr_pipe[0] >= 0) {
        (void)close(stderr_pipe[0]);
    }
    if (stderr_pipe[1] >= 0) {
        (void)close(stderr_pipe[1]);
    }
    if (spawned) {
        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
        }
    }
    if (actions_ready) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    fdn_process_posix_strings_close(arguments,
                                    process->argument_count + 1);
    if (!process->inherit_environment) {
        fdn_process_posix_strings_close(environment,
                                        process->environment_count);
    }
    fdn_dealloc(working_directory);
    if (status != 0) {
        fdn_dealloc(stdout_capture.data);
        fdn_dealloc(stderr_capture.data);
    }
    return status;
}
#endif

int32_t foundation_runtime_process_run(uint64_t handle, int32_t *exit_code,
                                       uint64_t *stdout_handle,
                                       uint64_t *stderr_handle) {
    const fdn_process *process = (const fdn_process *)(uintptr_t)handle;
    if (exit_code == NULL || stdout_handle == NULL || stderr_handle == NULL) {
        fdn_panic_cstr("process result output is null");
    }
    *exit_code = 0;
    *stdout_handle = 0;
    *stderr_handle = 0;
    if (process == NULL) {
        return FDN_PROCESS_CLOSED;
    }
    return fdn_process_run_platform(process, exit_code, stdout_handle,
                                    stderr_handle);
}
