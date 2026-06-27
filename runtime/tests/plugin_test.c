#include "foundation/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    PLUGIN_OK = 0,
    PLUGIN_INVALID_PATH = 1,
    PLUGIN_LOAD_FAILED = 2,
    PLUGIN_MISSING_QUERY = 3,
    PLUGIN_QUERY_FAILED = 4,
    PLUGIN_ABI_MISMATCH = 5,
    PLUGIN_SDK_MISMATCH = 6,
    PLUGIN_TARGET_MISMATCH = 7,
    PLUGIN_CONTRACT_MISMATCH = 8,
    PLUGIN_INVALID_DESCRIPTOR = 9,
    PLUGIN_CREATE_FAILED = 10,
    PLUGIN_LIFECYCLE_FAILED = 11,
};

static fdn_string borrowed(const char *value) {
    return fdn_string_static(value, strlen(value));
}

static int text_is(fdn_string value, const char *expected) {
    return value.length == strlen(expected) &&
           memcmp(value.data, expected, value.length) == 0;
}

static int open_plugin(const char *path, uint64_t *handle, fdn_string *name,
                       fdn_string *detail) {
    const fdn_string source = borrowed(path);
    return foundation_runtime_plugin_open(&source, handle, name, detail);
}

static int open_failure(const char *path, int expected_status,
                        const char *expected_detail) {
    uint64_t handle = 0;
    fdn_string name = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    const int status = open_plugin(path, &handle, &name, &detail);
    const int matches = status == expected_status && handle == 0 &&
                        text_is(detail, expected_detail) &&
                        foundation_runtime_plugin_live_handles() == 0;
    fdn_string_drop(&name);
    fdn_string_drop(&detail);
    return matches;
}

int main(int argc, char **argv) {
    const size_t baseline = fdn_live_allocations();
    uint64_t handle = 0;
    fdn_string name = fdn_string_static("", 0);
    fdn_string detail = fdn_string_static("", 0);
    fdn_string empty = fdn_string_static("", 0);

    if (argc != 11) {
        fputs("plugin fixture paths are required\n", stderr);
        return 1;
    }
    if (foundation_runtime_plugin_open(&empty, &handle, &name, &detail) !=
            PLUGIN_INVALID_PATH ||
        handle != 0 || !text_is(detail, "invalid plugin path")) {
        return 2;
    }
    fdn_string_drop(&detail);
    if (open_plugin("foundation-plugin-missing-file", &handle, &name, &detail) !=
            PLUGIN_LOAD_FAILED ||
        handle != 0) {
        return 3;
    }
    fdn_string_drop(&detail);
    if (open_plugin(argv[2], &handle, &name, &detail) != PLUGIN_MISSING_QUERY ||
        handle != 0) {
        return 4;
    }
    fdn_string_drop(&detail);
    if (open_plugin(argv[3], &handle, &name, &detail) != PLUGIN_ABI_MISMATCH ||
        handle != 0) {
        return 5;
    }
    fdn_string_drop(&detail);
    if (!open_failure(argv[5], PLUGIN_QUERY_FAILED, "fixture query rejected")) {
        return 13;
    }
    if (!open_failure(argv[6], PLUGIN_SDK_MISMATCH, "plugin SDK mismatch")) {
        return 14;
    }
    if (!open_failure(argv[7], PLUGIN_TARGET_MISMATCH, "plugin target mismatch")) {
        return 15;
    }
    if (!open_failure(argv[8], PLUGIN_CONTRACT_MISMATCH,
                      "plugin contract mismatch")) {
        return 16;
    }
    if (!open_failure(argv[9], PLUGIN_INVALID_DESCRIPTOR,
                      "invalid plugin descriptor")) {
        return 17;
    }
    if (!open_failure(argv[10], PLUGIN_CREATE_FAILED,
                      "fixture creation rejected")) {
        return 18;
    }
    if (open_plugin(argv[1], &handle, &name, &detail) != PLUGIN_OK || handle == 0 ||
        !text_is(name, "sample-native") ||
        foundation_runtime_plugin_live_handles() != 1) {
        return 6;
    }
    if (foundation_runtime_plugin_start(handle, &detail) != PLUGIN_OK ||
        foundation_runtime_plugin_start(handle, &detail) != PLUGIN_OK ||
        foundation_runtime_plugin_stop(handle, &detail) != PLUGIN_OK ||
        foundation_runtime_plugin_stop(handle, &detail) != PLUGIN_OK) {
        return 7;
    }
    if (foundation_runtime_plugin_close(&handle, &detail) != PLUGIN_OK ||
        handle != 0 || foundation_runtime_plugin_live_handles() != 0) {
        return 8;
    }
    fdn_string_drop(&name);
    fdn_string_drop(&detail);

    if (open_plugin(argv[4], &handle, &name, &detail) != PLUGIN_OK ||
        !text_is(name, "failing-native")) {
        return 9;
    }
    if (foundation_runtime_plugin_start(handle, &detail) !=
            PLUGIN_LIFECYCLE_FAILED ||
        !text_is(detail, "fixture start rejected")) {
        return 10;
    }
    if (foundation_runtime_plugin_close(&handle, &detail) != PLUGIN_OK) {
        return 11;
    }
    fdn_string_drop(&name);
    fdn_string_drop(&detail);
    if (foundation_runtime_plugin_live_handles() != 0 ||
        fdn_live_allocations() != baseline) {
        return 12;
    }
    return 0;
}
