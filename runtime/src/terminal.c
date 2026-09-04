#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "bytes_internal.h"
#include "foundation/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FDN_TERMINAL_INVALID_ARGUMENT = 1,
    FDN_TERMINAL_UNAVAILABLE = 2,
    FDN_TERMINAL_IO = 3,
    FDN_TERMINAL_CLOSED = 4,
    FDN_TERMINAL_DATA = 1,
    FDN_TERMINAL_RESIZE = 2,
    FDN_TERMINAL_EOF = 3,
};

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

typedef struct fdn_terminal {
    HANDLE input;
    HANDLE output;
    HANDLE cancel;
    DWORD input_mode;
    DWORD output_mode;
    uint16_t columns;
    uint16_t rows;
    size_t references;
    bool restored;
    CRITICAL_SECTION lock;
} fdn_terminal;

static volatile LONG fdn_terminal_active;
static volatile LONG64 fdn_terminal_count;

static void fdn_terminal_enter(fdn_terminal* terminal) { EnterCriticalSection(&terminal->lock); }

static void fdn_terminal_leave(fdn_terminal* terminal) { LeaveCriticalSection(&terminal->lock); }

static int fdn_terminal_dimensions(HANDLE output, uint16_t* columns, uint16_t* rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    const SHORT width = GetConsoleScreenBufferInfo(output, &info)
                            ? (SHORT)(info.srWindow.Right - info.srWindow.Left + 1)
                            : 0;
    const SHORT height = width > 0 ? (SHORT)(info.srWindow.Bottom - info.srWindow.Top + 1) : 0;
    if (width <= 0 || height <= 0) {
        return 0;
    }
    *columns = (uint16_t)width;
    *rows = (uint16_t)height;
    return 1;
}

static void fdn_terminal_restore(fdn_terminal* terminal) {
    bool restore = false;
    fdn_terminal_enter(terminal);
    if (!terminal->restored) {
        terminal->restored = true;
        restore = true;
    }
    fdn_terminal_leave(terminal);
    if (restore) {
        (void)SetConsoleMode(terminal->input, terminal->input_mode);
        (void)SetConsoleMode(terminal->output, terminal->output_mode);
        (void)SetEvent(terminal->cancel);
        (void)InterlockedExchange(&fdn_terminal_active, 0);
    }
}

static void fdn_terminal_release(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    bool destroy;
    if (terminal == NULL) {
        return;
    }
    fdn_terminal_enter(terminal);
    if (terminal->references == 0) {
        fdn_terminal_leave(terminal);
        fdn_panic_cstr("terminal reference underflow");
    }
    --terminal->references;
    destroy = terminal->references == 0;
    fdn_terminal_leave(terminal);
    if (destroy) {
        fdn_terminal_restore(terminal);
        (void)CloseHandle(terminal->cancel);
        DeleteCriticalSection(&terminal->lock);
        fdn_dealloc(terminal);
        if (InterlockedDecrement64(&fdn_terminal_count) < 0) {
            fdn_panic_cstr("terminal handle count underflow");
        }
    }
}

