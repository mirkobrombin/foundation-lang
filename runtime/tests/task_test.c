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

typedef struct parent_frame {
    fdn_task *child;
    int32_t result;
    int *parent_polls;
    int *child_polls;
    bool *parent_cancellation_seen;
    bool *child_cancellation_seen;
} parent_frame;

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

static fdn_task_poll poll_parent(void *raw, bool cancellation_requested) {
    parent_frame *frame = raw;
    int32_t child_result;
    ++*frame->parent_polls;
    if (frame->parent_cancellation_seen != NULL) {
        *frame->parent_cancellation_seen = cancellation_requested;
    }
    if (frame->child == NULL) {
        frame->child = spawn_test(20, frame->child_polls, true,
                                  frame->child_cancellation_seen);
    }
    if (!fdn_task_poll_wait(&frame->child, &child_result)) {
        return FDN_TASK_PENDING;
    }
    frame->result = child_result + 2;
    return FDN_TASK_READY;
}

static void move_parent_result(void *raw, void *output) {
    parent_frame *frame = raw;
    *(int32_t *)output = frame->result;
}

static void drop_parent_frame(void *raw) {
    parent_frame *frame = raw;
    fdn_task_drop(&frame->child);
    fdn_dealloc(frame);
}

static fdn_task *spawn_parent(int *parent_polls, int *child_polls,
                              bool *parent_cancellation_seen,
                              bool *child_cancellation_seen) {
    parent_frame *frame = fdn_alloc(sizeof(*frame));
    frame->child = NULL;
    frame->result = 0;
    frame->parent_polls = parent_polls;
    frame->child_polls = child_polls;
    frame->parent_cancellation_seen = parent_cancellation_seen;
    frame->child_cancellation_seen = child_cancellation_seen;
    return fdn_task_spawn(frame, poll_parent, move_parent_result, drop_parent_frame);
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

    int parent_polls = 0;
    int child_polls = 0;
    fdn_task *parent = spawn_parent(&parent_polls, &child_polls, NULL, NULL);
    fdn_task_wait(&parent, &result);
    assert(parent == NULL);
    assert(result == 42);
    assert(parent_polls == 2);
    assert(child_polls == 2);
    assert(fdn_task_live_count() == 0);

    bool parent_cancellation_seen = false;
    bool child_cancellation_seen = false;
    fdn_task *cancelled_parent = spawn_parent(&parent_polls, &child_polls,
                                               &parent_cancellation_seen,
                                               &child_cancellation_seen);
    fdn_task_drop(&cancelled_parent);
    assert(cancelled_parent == NULL);
    assert(parent_cancellation_seen);
    assert(child_cancellation_seen);
    assert(fdn_task_live_count() == 0);

    int supervised_polls = 0;
    uint64_t supervisor = foundation_runtime_supervisor_open();
    fdn_task *supervised_first = spawn_test(3, &supervised_polls, true, NULL);
    fdn_task *supervised_second = spawn_test(4, &supervised_polls, false, NULL);
    foundation_runtime_supervisor_adopt(supervisor, supervised_first);
    foundation_runtime_supervisor_adopt(supervisor, supervised_second);
    assert(foundation_runtime_supervisor_live_count() == 1);
    foundation_runtime_supervisor_wait(supervisor);
    foundation_runtime_supervisor_release(supervisor);
    assert(supervised_polls == 3);
    assert(foundation_runtime_supervisor_live_count() == 0);
    assert(fdn_task_live_count() == 0);

    bool supervised_cancellation_seen = false;
    supervisor = foundation_runtime_supervisor_open();
    fdn_task *supervised_cancelled =
        spawn_test(5, &supervised_polls, true, &supervised_cancellation_seen);
    foundation_runtime_supervisor_adopt(supervisor, supervised_cancelled);
    foundation_runtime_supervisor_cancel(supervisor);
    foundation_runtime_supervisor_release(supervisor);
    assert(supervised_cancellation_seen);
    assert(foundation_runtime_supervisor_live_count() == 0);
    assert(fdn_task_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
