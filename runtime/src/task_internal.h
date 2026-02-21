#ifndef FOUNDATION_TASK_INTERNAL_H
#define FOUNDATION_TASK_INTERNAL_H

#include "foundation/runtime.h"

typedef void (*fdn_task_cancel_wait_fn)(fdn_task *task, void *context);
typedef bool (*fdn_task_idle_wake_fn)(void);
typedef bool (*fdn_task_idle_deadline_fn)(uint64_t *deadline_nanoseconds);
typedef void (*fdn_task_idle_sleep_fn)(uint64_t deadline_nanoseconds);
typedef void (*fdn_task_idle_wait_fn)(bool has_deadline,
                                      uint64_t deadline_nanoseconds);

struct fdn_task {
    struct fdn_task *next;
    struct fdn_task *waiter;
    struct fdn_task *waiting_on;
    struct fdn_task *wait_next;
    void *wait_context;
    void *wait_value;
    fdn_task_cancel_wait_fn cancel_wait;
    unsigned int wait_kind;
    void *wake_context;
    unsigned int wake_kind;
    unsigned int wake_status;
    void *frame;
    fdn_task_poll_fn poll;
    fdn_task_move_result_fn move_result;
    fdn_task_drop_frame_fn drop_frame;
    bool cancellation_requested;
    bool wake_ready;
    bool queued;
    bool ready;
};

fdn_task *fdn_task_current_get(void);
void fdn_task_park_current(void *context, void *value, unsigned int kind,
                           fdn_task_cancel_wait_fn cancel_wait);
bool fdn_task_take_wake(void *context, unsigned int kind, unsigned int *status);
void fdn_task_wake(fdn_task *task, unsigned int status);
void fdn_task_set_timer_source(fdn_task_idle_wake_fn wake,
                               fdn_task_idle_deadline_fn deadline,
                               fdn_task_idle_sleep_fn sleep);
void fdn_task_set_external_source(fdn_task_idle_wake_fn wake,
                                  fdn_task_idle_wait_fn wait);
void fdn_task_clear_external_source(fdn_task_idle_wake_fn wake,
                                    fdn_task_idle_wait_fn wait);

#endif
