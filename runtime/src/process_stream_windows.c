#if defined(_WIN32)
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "bytes_internal.h"
#include "foundation/process_internal.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    FDN_STREAM_NOT_FOUND = 1,
    FDN_STREAM_PERMISSION = 2,
    FDN_STREAM_INVALID_ARGUMENT = 3,
    FDN_STREAM_OUTPUT_LIMIT = 6,
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fdn_stream_process {
    HANDLE input;
    HANDLE output;
    HANDLE error;
    HANDLE process;
    size_t references;
    bool waited;
    int32_t exit_code;
    CRITICAL_SECTION lock;
} fdn_stream_process;

typedef struct fdn_stream_handle {
    fdn_stream_process* process;
    uint8_t kind;
    uint8_t buffer[8192];
    size_t buffer_offset;
    size_t buffer_length;
} fdn_stream_handle;

static volatile LONG64 fdn_live_stream_count;

static void fdn_stream_count_add(void) {
    if (InterlockedIncrement64(&fdn_live_stream_count) <= 0) {
        fdn_panic_cstr("process stream count overflow");
    }
}

static void fdn_stream_count_remove(void) {
    if (InterlockedDecrement64(&fdn_live_stream_count) < 0) {
        fdn_panic_cstr("process stream count underflow");
    }
}

uint64_t foundation_runtime_process_stream_live_handles(void) {
    return (uint64_t)InterlockedCompareExchange64(&fdn_live_stream_count, 0, 0);
}

static void fdn_stream_enter(fdn_stream_process* process) { EnterCriticalSection(&process->lock); }

static void fdn_stream_leave(fdn_stream_process* process) { LeaveCriticalSection(&process->lock); }

static void fdn_stream_abort_process(fdn_stream_process* process) {
    fdn_stream_enter(process);
    if (!process->waited) {
        (void)TerminateProcess(process->process, 1);
    }
    if (process->input != NULL) {
        (void)CancelIoEx(process->input, NULL);
    }
    if (process->output != NULL) {
        (void)CancelIoEx(process->output, NULL);
    }
    if (process->error != NULL) {
        (void)CancelIoEx(process->error, NULL);
    }
    fdn_stream_leave(process);
}

static int32_t fdn_stream_wait_process(fdn_stream_process* process, int32_t* exit_code) {
    DWORD code = 0;
    fdn_stream_enter(process);
    if (process->waited) {
        *exit_code = process->exit_code;
        fdn_stream_leave(process);
        return 0;
    }
    fdn_stream_leave(process);
    if (WaitForSingleObject(process->process, INFINITE) != WAIT_OBJECT_0 ||
        GetExitCodeProcess(process->process, &code) == 0) {
        return FDN_STREAM_IO;
    }
    *exit_code = (int32_t)code;
    fdn_stream_enter(process);
    process->waited = true;
    process->exit_code = *exit_code;
    fdn_stream_leave(process);
    return 0;
}

static void fdn_stream_close_endpoint(fdn_stream_process* process, uint8_t kind) {
    HANDLE* endpoint = NULL;
    if (kind == FDN_STREAM_INPUT) {
        endpoint = &process->input;
    } else if (kind == FDN_STREAM_OUTPUT) {
        endpoint = &process->output;
    } else if (kind == FDN_STREAM_ERROR) {
        endpoint = &process->error;
    }
    if (endpoint != NULL && *endpoint != NULL) {
        (void)CloseHandle(*endpoint);
        *endpoint = NULL;
    }
}

static void fdn_stream_destroy(fdn_stream_process* process) {
    int32_t ignored = 0;
    fdn_stream_abort_process(process);
    (void)fdn_stream_wait_process(process, &ignored);
    fdn_stream_close_endpoint(process, FDN_STREAM_INPUT);
    fdn_stream_close_endpoint(process, FDN_STREAM_OUTPUT);
    fdn_stream_close_endpoint(process, FDN_STREAM_ERROR);
    (void)CloseHandle(process->process);
    DeleteCriticalSection(&process->lock);
    fdn_dealloc(process);
    fdn_stream_count_remove();
}