int32_t foundation_runtime_terminal_open(uint64_t* reader, uint64_t* controller, uint16_t* columns,
                                         uint16_t* rows) {
    fdn_terminal* terminal;
    HANDLE input;
    HANDLE output;
    DWORD input_mode;
    DWORD output_mode;
    DWORD raw_input;
    DWORD raw_output;
    HANDLE cancel;
    if (reader == NULL || controller == NULL || columns == NULL || rows == NULL) {
        fdn_panic_cstr("terminal output is null");
    }
    *reader = 0;
    *controller = 0;
    *columns = 0;
    *rows = 0;
    if (InterlockedCompareExchange(&fdn_terminal_active, 1, 0) != 0) {
        return FDN_TERMINAL_UNAVAILABLE;
    }
    input = GetStdHandle(STD_INPUT_HANDLE);
    output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == NULL || input == INVALID_HANDLE_VALUE || output == NULL ||
        output == INVALID_HANDLE_VALUE || GetConsoleMode(input, &input_mode) == 0 ||
        GetConsoleMode(output, &output_mode) == 0 ||
        !fdn_terminal_dimensions(output, columns, rows)) {
        (void)InterlockedExchange(&fdn_terminal_active, 0);
        return FDN_TERMINAL_UNAVAILABLE;
    }
    cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (cancel == NULL) {
        (void)InterlockedExchange(&fdn_terminal_active, 0);
        return FDN_TERMINAL_IO;
    }
    raw_input = input_mode;
    raw_input &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_MOUSE_INPUT |
                   ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT);
    raw_input |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    raw_output = output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(input, raw_input) == 0 || SetConsoleMode(output, raw_output) == 0) {
        (void)SetConsoleMode(input, input_mode);
        (void)SetConsoleMode(output, output_mode);
        (void)CloseHandle(cancel);
        (void)InterlockedExchange(&fdn_terminal_active, 0);
        return FDN_TERMINAL_IO;
    }
    terminal = fdn_alloc(sizeof(*terminal));
    terminal->input = input;
    terminal->output = output;
    terminal->cancel = cancel;
    terminal->input_mode = input_mode;
    terminal->output_mode = output_mode;
    terminal->columns = *columns;
    terminal->rows = *rows;
    terminal->references = 2;
    terminal->restored = false;
    InitializeCriticalSection(&terminal->lock);
    *reader = (uint64_t)(uintptr_t)terminal;
    *controller = (uint64_t)(uintptr_t)terminal;
    if (InterlockedIncrement64(&fdn_terminal_count) <= 0) {
        fdn_panic_cstr("terminal handle count overflow");
    }
    return 0;
}

int32_t foundation_runtime_terminal_read(uint64_t handle, uint64_t limit, uint32_t* kind,
                                         uint16_t* columns, uint16_t* rows, uint64_t* value) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    HANDLE waiting[2];
    uint8_t* data;
    DWORD count = 0;
    if (kind == NULL || columns == NULL || rows == NULL || value == NULL) {
        fdn_panic_cstr("terminal read output is null");
    }
    *kind = 0;
    *columns = 0;
    *rows = 0;
    *value = 0;
    if (terminal == NULL) {
        return FDN_TERMINAL_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > UINT32_MAX) {
        return FDN_TERMINAL_INVALID_ARGUMENT;
    }
    waiting[0] = terminal->cancel;
    waiting[1] = terminal->input;
    for (;;) {
        uint16_t current_columns = 0;
        uint16_t current_rows = 0;
        const DWORD event = WaitForMultipleObjects(2, waiting, FALSE, 100);
        if (event == WAIT_OBJECT_0) {
            return FDN_TERMINAL_CLOSED;
        }
        if (event == WAIT_FAILED) {
            return FDN_TERMINAL_IO;
        }
        if (fdn_terminal_dimensions(terminal->output, &current_columns, &current_rows) &&
            (current_columns != terminal->columns || current_rows != terminal->rows)) {
            terminal->columns = current_columns;
            terminal->rows = current_rows;
            *kind = FDN_TERMINAL_RESIZE;
            *columns = current_columns;
            *rows = current_rows;
            return 0;
        }
        if (event == WAIT_TIMEOUT) {
            continue;
        }
        data = fdn_alloc((size_t)limit);
        if (ReadFile(terminal->input, data, (DWORD)limit, &count, NULL) == 0) {
            const DWORD error = GetLastError();
            fdn_dealloc(data);
            return error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED
                       ? FDN_TERMINAL_CLOSED
                       : FDN_TERMINAL_IO;
        }
        if (count == 0) {
            fdn_dealloc(data);
            *kind = FDN_TERMINAL_EOF;
            return 0;
        }
        if (fdn_bytes_adopt(data, (size_t)count, (size_t)limit, value) != 0) {
            fdn_dealloc(data);
            return FDN_TERMINAL_IO;
        }
        *kind = FDN_TERMINAL_DATA;
        return 0;
    }
}

void foundation_runtime_terminal_abort(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    if (terminal != NULL) {
        fdn_terminal_restore(terminal);
    }
}

void foundation_runtime_terminal_reader_close(uint64_t handle) { fdn_terminal_release(handle); }

void foundation_runtime_terminal_controller_close(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    if (terminal != NULL) {
        fdn_terminal_restore(terminal);
    }
    fdn_terminal_release(handle);
}

