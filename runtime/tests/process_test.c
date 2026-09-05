#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "../src/bytes_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static fdn_string text(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static int bytes_are(uint64_t handle, const char *expected) {
    const uint8_t *data = NULL;
    size_t length = 0;
    const size_t expected_length = strlen(expected);
    return fdn_bytes_view(handle, &data, &length) == 0 &&
           length == expected_length &&
           (length == 0 || memcmp(data, expected, length) == 0);
}

static int bytes_contain(const uint8_t *data, size_t length,
                         const char *expected) {
    const size_t expected_length = strlen(expected);
    size_t offset;
    if (expected_length > length) {
        return 0;
    }
    for (offset = 0; offset <= length - expected_length; ++offset) {
        if (memcmp(data + offset, expected, expected_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint64_t bytes_from(const char *value) {
    const size_t length = strlen(value);
    uint8_t *data = fdn_alloc(length);
    uint64_t handle = 0;
    if (length != 0) {
        (void)memcpy(data, value, length);
    }
    if (fdn_bytes_adopt(data, length, length, &handle) != 0) {
        fdn_dealloc(data);
        return 0;
    }
    return handle;
}

static int current_process_is_valid(void) {
    fdn_string executable = fdn_string_static("", 0);
    int result = 0;
    if (foundation_runtime_process_current_id() == 0 ||
        foundation_runtime_process_executable(&executable) != 0 ||
        executable.length == 0) {
        result = 1;
    }
    fdn_string_drop(&executable);
    return result;
}

static int path_bytes_are(uint64_t handle, const char *expected) {
#if defined(_WIN32)
    const uint8_t *data = NULL;
    size_t length = 0;
    const size_t expected_length = strlen(expected);
    if (fdn_bytes_view(handle, &data, &length) != 0 ||
        length != expected_length) {
        return 0;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char actual = data[index];
        unsigned char wanted = (unsigned char)expected[index];
        if (actual == '\\') {
            actual = '/';
        }
        if (wanted == '\\') {
            wanted = '/';
        }
        if (actual >= 'A' && actual <= 'Z') {
            actual = (unsigned char)(actual - 'A' + 'a');
        }
        if (wanted >= 'A' && wanted <= 'Z') {
            wanted = (unsigned char)(wanted - 'A' + 'a');
        }
        if (actual != wanted) {
            return 0;
        }
    }
    return 1;
#else
    return bytes_are(handle, expected);
#endif
}

static int child_main(int argc, char **argv) {
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1 ||
        _setmode(_fileno(stderr), _O_BINARY) == -1) {
        return 4;
    }
#endif
    if (argc >= 3 && strcmp(argv[2], "emit") == 0 && argc == 4) {
        (void)fprintf(stdout, "out:%s\n", argv[3]);
        (void)fprintf(stderr, "err:%s\n", argv[3]);
        return 7;
    }
    if (argc >= 3 && strcmp(argv[2], "cwd") == 0) {
        char directory[4096];
        if (getcwd(directory, sizeof(directory)) == NULL) {
            return 2;
        }
        (void)fprintf(stdout, "%s\n", directory);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "environment") == 0) {
        const char *value = getenv("FDN_PROCESS_TEST");
        (void)fprintf(stdout, "%s\n", value == NULL ? "missing" : value);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "flood") == 0) {
        char output[256];
        (void)memset(output, 'x', sizeof(output));
        return fwrite(output, 1, sizeof(output), stdout) == sizeof(output) ? 0 : 3;
    }
    if (argc >= 3 && strcmp(argv[2], "pty") == 0) {
        char input[64];
        const char *value = getenv("FDN_PTY_TEST");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            return 5;
        }
        (void)fprintf(stdout, "pty:%s:%s", value == NULL ? "missing" : value, input);
        (void)fflush(stdout);
        return 7;
    }
    if (argc >= 3 && strcmp(argv[2], "pty-output") == 0) {
        (void)fputs("foundation-pty\n", stdout);
        (void)fflush(stdout);
        return 7;
    }
#if defined(_WIN32)
    if (argc >= 3 && strcmp(argv[2], "handle") == 0 && argc == 4) {
        const uintptr_t value = (uintptr_t)strtoull(argv[3], NULL, 10);
        (void)SetEvent((HANDLE)value);
        return 0;
    }
#endif
    return 1;
}

