#include "foundation/freestanding.h"
#include "freestanding_silent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The library never prints, so it links without fdn_hook_write. */
static fdn_context context = FDN_CONTEXT_INIT;
static size_t allocations;
static size_t releases;

fdn_context *fdn_hook_context(void) { return &context; }

void *fdn_hook_alloc(size_t size) {
    ++allocations;
    return malloc(size);
}

void fdn_hook_free(void *value) {
    ++releases;
    free(value);
}

_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location) {
    (void)location;
    printf("panic %.*s\n", (int)message.length, message.data);
    exit(3);
}

int main(void) {
    const fdn_string name = fdn_string_static("world", 5);
    fdn_string greeting = silent_greeting(&name);
    const int matched = greeting.length == 11 && memcmp(greeting.data, "quiet world", 11) == 0;
    fdn_string_drop(&greeting);
    if (!matched || allocations == 0 || allocations != releases) {
        printf("failed: silent greeting\n");
        return 1;
    }
    printf("silent ok\n");
    return 0;
}
