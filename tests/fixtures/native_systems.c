#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct native_pair {
    uint32_t left;
    uint32_t right;
};

void foundation_native_fill(struct native_pair *pair) {
    pair->left = 40;
    pair->right = 2;
}

uint32_t foundation_native_read_pair(const struct native_pair *pair) {
    return pair->left + pair->right;
}

static int32_t native_increment(int32_t value) { return value + 1; }

void *foundation_native_symbol(void) {
    void *result = NULL;
    int32_t (*callback)(int32_t) = native_increment;
    _Static_assert(sizeof(result) == sizeof(callback),
                   "fixture requires matching pointer representations");
    memcpy(&result, &callback, sizeof(result));
    return result;
}

bool foundation_native_check_cstring(const uint8_t *value) {
    return strcmp((const char *)value, "foundation") == 0;
}