static int run_pty(const char *program) {
    fdn_string program_value = text(program);
    fdn_string child = text("child");
    fdn_string mode = text("pty");
    fdn_string environment = text("FDN_PTY_TEST=foundation-value");
    uint64_t process = 0;
    uint64_t reader = 0;
    uint64_t writer = 0;
    uint64_t controller = 0;
    uint64_t waiter = 0;
    uint64_t input = 0;
    uint64_t output = 0;
    int32_t exit_code = 0;
    uint8_t collected[8192];
    size_t collected_length = 0;
    const uint8_t *data = NULL;
    size_t length = 0;
    int status = 1;
    if (foundation_runtime_process_open(&program_value, 0, true, &process) != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &mode) != 0 ||
        foundation_runtime_process_add_environment(process, &environment) != 0 ||
        foundation_runtime_process_pty_start(process, 80, 24, &reader, &writer, &controller,
                                             &waiter) != 0 ||
        foundation_runtime_process_pty_live_handles() != 1 ||
        foundation_runtime_process_pty_resize(controller, 120, 40) != 0) {
        goto cleanup;
    }
    input = bytes_from("MARKER-OK\n");
    if (input == 0 || foundation_runtime_process_pty_write(writer, input) != 0) {
        goto cleanup;
    }
    while (!bytes_contain(collected, collected_length,
                          "pty:foundation-value:MARKER-OK")) {
        if (foundation_runtime_process_pty_read(reader, 4096, &output) != 0 ||
            fdn_bytes_view(output, &data, &length) != 0 ||
            length > sizeof(collected) - collected_length) {
            goto cleanup;
        }
        (void)memcpy(collected + collected_length, data, length);
        collected_length += length;
        foundation_runtime_bytes_close(&output);
    }
    if (foundation_runtime_process_pty_wait(waiter, &exit_code) != 0 || exit_code != 7) {
        goto cleanup;
    }
    status = 0;

cleanup:
    foundation_runtime_bytes_close(&input);
    foundation_runtime_bytes_close(&output);
    foundation_runtime_process_pty_abort(controller);
    foundation_runtime_process_pty_reader_close(reader);
    foundation_runtime_process_pty_writer_close(writer);
    foundation_runtime_process_pty_controller_close(controller);
    foundation_runtime_process_pty_waiter_close(waiter);
    foundation_runtime_process_close(&process);
    if (status == 0 && foundation_runtime_process_pty_live_handles() != 0) {
        return 2;
    }
    return status;
}

#if defined(_WIN32)
static int inherited_handle_is_confined(const char *program) {
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE sentinel = CreateEventW(&security, TRUE, FALSE, NULL);
    char value[32];
    fdn_string program_value = text(program);
    fdn_string child = text("child");
    fdn_string mode = text("handle");
    fdn_string handle_value;
    uint64_t process = 0;
    uint64_t stdout_handle = 0;
    uint64_t stderr_handle = 0;
    int32_t exit_code = 0;
    int status = 1;
    if (sentinel == NULL ||
        snprintf(value, sizeof(value), "%llu",
                 (unsigned long long)(uintptr_t)sentinel) < 0) {
        if (sentinel != NULL) {
            (void)CloseHandle(sentinel);
        }
        return 1;
    }
    handle_value = text(value);
    if (foundation_runtime_process_open(&program_value, 64, true, &process) == 0 &&
        foundation_runtime_process_add_argument(process, &child) == 0 &&
        foundation_runtime_process_add_argument(process, &mode) == 0 &&
        foundation_runtime_process_add_argument(process, &handle_value) == 0 &&
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) == 0 &&
        exit_code == 0 && bytes_are(stdout_handle, "") &&
        bytes_are(stderr_handle, "") &&
        WaitForSingleObject(sentinel, 0) == WAIT_TIMEOUT) {
        status = 0;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);
    (void)CloseHandle(sentinel);
    return status;
}
#endif

