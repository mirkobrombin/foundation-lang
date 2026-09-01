#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "../src/bytes_internal.h"

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

static int run_test(int argc, char **argv) {
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
    char tree_root[4096];
    char tree_nested[4096];
    char tree_file[4096];
    char output_root[4096];
    char output_nested[4096];
    char output_file[4096];
#if !defined(_WIN32)
    char private_link[4096];
    char tree_link[4096];
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

    {
        fdn_string relative_directory = fdn_string_static("nested", 6);
        fdn_string relative_file = fdn_string_static("nested/payload.bin", 18);
        fdn_string blocked_directory =
            fdn_string_static("nested/payload.bin/child", 24);
        fdn_string invalid_relative = fdn_string_static("../escape", 9);
        fdn_string tree_path;
        fdn_string output_path;
        fdn_string entry_path = fdn_string_static("", 0);
        fdn_string payload_text = fdn_string_static("binary\0payload", 14);
        uint64_t root = 0;
        uint64_t tree = 0;
        uint64_t payload = foundation_runtime_bytes_from_text(&payload_text);
        uint64_t read_payload = 0;
        uint32_t kind = 0;
        bool executable = false;
        const uint8_t *read_data = NULL;
        size_t read_length = 0;
#if defined(_WIN32)
        const bool expected_executable = false;
#else
        const bool expected_executable = true;
#endif
        int tree_root_length = snprintf(tree_root, sizeof(tree_root), "%s-tree", argv[3]);
        int tree_nested_length = snprintf(tree_nested, sizeof(tree_nested),
                                          "%s/nested", tree_root);
        int tree_file_length = snprintf(tree_file, sizeof(tree_file),
                                        "%s/payload.bin", tree_nested);
        int output_root_length = snprintf(output_root, sizeof(output_root),
                                          "%s-output", argv[3]);
        int output_nested_length = snprintf(output_nested, sizeof(output_nested),
                                            "%s/nested", output_root);
        int output_file_length = snprintf(output_file, sizeof(output_file),
                                          "%s/payload.bin", output_nested);
        if (tree_root_length < 0 || tree_nested_length < 0 || tree_file_length < 0 ||
            output_root_length < 0 || output_nested_length < 0 || output_file_length < 0 ||
            (size_t)tree_root_length >= sizeof(tree_root) ||
            (size_t)tree_nested_length >= sizeof(tree_nested) ||
            (size_t)tree_file_length >= sizeof(tree_file) ||
            (size_t)output_root_length >= sizeof(output_root) ||
            (size_t)output_nested_length >= sizeof(output_nested) ||
            (size_t)output_file_length >= sizeof(output_file) || payload == 0) {
            foundation_runtime_bytes_close(&payload);
            return 34;
        }
        tree_path = fdn_string_static(tree_root, strlen(tree_root));
        output_path = fdn_string_static(output_root, strlen(output_root));
        if (foundation_runtime_fs_root_open(&tree_path, &root) != 0 || root == 0 ||
            foundation_runtime_fs_root_create_directory(root, &relative_directory) != 0 ||
            foundation_runtime_fs_root_create_directory(root, &invalid_relative) != 3 ||
            foundation_runtime_fs_root_write_file(root, &relative_file, payload, 0751) != 0 ||
            foundation_runtime_fs_root_create_directory(root, &blocked_directory) != 3 ||
            foundation_runtime_fs_root_close(&root) != 0 || root != 0) {
            foundation_runtime_bytes_close(&payload);
            return 35;
        }
        if (foundation_runtime_fs_tree_open(&tree_path, 1, 4096, &tree) != 5 || tree != 0 ||
            foundation_runtime_fs_tree_open(&tree_path, 10, 4096, &tree) != 0 || tree == 0 ||
            foundation_runtime_fs_live_directories() != 1) {
            foundation_runtime_bytes_close(&payload);
            return 36;
        }
        if (foundation_runtime_fs_tree_next(tree, &entry_path, &kind, &executable, &size) != 1 ||
            !line_is(entry_path, "nested") || kind != 2 || size != 0) {
            fdn_string_drop(&entry_path);
            foundation_runtime_bytes_close(&payload);
            (void)foundation_runtime_fs_tree_close(&tree);
            return 37;
        }
        if (foundation_runtime_fs_tree_next(tree, &entry_path, &kind, &executable, &size) != 1 ||
            !line_is(entry_path, "nested/payload.bin") || kind != 1 ||
            executable != expected_executable || size != 14 ||
            foundation_runtime_fs_tree_read(tree, &relative_file, 13, &read_payload) != 5 ||
            read_payload != 0 ||
            foundation_runtime_fs_tree_read(tree, &relative_file, 14, &read_payload) != 0 ||
            read_payload == 0 || fdn_bytes_view(read_payload, &read_data, &read_length) != 0 ||
            read_length != 14 || memcmp(read_data, "binary\0payload", 14) != 0 ||
            foundation_runtime_fs_tree_next(tree, &entry_path, &kind, &executable, &size) != 0 ||
            foundation_runtime_fs_tree_close(&tree) != 0 || tree != 0 ||
            foundation_runtime_fs_live_directories() != 0) {
            fdn_string_drop(&entry_path);
            foundation_runtime_bytes_close(&read_payload);
            foundation_runtime_bytes_close(&payload);
            (void)foundation_runtime_fs_tree_close(&tree);
            return 38;
        }
        fdn_string_drop(&entry_path);
        if (foundation_runtime_fs_root_open(&output_path, &root) != 0 || root == 0 ||
            foundation_runtime_fs_root_create_directory(root, &relative_directory) != 0 ||
            foundation_runtime_fs_root_write_file(root, &relative_file, read_payload, 0640) != 0 ||
            foundation_runtime_fs_root_close(&root) != 0 || root != 0) {
            foundation_runtime_bytes_close(&read_payload);
            foundation_runtime_bytes_close(&payload);
            return 39;
        }
        foundation_runtime_bytes_close(&read_payload);
        foundation_runtime_bytes_close(&payload);
#if !defined(_WIN32)
        {
            struct stat info;
            int tree_link_length = snprintf(tree_link, sizeof(tree_link),
                                            "%s/link", tree_root);
            if (tree_link_length < 0 || (size_t)tree_link_length >= sizeof(tree_link) ||
                stat(tree_file, &info) != 0 || (info.st_mode & 0777) != 0751 ||
                stat(output_file, &info) != 0 || (info.st_mode & 0777) != 0640 ||
                symlink("nested", tree_link) != 0 ||
                foundation_runtime_fs_tree_open(&tree_path, 10, 4096, &tree) != 0 ||
                foundation_runtime_fs_tree_next(tree, &entry_path, &kind, &executable, &size) != 1 ||
                !line_is(entry_path, "link") || kind != 3 ||
                foundation_runtime_fs_tree_close(&tree) != 0 || unlink(tree_link) != 0) {
                fdn_string_drop(&entry_path);
                (void)foundation_runtime_fs_tree_close(&tree);
                return 40;
            }
            fdn_string_drop(&entry_path);
        }
#endif
        if (remove(tree_file) != 0 || remove_directory(tree_nested) != 0 ||
            remove_directory(tree_root) != 0 || remove(output_file) != 0 ||
            remove_directory(output_nested) != 0 || remove_directory(output_root) != 0 ||
            foundation_runtime_fs_live_directories() != 0 || fdn_live_allocations() != 0) {
            return 41;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const int status = run_test(argc, argv);
    if (status != 0) {
        (void)fprintf(stderr, "runtime.fs failed at check %d\n", status);
    }
    return status;
}
