#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <share.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int line_is(fdn_string line, const char *expected) {
    const size_t length = strlen(expected);
    return line.length == length &&
           (length == 0 || memcmp(line.data, expected, length) == 0);
}

static FILE *open_file(const char *path, const char *mode) {
#if defined(_WIN32)
    return _fsopen(path, mode, _SH_DENYNO);
#else
    return fopen(path, mode);
#endif
}

static int remove_directory(const char *path) {
#if defined(_WIN32)
    return _rmdir(path);
#else
    return rmdir(path);
#endif
}

int main(int argc, char **argv) {
    fdn_string path;
    fdn_string directory_path;
    fdn_string line = fdn_string_static("", 0);
    fdn_string name = fdn_string_static("", 0);
    fdn_string text = fdn_string_static("", 0);
    uint64_t handle = 0;
    uint64_t directory = 0;
    uint64_t size = 0;
    uint64_t modified = 0;
    bool is_directory = false;
    FILE *writer;
    int32_t status;
    int root_length;
    int nested_length;
    char private_root[4096];
    char private_nested[4096];
#if !defined(_WIN32)
    char private_link[4096];
#endif
    if (argc != 4) {
        return 1;
    }
    path = fdn_string_static(argv[1], strlen(argv[1]));
    directory_path = fdn_string_static(argv[2], strlen(argv[2]));
    if (foundation_runtime_fs_size(&path, &size) != 0 || size == 0 ||
        foundation_runtime_fs_modified(&path, &modified) != 0 || modified < UINT64_C(1700000000) ||
        foundation_runtime_fs_open_lines(&path, &handle) != 0 || handle == 0 ||
        foundation_runtime_fs_live_handles() != 1) {
        return 2;
    }
    if (foundation_runtime_fs_next_line(handle, &line) != 1 ||
        !line_is(line, "{\"id\":1,\"text\":\"first\"}")) {
        return 3;
    }
    if (foundation_runtime_fs_next_line(handle, &line) != 1 ||
        !line_is(line, "{\"id\":2,\"text\":\"h\xc3\xa9llo\"}")) {
        fdn_string_drop(&line);
        return 4;
    }
    if (foundation_runtime_fs_next_line(handle, &line) != 1 ||
        !line_is(line, "{\"id\":3,\"text\":\"last\"}")) {
        fdn_string_drop(&line);
        return 5;
    }
    if (foundation_runtime_fs_next_line(handle, &line) != 0) {
        fdn_string_drop(&line);
        return 6;
    }
    fdn_string_drop(&line);
    if (foundation_runtime_fs_close(handle) != 0 ||
        foundation_runtime_fs_live_handles() != 0 || fdn_live_allocations() != 0) {
        return 7;
    }
    if (foundation_runtime_fs_read_text_limited(&path, size, &text) != 0 ||
        !line_is(text,
                 "{\"id\":1,\"text\":\"first\"}\n"
                 "{\"id\":2,\"text\":\"h\xc3\xa9llo\"}\n"
                 "{\"id\":3,\"text\":\"last\"}\n")) {
        fdn_string_drop(&text);
        return 20;
    }
    fdn_string_drop(&text);
    if (foundation_runtime_fs_read_text_limited(&path, UINT64_C(8), &text) != 5 ||
        text.length != 0 || fdn_live_allocations() != 0) {
        fdn_string_drop(&text);
        return 21;
    }
    if (foundation_runtime_fs_open_lines(&path, &handle) != 0 ||
        foundation_runtime_fs_next_line_limited(handle, UINT64_C(8), &line) != 5) {
        return 8;
    }
    fdn_string_drop(&line);
    if (foundation_runtime_fs_close(handle) != 0 ||
        foundation_runtime_fs_live_handles() != 0 || fdn_live_allocations() != 0) {
        return 9;
    }
    if (foundation_runtime_fs_is_directory(&directory_path, &is_directory) != 0 ||
        !is_directory || foundation_runtime_fs_is_directory(&path, &is_directory) != 0 ||
        is_directory || foundation_runtime_fs_open_directory(&directory_path, &directory) != 0 ||
        directory == 0 || foundation_runtime_fs_live_directories() != 1) {
        return 10;
    }
    if (foundation_runtime_fs_next_directory(directory, &name) != 1 ||
        !line_is(name, "lines.jsonl") ||
        foundation_runtime_fs_next_directory(directory, &name) != 0) {
        fdn_string_drop(&name);
        return 11;
    }
    fdn_string_drop(&name);
    if (foundation_runtime_fs_close_directory(directory) != 0 ||
        foundation_runtime_fs_live_directories() != 0 || fdn_live_allocations() != 0) {
        return 12;
    }
    status = foundation_runtime_fs_open_lines(&directory_path, &handle);
    if (status == 0) {
        if (foundation_runtime_fs_next_line_limited(handle, UINT64_C(32), &line) != 4 ||
            foundation_runtime_fs_close(handle) != 0) {
            return 13;
        }
    }
    if (foundation_runtime_fs_live_handles() != 0 || fdn_live_allocations() != 0) {
        return 14;
    }

    (void)remove(argv[3]);
    writer = open_file(argv[3], "wb");
    if (writer == NULL || fputs("first\n", writer) < 0 || fclose(writer) != 0) {
        return 15;
    }
    path = fdn_string_static(argv[3], strlen(argv[3]));
    if (foundation_runtime_fs_open_lines(&path, &handle) != 0 ||
        foundation_runtime_fs_next_line_limited(handle, UINT64_C(32), &line) != 1 ||
        !line_is(line, "first")) {
        return 16;
    }
    writer = open_file(argv[3], "ab");
    if (writer == NULL || fputs("growing", writer) < 0 || fclose(writer) != 0) {
        return 17;
    }
    if (foundation_runtime_fs_next_line_limited(handle, UINT64_C(32), &line) != 1 ||
        !line_is(line, "growing") ||
        foundation_runtime_fs_next_line_limited(handle, UINT64_C(32), &line) != 0) {
        return 18;
    }
    fdn_string_drop(&line);
    if (foundation_runtime_fs_close(handle) != 0 || remove(argv[3]) != 0 ||
        foundation_runtime_fs_live_handles() != 0 || fdn_live_allocations() != 0) {
        return 19;
    }
    writer = open_file(argv[3], "wb");
    if (writer == NULL || fputc(0xff, writer) == EOF || fclose(writer) != 0) {
        return 22;
    }
    if (foundation_runtime_fs_read_text_limited(&path, UINT64_C(32), &text) != 6 ||
        text.length != 0 || remove(argv[3]) != 0 || fdn_live_allocations() != 0) {
        fdn_string_drop(&text);
        return 23;
    }
    writer = open_file(argv[3], "wb");
    if (writer == NULL || fclose(writer) != 0) {
        return 24;
    }
    if (foundation_runtime_fs_read_text_limited(&path, UINT64_C(0), &text) != 0 ||
        text.length != 0 || remove(argv[3]) != 0 || fdn_live_allocations() != 0) {
        fdn_string_drop(&text);
        return 25;
    }
    text = fdn_string_static("atomic\n", 7);
    if (foundation_runtime_fs_write_private_text_atomic(&path, &text, UINT64_C(3)) != 5 ||
        foundation_runtime_fs_write_private_text_atomic(&path, &text, UINT64_C(7)) != 0 ||
        foundation_runtime_fs_read_private_text_limited(&path, UINT64_C(7), &text) != 0 ||
        !line_is(text, "atomic\n")) {
        fdn_string_drop(&text);
        return 26;
    }
    fdn_string_drop(&text);
#if !defined(_WIN32)
    {
        struct stat info;
        int link_length;
        if (stat(argv[3], &info) != 0 || (info.st_mode & 0777) != 0600) {
            (void)remove(argv[3]);
            return 27;
        }
        link_length = snprintf(private_link, sizeof(private_link), "%s-link", argv[3]);
        if (link_length < 0 || (size_t)link_length >= sizeof(private_link) ||
            symlink(argv[3], private_link) != 0) {
            (void)remove(argv[3]);
            return 32;
        }
        path = fdn_string_static(private_link, strlen(private_link));
        if (foundation_runtime_fs_read_private_text_limited(&path, UINT64_C(7), &text) != 3 ||
            text.length != 0 || foundation_runtime_fs_delete_private_file(&path) != 3 ||
            unlink(private_link) != 0) {
            fdn_string_drop(&text);
            (void)remove(argv[3]);
            return 33;
        }
    }
#endif
    path = fdn_string_static(argv[3], strlen(argv[3]));
    root_length = snprintf(private_root, sizeof(private_root), "%s-dir", argv[3]);
    nested_length = snprintf(private_nested, sizeof(private_nested), "%s/nested", private_root);
    if (foundation_runtime_fs_delete_private_file(&path) != 0 ||
        foundation_runtime_fs_delete_private_file(&path) != 1 ||
        root_length < 0 || nested_length < 0 ||
        (size_t)root_length >= sizeof(private_root) ||
        (size_t)nested_length >= sizeof(private_nested)) {
        return 28;
    }
    path = fdn_string_static(private_nested, strlen(private_nested));
    text = fdn_string_static("", 0);
    if (foundation_runtime_fs_create_private_directory(&text) != 3 ||
        foundation_runtime_fs_create_private_directory(&path) != 0 ||
        foundation_runtime_fs_is_directory(&path, &is_directory) != 0 || !is_directory) {
        return 29;
    }
#if !defined(_WIN32)
    {
        struct stat info;
        if (stat(private_nested, &info) != 0 || (info.st_mode & 0777) != 0700) {
            return 30;
        }
    }
#endif
    if (remove_directory(private_nested) != 0 || remove_directory(private_root) != 0 ||
        fdn_live_allocations() != 0) {
        return 31;
    }
    return 0;
}