static uint64_t fdn_stream_new_handle(fdn_stream_process* process, uint8_t kind) {
    fdn_stream_handle* handle = fdn_alloc(sizeof(*handle));
    handle->process = process;
    handle->kind = kind;
    handle->buffer_offset = 0;
    handle->buffer_length = 0;
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

int32_t foundation_runtime_process_stream_start(uint64_t process_handle, uint64_t* input,
                                                uint64_t* output, uint64_t* error,
                                                uint64_t* controller, uint64_t* waiter) {
    const fdn_process* configuration = fdn_process_from_handle(process_handle);
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION information = {0};
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    SIZE_T attributes_size = 0;
    HANDLE inherited[3];
    HANDLE input_read = NULL;
    HANDLE input_write = NULL;
    HANDLE output_read = NULL;
    HANDLE output_write = NULL;
    HANDLE error_read = NULL;
    HANDLE error_write = NULL;
    wchar_t* program = NULL;
    wchar_t* command = NULL;
    wchar_t* environment = NULL;
    wchar_t* working_directory = NULL;
    fdn_stream_process* process = NULL;
    int32_t status = 0;
    BOOL attributes_ready = FALSE;
    BOOL created = FALSE;
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
    program = fdn_process_windows_program(configuration);
    command = fdn_process_windows_command(configuration);
    environment = fdn_process_windows_environment(configuration);
    status = fdn_process_windows_cwd(configuration, &working_directory);
    if (status != 0) {
        goto cleanup;
    }
    if (program == NULL || command == NULL ||
        (fdn_process_environment_block_required(configuration) && environment == NULL)) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (CreatePipe(&input_read, &input_write, &security, 0) == 0 ||
        SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0) == 0 ||
        CreatePipe(&output_read, &output_write, &security, 0) == 0 ||
        SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0) == 0 ||
        CreatePipe(&error_read, &error_write, &security, 0) == 0 ||
        SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0) == 0) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    inherited[0] = input_read;
    inherited[1] = output_write;
    inherited[2] = error_write;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
    if (attributes_size == 0) {
        status = FDN_STREAM_RESOURCE_LIMIT;
        goto cleanup;
    }
    attributes = fdn_alloc(attributes_size);
    if (InitializeProcThreadAttributeList(attributes, 1, 0, &attributes_size) == 0) {
        status = FDN_STREAM_IO;
        goto cleanup;
    }
    attributes_ready = TRUE;
    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                  sizeof(inherited), NULL, NULL) == 0) {
        status = FDN_STREAM_IO;
        goto cleanup;
    }
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&information, 0, sizeof(information));
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input_read;
    startup.StartupInfo.hStdOutput = output_write;
    startup.StartupInfo.hStdError = error_write;
    startup.lpAttributeList = attributes;
    created =
        CreateProcessW(program, command, NULL, NULL, TRUE,
                       CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                       fdn_process_environment_block_required(configuration) ? environment : NULL,
                       working_directory, &startup.StartupInfo, &information);
    if (!created) {
        status = fdn_process_windows_status(GetLastError());
        goto cleanup;
    }
    (void)CloseHandle(input_read);
    input_read = NULL;
    (void)CloseHandle(output_write);
    output_write = NULL;
    (void)CloseHandle(error_write);
    error_write = NULL;
    (void)CloseHandle(information.hThread);
    information.hThread = NULL;
    process = fdn_alloc(sizeof(*process));
    process->input = input_write;
    process->output = output_read;
    process->error = error_read;
    process->process = information.hProcess;
    process->references = 5;
    process->waited = false;
    process->exit_code = 0;
    InitializeCriticalSection(&process->lock);
    input_write = NULL;
    output_read = NULL;
    error_read = NULL;
    information.hProcess = NULL;
    *input = fdn_stream_new_handle(process, FDN_STREAM_INPUT);
    *output = fdn_stream_new_handle(process, FDN_STREAM_OUTPUT);
    *error = fdn_stream_new_handle(process, FDN_STREAM_ERROR);
    *controller = fdn_stream_new_handle(process, FDN_STREAM_CONTROLLER);
    *waiter = fdn_stream_new_handle(process, FDN_STREAM_WAITER);
    fdn_stream_count_add();
    status = 0;

cleanup:
    if (created && information.hProcess != NULL) {
        (void)TerminateProcess(information.hProcess, 1);
        (void)WaitForSingleObject(information.hProcess, INFINITE);
    }
    if (information.hThread != NULL) {
        (void)CloseHandle(information.hThread);
    }
    if (information.hProcess != NULL) {
        (void)CloseHandle(information.hProcess);
    }
    if (input_read != NULL) {
        (void)CloseHandle(input_read);
    }
    if (input_write != NULL) {
        (void)CloseHandle(input_write);
    }
    if (output_read != NULL) {
        (void)CloseHandle(output_read);
    }
    if (output_write != NULL) {
        (void)CloseHandle(output_write);
    }
    if (error_read != NULL) {
        (void)CloseHandle(error_read);
    }
    if (error_write != NULL) {
        (void)CloseHandle(error_write);
    }
    if (attributes_ready) {
        DeleteProcThreadAttributeList(attributes);
    }
    fdn_dealloc(attributes);
    fdn_dealloc(program);
    fdn_dealloc(command);
    fdn_dealloc(environment);
    fdn_dealloc(working_directory);
    return status;
}

