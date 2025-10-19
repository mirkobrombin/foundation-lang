#include "foundation/runtime.h"

#include <stdlib.h>

struct fdn_task {
    struct fdn_task *next;
    struct fdn_task *waiter;
    struct fdn_task *waiting_on;
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
static FDN_THREAD_LOCAL fdn_task *fdn_task_current;

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

static void fdn_task_request_cancellation(fdn_task *task) {
    while (task != NULL && !task->cancellation_requested) {
        task->cancellation_requested = true;
        task = task->waiting_on;
    }
}

static void fdn_task_wake_waiter(fdn_task *task) {
    fdn_task *waiter = task->waiter;
    if (waiter == NULL) {
        return;
    }
    if (waiter->waiting_on != task) {
        fdn_panic_cstr("invalid task waiter");
    }
    task->waiter = NULL;
    waiter->waiting_on = NULL;
    fdn_task_enqueue(waiter);
}

static void fdn_task_run_one(void) {
    fdn_task *task = fdn_task_dequeue();
    fdn_task *previous;
    fdn_task_poll result;
    if (task == NULL) {
        fdn_panic_cstr("task executor made no progress");
    }
    previous = fdn_task_current;
    fdn_task_current = task;
    result = task->poll(task->frame, task->cancellation_requested);
    fdn_task_current = previous;
    if (result != FDN_TASK_PENDING && result != FDN_TASK_READY) {
        fdn_panic_cstr("invalid task poll result");
    }
    if (result == FDN_TASK_READY) {
        if (task->waiting_on != NULL) {
            fdn_panic_cstr("ready task still waits on a child");
        }
        task->ready = true;
        fdn_task_wake_waiter(task);
        return;
    }
    if (task->waiting_on == NULL) {
        fdn_task_enqueue(task);
    }
}

static void fdn_task_run_until(fdn_task *target) {
    while (!target->ready) {
        fdn_task_run_one();
    }
}

static void fdn_task_release(fdn_task *task, void *result, bool move_result) {
    if (!task->ready || task->waiter != NULL || task->waiting_on != NULL) {
        fdn_panic_cstr("cannot release an incomplete task");
    }
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
    task->waiter = NULL;
    task->waiting_on = NULL;
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

bool fdn_task_poll_wait(fdn_task **task, void *result) {
    fdn_task *owned;
    if (fdn_task_current == NULL) {
        fdn_panic_cstr("task poll wait requires an active task");
    }
    if (task == NULL || *task == NULL) {
        fdn_panic_cstr("task handle was already consumed");
    }
    owned = *task;
    if (owned == fdn_task_current) {
        fdn_panic_cstr("task cannot wait on itself");
    }
    if (owned->ready) {
        if (fdn_task_current->waiting_on != NULL || owned->waiter != NULL) {
            fdn_panic_cstr("invalid completed task waiter");
        }
        *task = NULL;
        fdn_task_release(owned, result, true);
        return true;
    }
    if (fdn_task_current->waiting_on != NULL || owned->waiter != NULL) {
        fdn_panic_cstr("task already has a waiter");
    }
    if (fdn_task_current->cancellation_requested) {
        fdn_task_request_cancellation(owned);
    }
    fdn_task_current->waiting_on = owned;
    owned->waiter = fdn_task_current;
    return false;
}

void fdn_task_wait(fdn_task **task, void *result) {
    fdn_task *owned;
    if (task == NULL || *task == NULL) {
        fdn_panic_cstr("task handle was already consumed");
    }
    owned = *task;
    *task = NULL;
    if (owned->waiter != NULL) {
        fdn_panic_cstr("task already has a waiter");
    }
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
    if (owned->waiter != NULL) {
        fdn_panic_cstr("task already has a waiter");
    }
    fdn_task_request_cancellation(owned);
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
