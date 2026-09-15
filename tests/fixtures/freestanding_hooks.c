#include "foundation/freestanding.h"
#include "freestanding_library.h"

#include <setjmp.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hooks run over a fixed bump arena. Storage is never reused, so the counters alone prove
   that every allocation was released. */
static alignas(max_align_t) unsigned char arena[1u << 16];
static size_t arena_used;
static size_t allocations;
static size_t releases;
static int exhausted;
static fdn_context context = FDN_CONTEXT_INIT;
static jmp_buf recovery;

struct write_call {
    char data[16];
    size_t length;
};

static struct write_call writes[4];
static size_t write_count;

fdn_context *fdn_hook_context(void) { return &context; }

void *fdn_hook_alloc(size_t size) {
    const size_t alignment = alignof(max_align_t);
    const size_t rounded = (size + alignment - 1) / alignment * alignment;
    void *value;
    if (exhausted || size == 0 || rounded > sizeof(arena) - arena_used) {
        return NULL;
    }
    value = arena + arena_used;
    arena_used += rounded;
    ++allocations;
    return value;
}

void fdn_hook_free(void *value) {
    if (value == NULL) {
        abort();
    }
    ++releases;
}

void fdn_hook_write(const char *data, size_t length) {
    if (write_count < sizeof(writes) / sizeof(writes[0]) && length <= sizeof(writes[0].data)) {
        memcpy(writes[write_count].data, data, length);
        writes[write_count].length = length;
    }
    ++write_count;
}

_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location) {
    printf("panic %.*s", (int)message.length, message.data);
    if (location != NULL && location->package_name == NULL) {
        printf(" at [native] %s (%s:%u)", location->function_name, location->source_file,
               (unsigned int)location->line);
    } else if (location != NULL) {
        printf(" at %s.%s (%s:%u)", location->package_name, location->function_name,
               location->source_file, (unsigned int)location->line);
    }
    printf("\n");
    fflush(stdout);
    longjmp(recovery, 1);
}

int32_t harness_switch_context(int32_t value) { return value; }

int32_t harness_reenter(int32_t value) { return value; }

static int fail(const char *message) {
    printf("failed: %s\n", message);
    return 1;
}

static int run_hooks(void) {
    const fdn_string name = fdn_string_static("world", 5);
    fdn_string greeting = freestanding_greeting(&name);
    if (greeting.length != 11 || memcmp(greeting.data, "hello world", 11) != 0) {
        return fail("greeting");
    }
    fdn_string_drop(&greeting);
    if (allocations == 0 || allocations != releases) {
        return fail("allocation balance");
    }
    freestanding_print();
    if (write_count != 2 || writes[0].length != 5 || memcmp(writes[0].data, "hello", 5) != 0 ||
        writes[1].length != 1 || writes[1].data[0] != '\n') {
        return fail("print writes");
    }
    printf("hooks ok: %zu allocations, %zu releases, %zu writes\n", allocations, releases,
           write_count);
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "hooks";
    if (strcmp(mode, "hooks") == 0) {
        return run_hooks();
    }
    if (setjmp(recovery) != 0) {
        fdn_context_init(&context);
        return 0;
    }
    if (strcmp(mode, "index") == 0) {
        (void)freestanding_index(7);
        return fail("out-of-bounds index returned");
    }
    if (strcmp(mode, "allocation") == 0) {
        const fdn_string name = fdn_string_static("world", 5);
        exhausted = 1;
        (void)freestanding_greeting(&name);
        return fail("exhausted allocation returned");
    }
    return fail("unknown mode");
}
