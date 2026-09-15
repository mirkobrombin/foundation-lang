#include "foundation/freestanding.h"
#include "freestanding_library.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { iterations = 2000 };

/* Each harness thread owns a context. The context hook returns the calling thread's context
   from harness-owned thread-local storage. */
static _Thread_local fdn_context *current;
static atomic_size_t allocations;
static atomic_size_t releases;
static fdn_context first_context = FDN_CONTEXT_INIT;
static fdn_context second_context = FDN_CONTEXT_INIT;
static jmp_buf recovery;
static int nested_panics;
static int panicked_on_second;
static char panic_function[64];

fdn_context *fdn_hook_context(void) { return current; }

void *fdn_hook_alloc(size_t size) {
    void *value = malloc(size);
    if (value != NULL) {
        atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
    }
    return value;
}

void fdn_hook_free(void *value) {
    atomic_fetch_add_explicit(&releases, 1, memory_order_relaxed);
    free(value);
}

void fdn_hook_write(const char *data, size_t length) {
    (void)data;
    (void)length;
}

_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location) {
    panicked_on_second = current == &second_context;
    if (location != NULL && location->function_name != NULL) {
        snprintf(panic_function, sizeof(panic_function), "%s", location->function_name);
    }
    printf("panic %.*s in %s\n", (int)message.length, message.data, panic_function);
    fflush(stdout);
    longjmp(recovery, 1);
}

/* Switches the current context before calling back into Foundation, then restores it. */
int32_t harness_switch_context(int32_t value) {
    fdn_context *previous = current;
    int32_t result;
    current = &second_context;
    result = nested_panics ? freestanding_nested_panic(value) : freestanding_nested(value);
    current = previous;
    return result;
}

/* Calls back into Foundation on the same context. */
int32_t harness_reenter(int32_t value) { return freestanding_nested(value); }

static void *worker(void *argument) {
    const char *label = argument;
    const size_t length = strlen(label);
    fdn_context context = FDN_CONTEXT_INIT;
    char expected[32];
    int index;
    current = &context;
    snprintf(expected, sizeof(expected), "hello %s", label);
    for (index = 0; index < iterations; ++index) {
        const fdn_string name = fdn_string_static(label, length);
        fdn_string greeting = freestanding_greeting(&name);
        const int matched = greeting.length == strlen(expected) &&
                            memcmp(greeting.data, expected, greeting.length) == 0;
        fdn_string_drop(&greeting);
        if (!matched || freestanding_nested(index) != index * 2) {
            return argument;
        }
    }
    return NULL;
}

static int fail(const char *message) {
    printf("failed: %s\n", message);
    return 1;
}

int main(void) {
    pthread_t first;
    pthread_t second;
    void *first_result = NULL;
    void *second_result = NULL;
    if (pthread_create(&first, NULL, worker, "first") != 0 ||
        pthread_create(&second, NULL, worker, "second") != 0) {
        return fail("thread start");
    }
    pthread_join(first, &first_result);
    pthread_join(second, &second_result);
    if (first_result != NULL || second_result != NULL) {
        return fail("concurrent results");
    }
    if (atomic_load(&allocations) == 0 || atomic_load(&allocations) != atomic_load(&releases)) {
        return fail("live allocations");
    }

    current = &first_context;
    if (freestanding_outer(5) != 11) {
        return fail("context switch");
    }
    if (freestanding_reenter(4) != 9) {
        return fail("re-entry");
    }
    nested_panics = 1;
    if (setjmp(recovery) == 0) {
        (void)freestanding_outer(1);
        return fail("nested panic returned");
    }
    /* Both contexts abandoned frames when the hook transferred control. */
    current = &first_context;
    fdn_context_init(&first_context);
    fdn_context_init(&second_context);
    if (!panicked_on_second) {
        return fail("panic context");
    }
    nested_panics = 0;
    if (freestanding_outer(5) != 11) {
        return fail("reinitialized contexts");
    }
    printf("contexts ok: panic location %s\n", panic_function);
    return 0;
}