static int run_process(const char *program, const char *working_directory) {
    const char *literal = "value ; $() & with spaces";
    const char *expected_stdout = "out:value ; $() & with spaces\n";
    const char *expected_stderr = "err:value ; $() & with spaces\n";
    fdn_string program_value = text(program);
    fdn_string working_directory_value = text(working_directory);
    fdn_string child = text("child");
    fdn_string emit = text("emit");
    fdn_string argument = text(literal);
    fdn_string cwd = text("cwd");
    fdn_string environment_mode = text("environment");
    fdn_string environment = text("FDN_PROCESS_TEST=foundation-value");
    fdn_string flood = text("flood");
    fdn_string missing = text("foundation-process-missing-7d8e3b2f");
    uint64_t process = 0;
    uint64_t stdout_handle = 0;
    uint64_t stderr_handle = 0;
    int32_t exit_code = 0;
    int32_t status;
    char expected_cwd[4096];

    if (foundation_runtime_process_open(&program_value, 4096, true, &process) != 0 ||
        process == 0 || foundation_runtime_process_live_handles() != 1 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &emit) != 0 ||
        foundation_runtime_process_add_argument(process, &argument) != 0 ||
        foundation_runtime_process_set_working_directory(
            process, &working_directory_value) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 0 ||
        exit_code != 7 || !bytes_are(stdout_handle, expected_stdout) ||
        !bytes_are(stderr_handle, expected_stderr)) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 1;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);

    if (snprintf(expected_cwd, sizeof(expected_cwd), "%s\n", working_directory) < 0) {
        return 2;
    }
    if (foundation_runtime_process_open(&program_value, 4096, true, &process) != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &cwd) != 0 ||
        foundation_runtime_process_set_working_directory(
            process, &working_directory_value) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 0 ||
        exit_code != 0 || !path_bytes_are(stdout_handle, expected_cwd) ||
        !bytes_are(stderr_handle, "")) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 2;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);

    if (foundation_runtime_process_open(&program_value, 64, true, &process) != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &flood) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 6 ||
        stdout_handle != 0 || stderr_handle != 0) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 3;
    }
    foundation_runtime_process_close(&process);

    if (foundation_runtime_process_open(&missing, 64, true, &process) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 1 ||
        stdout_handle != 0 || stderr_handle != 0) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 4;
    }
    foundation_runtime_process_close(&process);

    if (foundation_runtime_process_open(&program_value, 4096, false, &process) != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &environment_mode) != 0 ||
        foundation_runtime_process_add_environment(process, &environment) != 0 ||
        foundation_runtime_process_add_environment(process, &environment) != 4 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 0 ||
        exit_code != 0 || !bytes_are(stdout_handle, "foundation-value\n") ||
        !bytes_are(stderr_handle, "")) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 5;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);

    status = foundation_runtime_process_open(&program_value, 64, true, &process);
    if (status != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_add_argument(process, &environment_mode) != 0 ||
        foundation_runtime_process_add_environment(process, &environment) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 0 ||
        exit_code != 0 || !bytes_are(stdout_handle, "foundation-value\n") ||
        !bytes_are(stderr_handle, "")) {
        foundation_runtime_bytes_close(&stdout_handle);
        foundation_runtime_bytes_close(&stderr_handle);
        foundation_runtime_process_close(&process);
        return 6;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);
#if defined(_WIN32)
    if (inherited_handle_is_confined(program) != 0) {
        return 7;
    }
#endif
    return foundation_runtime_process_live_handles() == 0 &&
                   fdn_live_allocations() == 0
               ? 0
               : 8;
}

int main(int argc, char **argv) {
    int status;
    if (argc >= 2 && strcmp(argv[1], "child") == 0) {
        return child_main(argc, argv);
    }
    if (argc != 2) {
        return 1;
    }
    if (current_process_is_valid() != 0) {
        return 9;
    }
    status = run_process(argv[0], argv[1]);
    if (status == 0) {
        status = run_pty(argv[0]);
    }
    if (status != 0) {
        (void)fprintf(stderr, "runtime.process failed at check %d\n", status);
    }
    return status;
}