uint64_t foundation_runtime_terminal_live_handles(void) {
    return (uint64_t)InterlockedCompareExchange64(&fdn_terminal_count, 0, 0);
}

#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct fdn_terminal {
    struct termios mode;
    int cancel_read;
    int cancel_write;
    uint16_t columns;
    uint16_t rows;
    size_t references;
    bool restored;
    pthread_mutex_t lock;
} fdn_terminal;

static atomic_bool fdn_terminal_active;
static atomic_uint_fast64_t fdn_terminal_count;

static void fdn_terminal_enter(fdn_terminal* terminal) {
    if (pthread_mutex_lock(&terminal->lock) != 0) {
        fdn_panic_cstr("terminal lock failed");
    }
}

static void fdn_terminal_leave(fdn_terminal* terminal) {
    if (pthread_mutex_unlock(&terminal->lock) != 0) {
        fdn_panic_cstr("terminal unlock failed");
    }
}

static int fdn_terminal_dimensions(uint16_t* columns, uint16_t* rows) {
    struct winsize size;
    (void)memset(&size, 0, sizeof(size));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_col == 0 || size.ws_row == 0) {
        return 0;
    }
    *columns = size.ws_col;
    *rows = size.ws_row;
    return 1;
}

static void fdn_terminal_restore(fdn_terminal* terminal) {
    bool restore = false;
    fdn_terminal_enter(terminal);
    if (!terminal->restored) {
        terminal->restored = true;
        restore = true;
    }
    fdn_terminal_leave(terminal);
    if (restore) {
        const uint8_t signal = 1;
        ssize_t written;
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &terminal->mode);
        written = write(terminal->cancel_write, &signal, sizeof(signal));
        (void)written;
        atomic_store_explicit(&fdn_terminal_active, false, memory_order_release);
    }
}

static void fdn_terminal_release(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    bool destroy;
    if (terminal == NULL) {
        return;
    }
    fdn_terminal_enter(terminal);
    if (terminal->references == 0) {
        fdn_terminal_leave(terminal);
        fdn_panic_cstr("terminal reference underflow");
    }
    --terminal->references;
    destroy = terminal->references == 0;
    fdn_terminal_leave(terminal);
    if (destroy) {
        fdn_terminal_restore(terminal);
        (void)close(terminal->cancel_read);
        (void)close(terminal->cancel_write);
        if (pthread_mutex_destroy(&terminal->lock) != 0) {
            fdn_panic_cstr("terminal destroy failed");
        }
        fdn_dealloc(terminal);
        if (atomic_fetch_sub_explicit(&fdn_terminal_count, 1, memory_order_relaxed) == 0) {
            fdn_panic_cstr("terminal handle count underflow");
        }
    }
}