int32_t foundation_runtime_process_stream_read(uint64_t value, uint64_t limit, uint64_t* result) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    HANDLE endpoint;
    uint8_t* data;
    DWORD count = 0;
    if (result == NULL) {
        fdn_panic_cstr("process stream read output is null");
    }
    *result = 0;
    if (handle == NULL || (handle->kind != FDN_STREAM_OUTPUT && handle->kind != FDN_STREAM_ERROR)) {
        return FDN_STREAM_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > UINT32_MAX) {
        return FDN_STREAM_INVALID_ARGUMENT;
    }
    endpoint = handle->kind == FDN_STREAM_OUTPUT ? handle->process->output : handle->process->error;
    if (endpoint == NULL) {
        return FDN_STREAM_CLOSED;
    }
    data = fdn_alloc((size_t)limit);
    if (handle->buffer_offset < handle->buffer_length) {
        const size_t available = handle->buffer_length - handle->buffer_offset;
        const size_t selected = available < (size_t)limit ? available : (size_t)limit;
        (void)memcpy(data, handle->buffer + handle->buffer_offset, selected);
        handle->buffer_offset += selected;
        if (handle->buffer_offset == handle->buffer_length) {
            handle->buffer_offset = 0;
            handle->buffer_length = 0;
        }
        if (fdn_bytes_adopt(data, selected, (size_t)limit, result) != 0) {
            fdn_dealloc(data);
            return FDN_STREAM_IO;
        }
        return 0;
    }
    if (ReadFile(endpoint, data, (DWORD)limit, &count, NULL) == 0) {
        const DWORD native_error = GetLastError();
        fdn_dealloc(data);
        return native_error == ERROR_BROKEN_PIPE || native_error == ERROR_OPERATION_ABORTED
                   ? FDN_STREAM_EOF
                   : FDN_STREAM_IO;
    }
    if (count == 0) {
        fdn_dealloc(data);
        return FDN_STREAM_EOF;
    }
    if (fdn_bytes_adopt(data, (size_t)count, (size_t)limit, result) != 0) {
        fdn_dealloc(data);
        return FDN_STREAM_IO;
    }
    return 0;
}

int32_t foundation_runtime_process_stream_read_line(uint64_t value, uint64_t limit,
                                                    uint64_t* result) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    HANDLE endpoint;
    uint8_t* data;
    size_t length = 0;
    bool overflow = false;
    bool eof = false;
    if (result == NULL) {
        fdn_panic_cstr("process stream line output is null");
    }
    *result = 0;
    if (handle == NULL || (handle->kind != FDN_STREAM_OUTPUT &&
                           handle->kind != FDN_STREAM_ERROR)) {
        return FDN_STREAM_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > UINT32_MAX) {
        return FDN_STREAM_INVALID_ARGUMENT;
    }
    endpoint = handle->kind == FDN_STREAM_OUTPUT ? handle->process->output
                                                 : handle->process->error;
    if (endpoint == NULL) {
        return FDN_STREAM_CLOSED;
    }
    data = fdn_alloc((size_t)limit);
    while (!eof) {
        if (handle->buffer_offset == handle->buffer_length) {
            DWORD count = 0;
            if (ReadFile(endpoint, handle->buffer, (DWORD)sizeof(handle->buffer), &count,
                         NULL) == 0) {
                const DWORD native_error = GetLastError();
                if (native_error != ERROR_BROKEN_PIPE &&
                    native_error != ERROR_OPERATION_ABORTED) {
                    fdn_dealloc(data);
                    return FDN_STREAM_IO;
                }
                count = 0;
            }
            handle->buffer_offset = 0;
            handle->buffer_length = (size_t)count;
            eof = count == 0;
        }
        while (handle->buffer_offset < handle->buffer_length) {
            const uint8_t byte = handle->buffer[handle->buffer_offset++];
            if (byte == '\n') {
                if (overflow) {
                    fdn_dealloc(data);
                    return FDN_STREAM_OUTPUT_LIMIT;
                }
                if (fdn_bytes_adopt(data, length, (size_t)limit, result) != 0) {
                    fdn_dealloc(data);
                    return FDN_STREAM_IO;
                }
                return 0;
            }
            if (length < (size_t)limit) {
                data[length++] = byte;
            } else {
                overflow = true;
            }
        }
    }
    if (overflow) {
        fdn_dealloc(data);
        return FDN_STREAM_OUTPUT_LIMIT;
    }
    if (length == 0) {
        fdn_dealloc(data);
        return FDN_STREAM_EOF;
    }
    if (fdn_bytes_adopt(data, length, (size_t)limit, result) != 0) {
        fdn_dealloc(data);
        return FDN_STREAM_IO;
    }
    return 0;
}

int32_t foundation_runtime_process_stream_write(uint64_t value, uint64_t bytes_handle) {
    fdn_stream_handle* handle = (fdn_stream_handle*)(uintptr_t)value;
    const uint8_t* data = NULL;
    size_t length = 0;
    size_t offset = 0;
    if (handle == NULL || handle->kind != FDN_STREAM_INPUT || handle->process->input == NULL) {
        return FDN_STREAM_CLOSED;
    }
    if (fdn_bytes_view(bytes_handle, &data, &length) != 0) {
        return FDN_STREAM_INVALID_ARGUMENT;
    }
    while (offset < length) {
        DWORD count = 0;
        const DWORD chunk = length - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(length - offset);
        if (WriteFile(handle->process->input, data + offset, chunk, &count, NULL) == 0 ||
            count == 0) {
            const DWORD native_error = GetLastError();
            return native_error == ERROR_BROKEN_PIPE || native_error == ERROR_OPERATION_ABORTED
                       ? FDN_STREAM_CLOSED
                       : FDN_STREAM_IO;
        }
        offset += count;
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
typedef int fdn_process_stream_windows_translation_unit;
#endif
