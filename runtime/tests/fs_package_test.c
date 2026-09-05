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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if !defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static fdn_string text(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static int join_path(char *result, size_t capacity, const fdn_string *root,
                     const char *suffix) {
    const int length = snprintf(result, capacity, "%.*s/%s", (int)root->length,
                                root->data, suffix);
    return length >= 0 && (size_t)length < capacity;
}

#if defined(_WIN32)
static wchar_t *wide_path(const char *value) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           value, -1, NULL, 0);
    wchar_t *result;
    if (length == 0) {
        return NULL;
    }
    result = malloc((size_t)length * sizeof(*result));
    if (result == NULL ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                            result, length) != length) {
        free(result);
        return NULL;
    }
    return result;
}
#endif

static int create_links(const char *file_link, const char *file_target,
                        const char *directory_link,
                        const char *directory_target) {
#if defined(_WIN32)
    wchar_t *wide_file_link = wide_path(file_link);
    wchar_t *wide_file_target = wide_path(file_target);
    wchar_t *wide_directory_link = wide_path(directory_link);
    wchar_t *wide_directory_target = wide_path(directory_target);
    int created_file = 0;
    int created_directory = 0;
    if (wide_file_link != NULL && wide_file_target != NULL &&
        wide_directory_link != NULL && wide_directory_target != NULL) {
        created_file = CreateSymbolicLinkW(
            wide_file_link, wide_file_target,
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
        created_directory = CreateSymbolicLinkW(
            wide_directory_link, wide_directory_target,
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
    }
    if (created_file != 0 && created_directory == 0) {
        (void)DeleteFileW(wide_file_link);
    }
    if (created_directory != 0 && created_file == 0) {
        (void)RemoveDirectoryW(wide_directory_link);
    }
    free(wide_file_link);
    free(wide_file_target);
    free(wide_directory_link);
    free(wide_directory_target);
    return created_file != 0 && created_directory != 0;
#else
    if (symlink(file_target, file_link) != 0) {
        return 0;
    }
    if (symlink(directory_target, directory_link) != 0) {
        (void)unlink(file_link);
        return 0;
    }
    return 1;
#endif
}

static int filesystem_root_is_rejected(const fdn_string *path) {
#if defined(_WIN32)
    char volume[4096];
    size_t length = 0;
    size_t offset;
    unsigned int components = 0;
    fdn_string root;
    if (path->length >= 3 && path->data[1] == ':') {
        volume[0] = path->data[0];
        volume[1] = ':';
        volume[2] = '\\';
        length = 3;
    } else if (path->length >= 2 &&
               (path->data[0] == '\\' || path->data[0] == '/') &&
               (path->data[1] == '\\' || path->data[1] == '/')) {
        offset = 2;
        if (path->length >= 8 && path->data[2] == '?' &&
            path->data[3] == '\\' &&
            (path->data[4] == 'U' || path->data[4] == 'u') &&
            (path->data[5] == 'N' || path->data[5] == 'n') &&
            (path->data[6] == 'C' || path->data[6] == 'c') &&
            path->data[7] == '\\') {
            offset = 8;
        }
        while (offset < path->length && components < 2) {
            while (offset < path->length &&
                   (path->data[offset] == '\\' || path->data[offset] == '/')) {
                ++offset;
            }
            if (offset == path->length) {
                break;
            }
            ++components;
            while (offset < path->length && path->data[offset] != '\\' &&
                   path->data[offset] != '/') {
                ++offset;
            }
        }
        if (components != 2) {
            return 0;
        }
        length = offset;
        if (length >= sizeof(volume)) {
            return 0;
        }
        (void)memcpy(volume, path->data, length);
    } else {
        return 0;
    }
    volume[length] = '\0';
    root = text(volume);
    return foundation_runtime_fs_remove_tree(&root, 1, 1) == 3;
#else
    fdn_string root = fdn_string_static("/", 1);
    (void)path;
    return foundation_runtime_fs_remove_tree(&root, 1, 1) == 3;
#endif
}

static int run_test(int argc, char **argv) {
    static const uint8_t payload_data[] = {0, 0xff, 'F', '\n'};
    static const uint8_t patched_data[] = {0, 0xff, 'o', 'k'};
    fdn_string parent;
    fdn_string prefix = fdn_string_static("fdn-package", 11);
    fdn_string root = fdn_string_static("", 0);
    fdn_string outside = fdn_string_static("", 0);
    fdn_string nested = fdn_string_static("nested/deep", 11);
    fdn_string single = fdn_string_static("single", 6);
    fdn_string payload_relative = fdn_string_static("nested/deep/payload.bin", 23);
    fdn_string rename_source = fdn_string_static("rename-source.bin", 17);
    fdn_string rename_conflict = fdn_string_static("rename-conflict.bin", 19);
    fdn_string rename_destination = fdn_string_static("single/renamed.bin", 18);
    fdn_string keep_relative = fdn_string_static("keep.bin", 8);
    fdn_string file_link_relative = fdn_string_static("payload-link", 12);
    fdn_string payload_value = fdn_string_static(
        (const char *)payload_data, sizeof(payload_data));
    fdn_string patch_value = fdn_string_static("ok", 2);
    uint64_t payload = 0;
    uint64_t patch = 0;
    uint64_t root_writer = 0;
    uint64_t outside_writer = 0;
    uint64_t read = 0;
    uint64_t file = 0;
    uint64_t chunk = 0;
    uint64_t size = 0;
    const uint8_t *read_data = NULL;
    size_t read_length = 0;
    char payload_path[4096];
    char exclusive_path[4096];
    char single_path[4096];
    char keep_path[4096];
    char outside_path[4096];
    char file_link[4096];
    char directory_link[4096];
    char alias_path[4096];
    fdn_string payload_path_value;
    fdn_string exclusive_path_value;
    fdn_string file_link_value;
    fdn_string alias_path_value;
    bool exists = false;
    int links_created;
#if !defined(_WIN32)
    mode_t previous_mask;
    struct stat single_info;
#endif
    if (argc != 2) {
        return 1;
    }
    parent = text(argv[1]);
    payload = foundation_runtime_bytes_from_text(&payload_value);
    patch = foundation_runtime_bytes_from_text(&patch_value);
    if (payload == 0 || patch == 0 ||
        foundation_runtime_fs_create_temp_directory(&parent, &prefix, &root) != 0 ||
        foundation_runtime_fs_create_temp_directory(&parent, &prefix, &outside) != 0 ||
        !join_path(payload_path, sizeof(payload_path), &root,
                   "nested/deep/payload.bin") ||
        !join_path(exclusive_path, sizeof(exclusive_path), &root,
                   "exclusive.bin") ||
        !join_path(single_path, sizeof(single_path), &root, "single") ||
        !join_path(file_link, sizeof(file_link), &root, "payload-link") ||
        !join_path(directory_link, sizeof(directory_link), &root, "outside-link") ||
        !join_path(alias_path, sizeof(alias_path), &root,
                   "nested/deep/../..") ||
        !join_path(keep_path, sizeof(keep_path), &outside, "keep.bin") ||
        snprintf(outside_path, sizeof(outside_path), "%.*s",
                 (int)outside.length, outside.data) < 0 ||
        strlen(outside_path) != outside.length) {
        return 2;
    }
    payload_path_value = text(payload_path);
    exclusive_path_value = text(exclusive_path);
    file_link_value = text(file_link);
    alias_path_value = text(alias_path);
    if (foundation_runtime_fs_root_open(&root, &root_writer) != 0 ||
        foundation_runtime_fs_root_create_directory(root_writer, &nested) != 0) {
        return 3;
    }
#if !defined(_WIN32)
    previous_mask = umask(0077);
    if (foundation_runtime_fs_root_create_directory_entry(
            root_writer, &single, 488) != 0) {
        (void)umask(previous_mask);
        return 3;
    }
    (void)umask(previous_mask);
    if (stat(single_path, &single_info) != 0 ||
        (single_info.st_mode & 0777) != 0750) {
        return 3;
    }
#else
    if (foundation_runtime_fs_root_create_directory_entry(
            root_writer, &single, 488) != 0) {
        return 3;
    }
#endif
    if (
        foundation_runtime_fs_root_create_directory_entry(
            root_writer, &single, 488) != 7 ||
        foundation_runtime_fs_root_write_file(root_writer, &payload_relative,
                                              payload, 416) != 0 ||
        foundation_runtime_fs_root_write_file(root_writer, &rename_source,
                                              payload, 416) != 0 ||
        foundation_runtime_fs_root_rename(root_writer, &rename_source,
                                          &rename_destination) != 0 ||
        foundation_runtime_fs_root_write_file(root_writer, &rename_conflict,
                                              payload, 416) != 0 ||
        foundation_runtime_fs_root_rename(root_writer, &rename_conflict,
                                          &rename_destination) != 7 ||
        foundation_runtime_fs_root_open(&outside, &outside_writer) != 0 ||
        foundation_runtime_fs_root_write_file(outside_writer, &keep_relative,
                                              payload, 384) != 0 ||
        foundation_runtime_fs_root_close(&outside_writer) != 0) {
        return 3;
    }
    if (foundation_runtime_fs_read_bytes_sync_limited(
            &payload_path_value, sizeof(payload_data) - 1, &read) != 5 ||
        read != 0 ||
        foundation_runtime_fs_read_bytes_sync_limited(
            &payload_path_value, sizeof(payload_data), &read) != 0 ||
        fdn_bytes_view(read, &read_data, &read_length) != 0 ||
        read_length != sizeof(payload_data) ||
        memcmp(read_data, payload_data, sizeof(payload_data)) != 0) {
        return 4;
    }
    foundation_runtime_bytes_close(&read);
    if (foundation_runtime_fs_file_open(&payload_path_value, 1, &file) != 0 ||
        foundation_runtime_fs_file_size(file, &size) != 0 ||
        size != sizeof(payload_data) ||
        foundation_runtime_fs_file_read(file, 2, &chunk) != 0 ||
        fdn_bytes_view(chunk, &read_data, &read_length) != 0 ||
        read_length != 2 || memcmp(read_data, payload_data, 2) != 0) {
        return 5;
    }
    foundation_runtime_bytes_close(&chunk);
    if (foundation_runtime_fs_file_seek(file, 2) != 0 ||
        foundation_runtime_fs_file_read(file, 8, &chunk) != 0 ||
        fdn_bytes_view(chunk, &read_data, &read_length) != 0 ||
        read_length != 2 || memcmp(read_data, payload_data + 2, 2) != 0) {
        return 6;
    }
    foundation_runtime_bytes_close(&chunk);
    if (foundation_runtime_fs_file_read(file, 8, &chunk) != 0 || chunk != 0 ||
        foundation_runtime_fs_file_close(&file) != 0 || file != 0 ||
        foundation_runtime_fs_file_close(&file) != 0) {
        return 7;
    }
    if (foundation_runtime_fs_file_open(&payload_path_value, 2, &file) != 0 ||
        foundation_runtime_fs_file_seek(file, 2) != 0 ||
        foundation_runtime_fs_file_write(file, patch) != 0 ||
        foundation_runtime_fs_file_resize(file, 6) != 0 ||
        foundation_runtime_fs_file_write(file, patch) != 0 ||
        foundation_runtime_fs_file_size(file, &size) != 0 || size != 6 ||
        foundation_runtime_fs_file_resize(file, sizeof(patched_data)) != 0 ||
        foundation_runtime_fs_file_size(file, &size) != 0 ||
        size != sizeof(patched_data) ||
        foundation_runtime_fs_file_sync(file) != 0 ||
        foundation_runtime_fs_file_close(&file) != 0 ||
        foundation_runtime_fs_read_bytes_sync_limited(
            &payload_path_value, sizeof(patched_data), &read) != 0 ||
        fdn_bytes_view(read, &read_data, &read_length) != 0 ||
        read_length != sizeof(patched_data) ||
        memcmp(read_data, patched_data, sizeof(patched_data)) != 0) {
        return 8;
    }
    foundation_runtime_bytes_close(&read);
    if (foundation_runtime_fs_file_open(&exclusive_path_value, 4, &file) != 0 ||
        foundation_runtime_fs_file_close(&file) != 0 ||
        foundation_runtime_fs_file_open(&exclusive_path_value, 4, &file) != 7 ||
        file != 0) {
        return 9;
    }
    if (foundation_runtime_fs_file_open(&payload_path_value, 3, &file) != 0 ||
        foundation_runtime_fs_file_write(file, payload) != 0 ||
        foundation_runtime_fs_file_sync(file) != 0 ||
        foundation_runtime_fs_file_close(&file) != 0 ||
        foundation_runtime_fs_root_file_open(root_writer, &payload_relative,
                                             1, &file) != 0 ||
        foundation_runtime_fs_file_read(file, sizeof(payload_data), &chunk) != 0 ||
        fdn_bytes_view(chunk, &read_data, &read_length) != 0 ||
        read_length != sizeof(payload_data) ||
        memcmp(read_data, payload_data, sizeof(payload_data)) != 0 ||
        foundation_runtime_fs_file_close(&file) != 0) {
        return 10;
    }
    foundation_runtime_bytes_close(&chunk);
    {
        fdn_string invalid_relative = fdn_string_static("../outside", 10);
        if (foundation_runtime_fs_root_file_open(
                root_writer, &invalid_relative, 1, &file) != 3 ||
            file != 0) {
            return 11;
        }
    }
    if (foundation_runtime_fs_remove_tree(&alias_path_value, 64, 8) != 3 ||
        foundation_runtime_fs_remove_tree(&root, 64, 129) != 5 ||
        !filesystem_root_is_rejected(&root) ||
        foundation_runtime_fs_exists(&payload_path_value, &exists) != 0 ||
        !exists) {
        return 12;
    }
    links_created = create_links(file_link, keep_path, directory_link,
                                 outside_path);
#if !defined(_WIN32)
    if (links_created == 0) {
        return 13;
    }
#endif
    if (links_created != 0 &&
        (foundation_runtime_fs_read_bytes_sync_limited(
             &file_link_value, sizeof(payload_data), &read) != 3 ||
         read != 0 ||
         foundation_runtime_fs_root_file_open(root_writer, &file_link_relative,
                                              1, &file) != 3 ||
         file != 0)) {
        return 14;
    }
    if (foundation_runtime_fs_root_close(&root_writer) != 0) {
        return 15;
    }
    if (foundation_runtime_fs_remove_tree(&root, 1, 8) != 5 ||
        foundation_runtime_fs_exists(&root, &exists) != 0 || !exists ||
        foundation_runtime_fs_remove_tree(&root, 64, 8) != 0 ||
        foundation_runtime_fs_exists(&root, &exists) != 0 || exists) {
        return 16;
    }
    {
        fdn_string keep_value = text(keep_path);
        if (foundation_runtime_fs_exists(&keep_value, &exists) != 0 || !exists ||
            foundation_runtime_fs_remove_tree(&outside, 8, 4) != 0) {
            return 17;
        }
    }
    foundation_runtime_bytes_close(&payload);
    foundation_runtime_bytes_close(&patch);
    fdn_string_drop(&root);
    fdn_string_drop(&outside);
    return fdn_live_allocations() == 0 ? 0 : 18;
}

int main(int argc, char **argv) {
    const int status = run_test(argc, argv);
    if (status != 0) {
        (void)fprintf(stderr, "runtime.fs-package failed at check %d\n", status);
    }
    return status;
}