int32_t foundation_runtime_terminal_open(uint64_t* reader, uint64_t* controller, uint16_t* columns,
                                         uint16_t* rows) {
    struct termios mode;
    struct termios raw;
    int cancel[2] = {-1, -1};
    fdn_terminal* terminal;
    bool expected = false;
    if (reader == NULL || controller == NULL || columns == NULL || rows == NULL) {
        fdn_panic_cstr("terminal output is null");
    }
    *reader = 0;
    *controller = 0;
    *columns = 0;
    *rows = 0;
    if (!atomic_compare_exchange_strong_explicit(&fdn_terminal_active, &expected, true,
                                                 memory_order_acq_rel, memory_order_acquire)) {
        return FDN_TERMINAL_UNAVAILABLE;
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &mode) != 0 ||
        !fdn_terminal_dimensions(columns, rows) || pipe(cancel) != 0) {
        atomic_store_explicit(&fdn_terminal_active, false, memory_order_release);
        return FDN_TERMINAL_UNAVAILABLE;
    }
    if (fcntl(cancel[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(cancel[1], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(cancel[1], F_SETFL, O_NONBLOCK) != 0) {
        (void)close(cancel[0]);
        (void)close(cancel[1]);
        atomic_store_explicit(&fdn_terminal_active, false, memory_order_release);
        return FDN_TERMINAL_IO;
    }
    raw = mode;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        (void)close(cancel[0]);
        (void)close(cancel[1]);
        atomic_store_explicit(&fdn_terminal_active, false, memory_order_release);
        return FDN_TERMINAL_IO;
    }
    terminal = fdn_alloc(sizeof(*terminal));
    terminal->mode = mode;
    terminal->cancel_read = cancel[0];
    terminal->cancel_write = cancel[1];
    terminal->columns = *columns;
    terminal->rows = *rows;
    terminal->references = 2;
    terminal->restored = false;
    if (pthread_mutex_init(&terminal->lock, NULL) != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &mode);
        (void)close(cancel[0]);
        (void)close(cancel[1]);
        fdn_dealloc(terminal);
        atomic_store_explicit(&fdn_terminal_active, false, memory_order_release);
        return FDN_TERMINAL_IO;
    }
    *reader = (uint64_t)(uintptr_t)terminal;
    *controller = (uint64_t)(uintptr_t)terminal;
    if (atomic_fetch_add_explicit(&fdn_terminal_count, 1, memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("terminal handle count overflow");
    }
    return 0;
}

int32_t foundation_runtime_terminal_read(uint64_t handle, uint64_t limit, uint32_t* kind,
                                         uint16_t* columns, uint16_t* rows, uint64_t* value) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    struct pollfd waiting[2];
    uint8_t* data;
    ssize_t count;
    if (kind == NULL || columns == NULL || rows == NULL || value == NULL) {
        fdn_panic_cstr("terminal read output is null");
    }
    *kind = 0;
    *columns = 0;
    *rows = 0;
    *value = 0;
    if (terminal == NULL) {
        return FDN_TERMINAL_CLOSED;
    }
    if (limit == 0 || limit > 16777216 || limit > (uint64_t)SIZE_MAX) {
        return FDN_TERMINAL_INVALID_ARGUMENT;
    }
    waiting[0].fd = terminal->cancel_read;
    waiting[0].events = POLLIN;
    waiting[1].fd = STDIN_FILENO;
    waiting[1].events = POLLIN;
    for (;;) {
        uint16_t current_columns = 0;
        uint16_t current_rows = 0;
        int status;
        do {
            status = poll(waiting, 2, 100);
        } while (status < 0 && errno == EINTR);
        if (status < 0) {
            return FDN_TERMINAL_IO;
        }
        if ((waiting[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            return FDN_TERMINAL_CLOSED;
        }
        if (fdn_terminal_dimensions(&current_columns, &current_rows) &&
            (current_columns != terminal->columns || current_rows != terminal->rows)) {
            terminal->columns = current_columns;
            terminal->rows = current_rows;
            *kind = FDN_TERMINAL_RESIZE;
            *columns = current_columns;
            *rows = current_rows;
            return 0;
        }
        if ((waiting[1].revents & (POLLERR | POLLNVAL)) != 0) {
            return FDN_TERMINAL_IO;
        }
        if ((waiting[1].revents & POLLHUP) != 0) {
            *kind = FDN_TERMINAL_EOF;
            return 0;
        }
        if ((waiting[1].revents & POLLIN) == 0) {
            continue;
        }
        data = fdn_alloc((size_t)limit);
        do {
            count = read(STDIN_FILENO, data, (size_t)limit);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            fdn_dealloc(data);
            return FDN_TERMINAL_IO;
        }
        if (count == 0) {
            fdn_dealloc(data);
            *kind = FDN_TERMINAL_EOF;
            return 0;
        }
        if (fdn_bytes_adopt(data, (size_t)count, (size_t)limit, value) != 0) {
            fdn_dealloc(data);
            return FDN_TERMINAL_IO;
        }
        *kind = FDN_TERMINAL_DATA;
        return 0;
    }
}

void foundation_runtime_terminal_abort(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    if (terminal != NULL) {
        fdn_terminal_restore(terminal);
    }
}

void foundation_runtime_terminal_reader_close(uint64_t handle) { fdn_terminal_release(handle); }

void foundation_runtime_terminal_controller_close(uint64_t handle) {
    fdn_terminal* terminal = (fdn_terminal*)(uintptr_t)handle;
    if (terminal != NULL) {
        fdn_terminal_restore(terminal);
    }
    fdn_terminal_release(handle);
}

uint64_t foundation_runtime_terminal_live_handles(void) {
    return atomic_load_explicit(&fdn_terminal_count, memory_order_relaxed);
}
#endif
