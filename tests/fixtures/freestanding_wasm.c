#include "foundation/freestanding.h"
#include "freestanding_silent.h"

/* A WebAssembly module without WASI: the integrator provides the hooks and the memory
   primitives that C compilers may call. */
static _Alignas(16) unsigned char arena[1 << 16];
static size_t arena_used;
static fdn_context context = FDN_CONTEXT_INIT;

fdn_context *fdn_hook_context(void) { return &context; }

void *fdn_hook_alloc(size_t size) {
    const size_t rounded = (size + 15u) & ~(size_t)15u;
    void *value;
    if (rounded > sizeof(arena) - arena_used) {
        return NULL;
    }
    value = arena + arena_used;
    arena_used += rounded;
    return value;
}

void fdn_hook_free(void *value) { (void)value; }

_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location) {
    (void)message;
    (void)location;
    __builtin_trap();
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *target = destination;
    const unsigned char *origin = source;
    while (length-- != 0) {
        *target++ = *origin++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *target = destination;
    const unsigned char *origin = source;
    if (target < origin) {
        while (length-- != 0) {
            *target++ = *origin++;
        }
    } else {
        while (length-- != 0) {
            target[length] = origin[length];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *target = destination;
    while (length-- != 0) {
        *target++ = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *first = left;
    const unsigned char *second = right;
    for (; length != 0; --length, ++first, ++second) {
        if (*first != *second) {
            return *first < *second ? -1 : 1;
        }
    }
    return 0;
}

/* Returns the byte length of the greeting, which the caller compares with "quiet wasm". */
__attribute__((export_name("run"))) int32_t run(void) {
    const fdn_string name = fdn_string_static("wasm", 4);
    fdn_string greeting = silent_greeting(&name);
    const int32_t length = (int32_t)greeting.length;
    fdn_string_drop(&greeting);
    return length;
}
