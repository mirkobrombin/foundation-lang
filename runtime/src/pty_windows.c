#if defined(_WIN32)
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "bytes_internal.h"
#include "foundation/process_internal.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

enum {
    FDN_PTY_NOT_FOUND = 1,
    FDN_PTY_PERMISSION = 2,
    FDN_PTY_INVALID_ARGUMENT = 3,
    FDN_PTY_RESOURCE_LIMIT = 7,
    FDN_PTY_IO = 8,
    FDN_PTY_CLOSED = 9,
    FDN_PTY_UNAVAILABLE = 10,
    FDN_PTY_EOF = 11,
};

typedef HRESULT(WINAPI *fdn_create_pseudo_console_fn)(COORD, HANDLE, HANDLE, DWORD,
                                                      HANDLE *);
typedef HRESULT(WINAPI *fdn_resize_pseudo_console_fn)(HANDLE, COORD);
typedef void(WINAPI *fdn_close_pseudo_console_fn)(HANDLE);

typedef struct fdn_pty {
    HANDLE input;
    HANDLE output;
    HANDLE process;
    HANDLE console;
    fdn_resize_pseudo_console_fn resize_console;
    fdn_close_pseudo_console_fn close_console;
    size_t references;
    bool waited;
    int32_t exit_code;
    CRITICAL_SECTION lock;
} fdn_pty;

static volatile LONG64 fdn_live_pty_count;

static void fdn_pty_count_add(void) {
    if (InterlockedIncrement64(&fdn_live_pty_count) <= 0) {
        fdn_panic_cstr("pty handle count overflow");
    }
}

static void fdn_pty_count_remove(void) {
    if (InterlockedDecrement64(&fdn_live_pty_count) < 0) {
        fdn_panic_cstr("pty handle count underflow");
    }
}

uint64_t foundation_runtime_process_pty_live_handles(void) {
    return (uint64_t)InterlockedCompareExchange64(&fdn_live_pty_count, 0, 0);
}

static void fdn_pty_enter(fdn_pty *pty) { EnterCriticalSection(&pty->lock); }

static void fdn_pty_leave(fdn_pty *pty) { LeaveCriticalSection(&pty->lock); }

static void fdn_pty_abort_process(fdn_pty *pty) {
    fdn_pty_enter(pty);
    if (!pty->waited) {
        (void)TerminateProcess(pty->process, 1);
    }
    if (pty->console != NULL) {
        pty->close_console(pty->console);
        pty->console = NULL;
    }
    (void)CancelIoEx(pty->input, NULL);
    (void)CancelIoEx(pty->output, NULL);
    fdn_pty_leave(pty);
}

static int32_t fdn_pty_wait_process(fdn_pty *pty, int32_t *exit_code) {
    DWORD code = 0;
    fdn_pty_enter(pty);
    if (pty->waited) {
        *exit_code = pty->exit_code;
        fdn_pty_leave(pty);
        return 0;
    }
    fdn_pty_leave(pty);
    if (WaitForSingleObject(pty->process, INFINITE) != WAIT_OBJECT_0 ||
        GetExitCodeProcess(pty->process, &code) == 0) {
        return FDN_PTY_IO;
    }
    *exit_code = (int32_t)code;
    fdn_pty_enter(pty);
    pty->waited = true;
    pty->exit_code = *exit_code;
    if (pty->console != NULL) {
        pty->close_console(pty->console);
        pty->console = NULL;
    }
    fdn_pty_leave(pty);
    return 0;
}

static void fdn_pty_destroy(fdn_pty *pty) {
    int32_t ignored = 0;
    fdn_pty_abort_process(pty);
    (void)fdn_pty_wait_process(pty, &ignored);
    (void)CloseHandle(pty->input);
    (void)CloseHandle(pty->output);
    (void)CloseHandle(pty->process);
    DeleteCriticalSection(&pty->lock);
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

static int32_t fdn_pty_functions(fdn_create_pseudo_console_fn *create,
                                 fdn_resize_pseudo_console_fn *resize,
                                 fdn_close_pseudo_console_fn *close) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (kernel == NULL) {
        return FDN_PTY_UNAVAILABLE;
    }
    *create = (fdn_create_pseudo_console_fn)(uintptr_t)GetProcAddress(
        kernel, "CreatePseudoConsole");
    *resize = (fdn_resize_pseudo_console_fn)(uintptr_t)GetProcAddress(
        kernel, "ResizePseudoConsole");
    *close = (fdn_close_pseudo_console_fn)(uintptr_t)GetProcAddress(
        kernel, "ClosePseudoConsole");
    return *create != NULL && *resize != NULL && *close != NULL ? 0
                                                                : FDN_PTY_UNAVAILABLE;
}

