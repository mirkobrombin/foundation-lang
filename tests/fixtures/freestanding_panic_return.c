#include "freestanding_library.h"

#include <stdio.h>
#include <stdlib.h>

/* This harness breaks the hook contract on purpose, so it does not include
   foundation/freestanding.h, which declares fdn_hook_panic as noreturn. The runtime must stop
   the program instead of resuming the panicked frames. */
typedef struct harness_context {
    void *reserved[8];
} harness_context;

static harness_context context;

harness_context *fdn_hook_context(void) { return &context; }

void *fdn_hook_alloc(size_t size) { return malloc(size); }

void fdn_hook_free(void *value) { free(value); }

void fdn_hook_write(const char *data, size_t length) {
    (void)data;
    (void)length;
}

void fdn_hook_panic(fdn_string message, const void *location) {
    (void)location;
    printf("panic hook returned for %.*s\n", (int)message.length, message.data);
    fflush(stdout);
}

int32_t harness_switch_context(int32_t value) { return value; }

int32_t harness_reenter(int32_t value) { return value; }

int main(void) {
    (void)freestanding_nested_panic(1);
    printf("execution resumed after the panic hook returned\n");
    return 0;
}
