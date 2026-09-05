#include "foundation/runtime.h"
#include "../src/bytes_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static fdn_string text(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static int bytes_are(uint64_t handle, const uint8_t *expected,
                     size_t expected_length) {
    const uint8_t *data = NULL;
    size_t length = 0;
    return fdn_bytes_view(handle, &data, &length) == 0 &&
           length == expected_length &&
           (length == 0 || memcmp(data, expected, length) == 0);
}

static FILE *redirect_stdin(const char *path) {
#if defined(_MSC_VER)
    FILE *stream = NULL;
    if (freopen_s(&stream, path, "rb", stdin) != 0) {
        return NULL;
    }
    return stream;
#else
    return freopen(path, "rb", stdin);
#endif
}

static int child_main(void) {
    static const uint8_t stdout_bytes[] = {'o', 0, 0xff};
    static const uint8_t stderr_bytes[] = {'e', 0, 0xfe};
    const fdn_string stdout_value = fdn_string_static(
        (const char *)stdout_bytes, sizeof(stdout_bytes));
    const fdn_string stderr_value = fdn_string_static(
        (const char *)stderr_bytes, sizeof(stderr_bytes));
    const fdn_string stdout_text = fdn_string_static("ut", 2);
    const fdn_string stderr_text = fdn_string_static("rr", 2);
    uint64_t stdout_handle = foundation_runtime_bytes_from_text(&stdout_value);
    uint64_t stderr_handle = foundation_runtime_bytes_from_text(&stderr_value);
    const int status =
        foundation_runtime_io_write_stdout_bytes(stdout_handle) != 0 ||
        foundation_runtime_io_write_stdout_text(&stdout_text) != 0 ||
        foundation_runtime_io_write_stderr_bytes(stderr_handle) != 0 ||
        foundation_runtime_io_write_stderr_text(&stderr_text) != 0;
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    return status;
}

static int read_line_test(void) {
    const char *input_path = "foundation-runtime-io-input.txt";
    static const char input[] = "line\r\nlast";
    FILE *fixture = fopen(input_path, "wb");
    fdn_string line = fdn_string_static("", 0);
    bool eof = false;
    int status = 0;
    if (fixture == NULL) {
        return 1;
    }
    const size_t written = fwrite(input, 1, sizeof(input) - 1, fixture);
    const int close_status = fclose(fixture);
    if (written != sizeof(input) - 1 || close_status != 0 ||
        redirect_stdin(input_path) == NULL) {
        (void)remove(input_path);
        return 1;
    }
    if (foundation_runtime_io_read_stdin_line(&line, &eof) != 0 || eof ||
        line.length != 4 || memcmp(line.data, "line", 4) != 0) {
        status = 1;
    }
    fdn_string_drop(&line);
    if (status == 0 &&
        (foundation_runtime_io_read_stdin_line(&line, &eof) != 0 || eof ||
         line.length != 4 || memcmp(line.data, "last", 4) != 0)) {
        status = 1;
    }
    fdn_string_drop(&line);
    if (status == 0 &&
        (foundation_runtime_io_read_stdin_line(&line, &eof) != 0 || !eof ||
         line.length != 0)) {
        status = 1;
    }
    fdn_string_drop(&line);
#if defined(_WIN32)
    if (redirect_stdin("NUL") == NULL) {
        status = 1;
    }
#else
    if (redirect_stdin("/dev/null") == NULL) {
        status = 1;
    }
#endif
    if (remove(input_path) != 0) {
        status = 1;
    }
    return status;
}

int main(int argc, char **argv) {
    static const uint8_t expected_stdout[] = {'o', 0, 0xff, 'u', 't'};
    static const uint8_t expected_stderr[] = {'e', 0, 0xfe, 'r', 'r'};
    fdn_string program;
    fdn_string child = fdn_string_static("child", 5);
    uint64_t process = 0;
    uint64_t stdout_handle = 0;
    uint64_t stderr_handle = 0;
    int32_t exit_code = 0;
    int status = 0;
    if (argc == 2 && strcmp(argv[1], "child") == 0) {
        return child_main();
    }
    if (argc != 1) {
        return 1;
    }
    program = text(argv[0]);
    if (read_line_test() != 0 ||
        foundation_runtime_io_read_stdin_line(NULL, NULL) != 1 ||
        foundation_runtime_io_write_stdout_bytes(0) != 1 ||
        foundation_runtime_io_write_stderr_bytes(0) != 1 ||
        foundation_runtime_io_write_stdout_text(NULL) != 1 ||
        foundation_runtime_io_write_stderr_text(NULL) != 1 ||
        foundation_runtime_process_open(&program, 64, true, &process) != 0 ||
        foundation_runtime_process_add_argument(process, &child) != 0 ||
        foundation_runtime_process_run(process, &exit_code, &stdout_handle,
                                       &stderr_handle) != 0 ||
        exit_code != 0 ||
        !bytes_are(stdout_handle, expected_stdout, sizeof(expected_stdout)) ||
        !bytes_are(stderr_handle, expected_stderr, sizeof(expected_stderr))) {
        status = 2;
    }
    foundation_runtime_bytes_close(&stdout_handle);
    foundation_runtime_bytes_close(&stderr_handle);
    foundation_runtime_process_close(&process);
    if (status == 0 && fdn_live_allocations() != 0) {
        status = 3;
    }
    if (status != 0) {
        (void)fprintf(stderr, "runtime.io failed at check %d\n", status);
    }
    return status;
}