int32_t foundation_runtime_process_pty_start(uint64_t process_handle, uint16_t columns,
                                             uint16_t rows, uint64_t *reader,
                                             uint64_t *writer, uint64_t *controller,
                                             uint64_t *waiter) {
    const fdn_process *process = fdn_process_from_handle(process_handle);
    fdn_create_pseudo_console_fn create_console = NULL;
    fdn_resize_pseudo_console_fn resize_console = NULL;
    fdn_close_pseudo_console_fn close_console = NULL;
    HANDLE input_read = NULL;
    HANDLE input_write = NULL;
    HANDLE output_read = NULL;
    HANDLE output_write = NULL;
    HANDLE console = NULL;
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION information = {0};
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    SIZE_T attributes_size = 0;
    wchar_t *program = NULL;
    wchar_t *command = NULL;
    wchar_t *environment = NULL;
    wchar_t *working_directory = NULL;
    COORD size;
    fdn_pty *pty = NULL;
    int32_t status;
    BOOL attributes_ready = FALSE;
    BOOL created = FALSE;
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
    if (columns == 0 || rows == 0 || columns > INT16_MAX || rows > INT16_MAX) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    status = fdn_pty_functions(&create_console, &resize_console, &close_console);
    if (status != 0) {
        return status;
    }
    program = fdn_process_windows_program(process);
    command = fdn_process_windows_command(process);
    environment = fdn_process_windows_environment(process);
    status = fdn_process_windows_cwd(process, &working_directory);
    if (status != 0) {
        goto cleanup;
    }
    if (program == NULL || command == NULL ||
        (fdn_process_environment_block_required(process) && environment == NULL)) {
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (CreatePipe(&input_read, &input_write, NULL, 0) == 0 ||
        CreatePipe(&output_read, &output_write, NULL, 0) == 0) {
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    size.X = (SHORT)columns;
    size.Y = (SHORT)rows;
    if (FAILED(create_console(size, input_read, output_write, 0, &console))) {
        status = FDN_PTY_IO;
        goto cleanup;
    }
    (void)CloseHandle(input_read);
    input_read = NULL;
    (void)CloseHandle(output_write);
    output_write = NULL;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
    if (attributes_size == 0) {
        status = FDN_PTY_RESOURCE_LIMIT;
        goto cleanup;
    }
    attributes = fdn_alloc(attributes_size);
    if (InitializeProcThreadAttributeList(attributes, 1, 0, &attributes_size) == 0) {
        status = FDN_PTY_IO;
        goto cleanup;
    }
    attributes_ready = TRUE;
    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                  console, sizeof(console), NULL, NULL) == 0) {
        status = FDN_PTY_IO;
        goto cleanup;
    }
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&information, 0, sizeof(information));
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    created = CreateProcessW(
        program, command, NULL, NULL, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        fdn_process_environment_block_required(process) ? environment : NULL,
        working_directory, &startup.StartupInfo, &information);
    if (!created) {
        status = fdn_process_windows_status(GetLastError());
        goto cleanup;
    }
    (void)CloseHandle(information.hThread);
    information.hThread = NULL;
    pty = fdn_alloc(sizeof(*pty));
    pty->input = input_write;
    pty->output = output_read;
    pty->process = information.hProcess;
    pty->console = console;
    pty->resize_console = resize_console;
    pty->close_console = close_console;
    pty->references = 4;
    pty->waited = false;
    pty->exit_code = 0;
    InitializeCriticalSection(&pty->lock);
    input_write = NULL;
    output_read = NULL;
    information.hProcess = NULL;
    console = NULL;
    *reader = (uint64_t)(uintptr_t)pty;
    *writer = (uint64_t)(uintptr_t)pty;
    *controller = (uint64_t)(uintptr_t)pty;
    *waiter = (uint64_t)(uintptr_t)pty;
    fdn_pty_count_add();
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
    if (console != NULL) {
        close_console(console);
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

int32_t foundation_runtime_process_pty_read(uint64_t handle, uint64_t limit,
                                            uint64_t *result) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    uint8_t *data;
    DWORD count = 0;
    if (result == NULL) {
        fdn_panic_cstr("pty read output is null");
    }
    *result = 0;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > UINT32_MAX) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    data = fdn_alloc((size_t)limit);
    if (ReadFile(pty->output, data, (DWORD)limit, &count, NULL) == 0) {
        const DWORD error = GetLastError();
        fdn_dealloc(data);
        return error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED
                   ? FDN_PTY_EOF
                   : FDN_PTY_IO;
    }
    if (count == 0) {
        fdn_dealloc(data);
        return FDN_PTY_EOF;
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
        DWORD count = 0;
        const DWORD chunk =
            length - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(length - offset);
        if (WriteFile(pty->input, data + offset, chunk, &count, NULL) == 0 ||
            count == 0) {
            const DWORD error = GetLastError();
            return error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED
                       ? FDN_PTY_CLOSED
                       : FDN_PTY_IO;
        }
        offset += count;
    }
    return 0;
}

int32_t foundation_runtime_process_pty_resize(uint64_t handle, uint16_t columns,
                                              uint16_t rows) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    COORD size;
    HRESULT result;
    if (pty == NULL) {
        return FDN_PTY_CLOSED;
    }
    if (columns == 0 || rows == 0 || columns > INT16_MAX || rows > INT16_MAX) {
        return FDN_PTY_INVALID_ARGUMENT;
    }
    size.X = (SHORT)columns;
    size.Y = (SHORT)rows;
    fdn_pty_enter(pty);
    result = pty->console == NULL ? E_HANDLE : pty->resize_console(pty->console, size);
    fdn_pty_leave(pty);
    return SUCCEEDED(result) ? 0 : FDN_PTY_IO;
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
    return fdn_pty_wait_process(pty, exit_code);
}

void foundation_runtime_process_pty_abort(uint64_t handle) {
    fdn_pty *pty = (fdn_pty *)(uintptr_t)handle;
    if (pty != NULL) {
        fdn_pty_abort_process(pty);
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
typedef int fdn_pty_windows_translation_unit;
#endif
