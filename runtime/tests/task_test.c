#include "foundation/runtime.h"

#include <assert.h>
#include <stdint.h>

typedef struct test_frame {
    int32_t input;
    int32_t result;
    int *polls;
    bool yield_once;
    bool yielded;
    bool *cancellation_seen;
} test_frame;

static fdn_task_poll poll_test(void *raw, bool cancellation_requested) {
    test_frame *frame = raw;
    ++*frame->polls;
    if (frame->cancellation_seen != NULL) {
        *frame->cancellation_seen = cancellation_requested;
    }
    if (frame->yield_once && !frame->yielded && !cancellation_requested) {
        frame->yielded = true;
        return FDN_TASK_PENDING;
    }
    frame->result = frame->input * 2;
    return FDN_TASK_READY;
}

static void move_test_result(void *raw, void *output) {
    test_frame *frame = raw;
    *(int32_t *)output = frame->result;
}

static void drop_test_frame(void *raw) { fdn_dealloc(raw); }

static fdn_task *spawn_test(int32_t input, int *polls, bool yield_once,
                            bool *cancellation_seen) {
    test_frame *frame = fdn_alloc(sizeof(*frame));
    frame->input = input;
    frame->result = 0;
    frame->polls = polls;
    frame->yield_once = yield_once;
    frame->yielded = false;
    frame->cancellation_seen = cancellation_seen;
    return fdn_task_spawn(frame, poll_test, move_test_result, drop_test_frame);
}

int main(void) {
    int polls = 0;
    int32_t result = 0;
    bool cancellation_seen = false;
    fdn_task *first = spawn_test(21, &polls, true, NULL);
    fdn_task *second = spawn_test(5, &polls, false, NULL);

    assert(polls == 0);
    assert(fdn_task_live_count() == 2);
    fdn_task_wait(&first, &result);
    assert(first == NULL);
    assert(result == 42);
    assert(polls == 3);
    assert(fdn_task_live_count() == 1);

    fdn_task_drop(&second);
    assert(second == NULL);
    assert(fdn_task_live_count() == 0);

    fdn_task *cancelled = spawn_test(8, &polls, true, &cancellation_seen);
    fdn_task_drop(&cancelled);
    assert(cancelled == NULL);
    assert(cancellation_seen);
    assert(fdn_task_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
