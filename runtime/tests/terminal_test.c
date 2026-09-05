#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../src/bytes_internal.h"
#include "foundation/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

int main(void) {
    uint64_t reader = 0;
    uint64_t controller = 0;
    uint16_t columns = 0;
    uint16_t rows = 0;
    const int32_t status = foundation_runtime_terminal_open(&reader, &controller, &columns, &rows);
    if (status != 0 && status != 2) {
        return 1;
    }
    if (status == 0) {
        if (reader == 0 || controller == 0 || columns == 0 || rows == 0 ||
            foundation_runtime_terminal_live_handles() != 1) {
            return 2;
        }
        foundation_runtime_terminal_controller_close(controller);
        foundation_runtime_terminal_reader_close(reader);
    }
    return foundation_runtime_terminal_live_handles() == 0 ? 0 : 3;
}

#else

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int same_mode(const struct termios* left, const struct termios* right) {
    return left->c_iflag == right->c_iflag && left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag && left->c_lflag == right->c_lflag;
}

int main(void) {
    struct winsize initial_size = {24, 80, 0, 0};
    const struct winsize resized = {37, 111, 0, 0};
    const uint8_t expected[] = {'x'};
    const uint8_t* data = NULL;
    struct termios original;
    struct termios raw;
    struct termios restored;
    int master = -1;
    int slave = -1;
    int saved_input = -1;
    int saved_output = -1;
    uint64_t reader = 0;
    uint64_t controller = 0;
    uint64_t duplicate_reader = 0;
    uint64_t duplicate_controller = 0;
    uint64_t value = 0;
    uint32_t kind = 0;
    uint16_t columns = 0;
    uint16_t rows = 0;
    size_t length = 0;
    int status = 1;

    if (openpty(&master, &slave, NULL, NULL, &initial_size) != 0 ||
        tcgetattr(slave, &original) != 0 || (saved_input = dup(STDIN_FILENO)) < 0 ||
        (saved_output = dup(STDOUT_FILENO)) < 0 || dup2(slave, STDIN_FILENO) < 0 ||
        dup2(slave, STDOUT_FILENO) < 0) {
        goto cleanup;
    }
    if (foundation_runtime_terminal_open(&reader, &controller, &columns, &rows) != 0 ||
        reader == 0 || controller == 0 || columns != 80 || rows != 24 ||
        foundation_runtime_terminal_live_handles() != 1 ||
        foundation_runtime_terminal_open(&duplicate_reader, &duplicate_controller, &columns,
                                         &rows) != 2 ||
        tcgetattr(STDIN_FILENO, &raw) != 0 ||
        (raw.c_lflag & (ECHO | ICANON | IEXTEN | ISIG)) != 0 ||
        write(master, expected, sizeof(expected)) != (ssize_t)sizeof(expected) ||
        foundation_runtime_terminal_read(reader, 16, &kind, &columns, &rows, &value) != 0 ||
        kind != 1 || fdn_bytes_view(value, &data, &length) != 0 || length != sizeof(expected) ||
        memcmp(data, expected, length) != 0) {
        goto cleanup;
    }
    foundation_runtime_bytes_close(&value);
    if (ioctl(slave, TIOCSWINSZ, &resized) != 0 ||
        foundation_runtime_terminal_read(reader, 16, &kind, &columns, &rows, &value) != 0 ||
        kind != 2 || columns != 111 || rows != 37) {
        goto cleanup;
    }
    foundation_runtime_terminal_abort(controller);
    if (foundation_runtime_terminal_read(reader, 16, &kind, &columns, &rows, &value) != 4 ||
        tcgetattr(STDIN_FILENO, &restored) != 0 || !same_mode(&original, &restored)) {
        goto cleanup;
    }
    status = 0;

cleanup:
    foundation_runtime_bytes_close(&value);
    foundation_runtime_terminal_controller_close(controller);
    foundation_runtime_terminal_reader_close(reader);
    if (saved_input >= 0) {
        (void)dup2(saved_input, STDIN_FILENO);
        (void)close(saved_input);
    }
    if (saved_output >= 0) {
        (void)dup2(saved_output, STDOUT_FILENO);
        (void)close(saved_output);
    }
    if (slave >= 0) {
        (void)close(slave);
    }
    if (master >= 0) {
        (void)close(master);
    }
    if (status == 0 &&
        (foundation_runtime_terminal_live_handles() != 0 || fdn_live_allocations() != 0)) {
        status = 2;
    }
    if (status != 0) {
        (void)fprintf(stderr, "runtime.terminal failed at check %d\n", status);
    }
    return status;
}

#endif
