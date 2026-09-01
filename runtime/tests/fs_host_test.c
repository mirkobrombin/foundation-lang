#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static fdn_string text(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static int string_is(const fdn_string *value, const char *expected) {
    const size_t length = strlen(expected);
    return value->length == length &&
           (length == 0 || memcmp(value->data, expected, length) == 0);
}

static int join_path(char *result, size_t capacity, const fdn_string *root,
                     const char *suffix) {
    const int length = snprintf(result, capacity, "%.*s/%s", (int)root->length,
                                root->data, suffix);
    return length >= 0 && (size_t)length < capacity;
}

static int run_test(int argc, char **argv) {
    fdn_string parent;
    fdn_string prefix = fdn_string_static("fdn-h\xc3\xb6st", 9);
    fdn_string root = fdn_string_static("", 0);
    fdn_string canonical = fdn_string_static("", 0);
    fdn_string temporary_file = fdn_string_static("", 0);
    fdn_string contents = fdn_string_static("", 0);
    fdn_string payload = fdn_string_static("h\xc3\xa9llo\n", 7);
    fdn_string replacement = fdn_string_static("replacement\n", 12);
    fdn_string invalid = fdn_string_static("\xff", 1);
    char nested[4096];
    char inner[4096];
    char source[4096];
    char copied[4096];
    char moved[4096];
    char replacement_path[4096];
#if !defined(_WIN32)
    char link_path[4096];
    char private_path[4096];
#endif
    fdn_string nested_value;
    fdn_string inner_value;
    fdn_string source_value;
    fdn_string copied_value;
    fdn_string moved_value;
    fdn_string replacement_value;
#if !defined(_WIN32)
    fdn_string private_value;
    mode_t previous_mask;
    struct stat public_info;
    struct stat private_info;
#endif
    bool exists = false;
    bool executable = false;
    uint32_t kind = 0;

    if (argc != 2) {
        return 1;
    }
    parent = text(argv[1]);
    if (foundation_runtime_fs_create_temp_directory(&parent, &prefix, &root) != 0 ||
        root.length == 0 ||
        !join_path(nested, sizeof(nested), &root, "nested") ||
        !join_path(inner, sizeof(inner), &root, "nested/inner") ||
        !join_path(source, sizeof(source), &root, "nested/inner/h\xc3\xa9llo.txt") ||
        !join_path(copied, sizeof(copied), &root, "nested/inner/copied.txt") ||
        !join_path(moved, sizeof(moved), &root, "nested/inner/moved.txt") ||
        !join_path(replacement_path, sizeof(replacement_path), &root,
                   "nested/inner/replacement.txt")
#if !defined(_WIN32)
        || !join_path(private_path, sizeof(private_path), &root,
                      "nested/inner/private.txt")
#endif
    ) {
        fdn_string_drop(&root);
        return 2;
    }
    nested_value = text(nested);
    inner_value = text(inner);
    source_value = text(source);
    copied_value = text(copied);
    moved_value = text(moved);
    replacement_value = text(replacement_path);
#if !defined(_WIN32)
    private_value = text(private_path);
    previous_mask = umask(0022);
#endif
    if (foundation_runtime_fs_create_directory_tree(&inner_value) != 0 ||
        foundation_runtime_fs_kind(&nested_value, &kind) != 0 || kind != 2 ||
        foundation_runtime_fs_write_text_atomic(&source_value, &payload, 6) != 5 ||
        foundation_runtime_fs_write_text_atomic(&source_value, &invalid, 1) != 6 ||
        foundation_runtime_fs_write_text_atomic(&source_value, &payload, 7) != 0 ||
#if !defined(_WIN32)
        foundation_runtime_fs_write_private_text_atomic(&private_value, &payload, 7) != 0 ||
        stat(source, &public_info) != 0 || (public_info.st_mode & 0777) != 0644 ||
        stat(private_path, &private_info) != 0 ||
        (private_info.st_mode & 0777) != 0600 ||
        chmod(source, 0754) != 0 ||
        foundation_runtime_fs_write_text_atomic(&source_value, &payload, 7) != 0 ||
        stat(source, &public_info) != 0 || (public_info.st_mode & 0777) != 0754 ||
#endif
        foundation_runtime_fs_exists(&source_value, &exists) != 0 || !exists ||
        foundation_runtime_fs_kind(&source_value, &kind) != 0 || kind != 1 ||
        foundation_runtime_fs_read_text_sync_limited(&source_value, 7, &contents) != 0 ||
        !string_is(&contents, "h\xc3\xa9llo\n")) {
        fdn_string_drop(&contents);
        fdn_string_drop(&root);
#if !defined(_WIN32)
        (void)umask(previous_mask);
#endif
        return 3;
    }
#if !defined(_WIN32)
    (void)umask(previous_mask);
#endif
    fdn_string_drop(&contents);
    if (foundation_runtime_fs_canonicalize(&source_value, &canonical) != 0 ||
        canonical.length == 0 ||
        foundation_runtime_fs_copy_file(&source_value, &copied_value) != 0 ||
        foundation_runtime_fs_copy_file(&source_value, &copied_value) != 7 ||
        foundation_runtime_fs_rename(&copied_value, &moved_value) != 0 ||
        foundation_runtime_fs_exists(&copied_value, &exists) != 0 || exists ||
        foundation_runtime_fs_write_text_atomic(&replacement_value, &replacement, 12) != 0 ||
        foundation_runtime_fs_rename(&replacement_value, &moved_value) != 7 ||
        foundation_runtime_fs_read_text_sync_limited(&replacement_value, 12, &contents) != 0 ||
        !string_is(&contents, "replacement\n") ||
        foundation_runtime_fs_replace(&replacement_value, &moved_value) != 0 ||
        foundation_runtime_fs_read_text_sync_limited(&moved_value, 12, &contents) != 0 ||
        !string_is(&contents, "replacement\n")) {
        fdn_string_drop(&contents);
        fdn_string_drop(&canonical);
        fdn_string_drop(&root);
        return 4;
    }
    fdn_string_drop(&contents);
#if defined(_WIN32)
    if (foundation_runtime_fs_is_executable(&moved_value, &executable) != 0 ||
        executable || foundation_runtime_fs_set_executable(&moved_value, true) != 10) {
        fdn_string_drop(&canonical);
        fdn_string_drop(&root);
        return 5;
    }
#else
    if (foundation_runtime_fs_set_executable(&moved_value, true) != 0 ||
        foundation_runtime_fs_is_executable(&moved_value, &executable) != 0 ||
        !executable || foundation_runtime_fs_set_executable(&moved_value, false) != 0 ||
        foundation_runtime_fs_is_executable(&moved_value, &executable) != 0 || executable) {
        fdn_string_drop(&canonical);
        fdn_string_drop(&root);
        return 5;
    }
    if (!join_path(link_path, sizeof(link_path), &root, "nested/inner/link") ||
        symlink(moved, link_path) != 0) {
        fdn_string_drop(&canonical);
        fdn_string_drop(&root);
        return 6;
    }
    {
        fdn_string link_value = text(link_path);
        if (foundation_runtime_fs_kind(&link_value, &kind) != 0 || kind != 3 ||
            foundation_runtime_fs_canonicalize(&link_value, &contents) != 3 ||
            foundation_runtime_fs_remove_file(&link_value) != 0) {
            fdn_string_drop(&contents);
            fdn_string_drop(&canonical);
            fdn_string_drop(&root);
            return 7;
        }
    }
#endif
    if (foundation_runtime_fs_create_temp_file(&root, &prefix, &temporary_file) != 0 ||
        temporary_file.length == 0 ||
        foundation_runtime_fs_exists(&temporary_file, &exists) != 0 || !exists ||
        foundation_runtime_fs_remove_file(&temporary_file) != 0 ||
        foundation_runtime_fs_remove_file(&source_value) != 0 ||
#if !defined(_WIN32)
        foundation_runtime_fs_remove_file(&private_value) != 0 ||
#endif
        foundation_runtime_fs_remove_file(&moved_value) != 0 ||
        foundation_runtime_fs_remove_empty_directory(&inner_value) != 0 ||
        foundation_runtime_fs_remove_empty_directory(&nested_value) != 0 ||
        foundation_runtime_fs_remove_empty_directory(&root) != 0) {
        fdn_string_drop(&temporary_file);
        fdn_string_drop(&canonical);
        fdn_string_drop(&root);
        return 8;
    }
    fdn_string_drop(&temporary_file);
    fdn_string_drop(&canonical);
    fdn_string_drop(&root);
    if (fdn_live_allocations() != 0) {
        return 9;
    }
    return 0;
}

int main(int argc, char **argv) {
    const int status = run_test(argc, argv);
    if (status != 0) {
        (void)fprintf(stderr, "runtime.fs-host failed at check %d\n", status);
    }
    return status;
}
