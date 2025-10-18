#include "foundation/runtime.h"

#include <stdlib.h>

struct fdn_task {
    struct fdn_task *next;
    void *frame;
    fdn_task_poll_fn poll;
    fdn_task_move_result_fn move_result;
    fdn_task_drop_frame_fn drop_frame;
    bool cancellation_requested;
    bool queued;
    bool ready;
};

#if defined(_MSC_VER)
#define FDN_THREAD_LOCAL __declspec(thread)
#else
#define FDN_THREAD_LOCAL _Thread_local
#endif

static FDN_THREAD_LOCAL fdn_task *fdn_task_queue_head;
static FDN_THREAD_LOCAL fdn_task *fdn_task_queue_tail;
static FDN_THREAD_LOCAL size_t fdn_task_count;
static FDN_THREAD_LOCAL bool fdn_task_current_cancellation;

static void fdn_task_enqueue(fdn_task *task) {
    if (task->queued || task->ready) {
        fdn_panic_cstr("invalid task enqueue");
    }
    task->next = NULL;
    task->queued = true;
    if (fdn_task_queue_tail == NULL) {
        fdn_task_queue_head = task;
    } else {
        fdn_task_queue_tail->next = task;
    }
    fdn_task_queue_tail = task;
}

static fdn_task *fdn_task_dequeue(void) {
    fdn_task *task = fdn_task_queue_head;
    if (task == NULL) {
        return NULL;
    }
    fdn_task_queue_head = task->next;
    if (fdn_task_queue_head == NULL) {
        fdn_task_queue_tail = NULL;
    }
    task->next = NULL;
    task->queued = false;
    return task;
}

static void fdn_task_run_one(void) {
    fdn_task *task = fdn_task_dequeue();
    if (task == NULL) {
        fdn_panic_cstr("task executor made no progress");
    }
    if (task->poll(task->frame, task->cancellation_requested) == FDN_TASK_READY) {
        task->ready = true;
        return;
    }
    fdn_task_enqueue(task);
}

static void fdn_task_run_until(fdn_task *target) {
    while (!target->ready) {
        fdn_task_run_one();
    }
}

static void fdn_task_release(fdn_task *task, void *result, bool move_result) {
    if (move_result) {
        task->move_result(task->frame, result);
    }
    task->drop_frame(task->frame);
    task->frame = NULL;
    --fdn_task_count;
    fdn_dealloc(task);
}

fdn_task *fdn_task_spawn(void *frame, fdn_task_poll_fn poll,
                         fdn_task_move_result_fn move_result,
                         fdn_task_drop_frame_fn drop_frame) {
    fdn_task *task;
    if (frame == NULL || poll == NULL || move_result == NULL || drop_frame == NULL) {
        fdn_panic_cstr("invalid task callbacks");
    }
    task = fdn_alloc(sizeof(*task));
    task->next = NULL;
    task->frame = frame;
    task->poll = poll;
    task->move_result = move_result;
    task->drop_frame = drop_frame;
    task->cancellation_requested = false;
    task->queued = false;
    task->ready = false;
    ++fdn_task_count;
    fdn_task_enqueue(task);
    return task;
}

void fdn_task_wait(fdn_task **task, void *result) {
    fdn_task *owned;
    if (task == NULL || *task == NULL) {
        fdn_panic_cstr("task handle was already consumed");
    }
    owned = *task;
    *task = NULL;
    fdn_task_run_until(owned);
    fdn_task_release(owned, result, true);
}

void fdn_task_drop(fdn_task **task) {
    fdn_task *owned;
    if (task == NULL || *task == NULL) {
        return;
    }
    owned = *task;
    *task = NULL;
    owned->cancellation_requested = true;
    fdn_task_run_until(owned);
    fdn_task_release(owned, NULL, false);
}

bool fdn_task_cancellation_requested(const fdn_task *task) {
    return task != NULL && task->cancellation_requested;
}

bool fdn_task_cancellation_enter(bool requested) {
    const bool previous = fdn_task_current_cancellation;
    fdn_task_current_cancellation = previous || requested;
    return previous;
}

void fdn_task_cancellation_leave(bool previous) {
    fdn_task_current_cancellation = previous;
}

bool fdn_task_cancellation_current(void) {
    return fdn_task_current_cancellation;
}

size_t fdn_task_live_count(void) { return fdn_task_count; }
