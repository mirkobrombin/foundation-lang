#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "task_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

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
static FDN_THREAD_LOCAL fdn_task_idle_wake_fn fdn_task_timer_wake;
static FDN_THREAD_LOCAL fdn_task_idle_deadline_fn fdn_task_timer_deadline;
static FDN_THREAD_LOCAL fdn_task_idle_sleep_fn fdn_task_timer_sleep;

typedef struct fdn_task_external_hub fdn_task_external_hub;

struct fdn_task_external_source {
    struct fdn_task_external_source *next;
    fdn_task_external_hub *hub;
    fdn_task_external_wake_fn wake;
    void *context;
};

struct fdn_task_external_hub {
    fdn_task_external_source *sources;
    bool notified;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE ready;
#else
    pthread_mutex_t lock;
    pthread_cond_t ready;
#endif
};

static FDN_THREAD_LOCAL fdn_task_external_hub *fdn_task_external_current;

uint64_t fdn_monotonic_nanoseconds(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64() * UINT64_C(1000000);
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        fdn_panic_cstr("monotonic clock failed");
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
#endif
}

static void fdn_task_external_lock(fdn_task_external_hub *hub) {
#if defined(_WIN32)
    EnterCriticalSection(&hub->lock);
#else
    if (pthread_mutex_lock(&hub->lock) != 0) {
        fdn_panic_cstr("task external source lock failed");
    }
#endif
}

static void fdn_task_external_unlock(fdn_task_external_hub *hub) {
#if defined(_WIN32)
    LeaveCriticalSection(&hub->lock);
#else
    if (pthread_mutex_unlock(&hub->lock) != 0) {
        fdn_panic_cstr("task external source unlock failed");
    }
#endif
}

static fdn_task_external_hub *fdn_task_external_open_hub(void) {
    fdn_task_external_hub *hub = fdn_alloc(sizeof(*hub));
    hub->sources = NULL;
    hub->notified = false;
#if defined(_WIN32)
    InitializeCriticalSection(&hub->lock);
    InitializeConditionVariable(&hub->ready);
#else
    if (pthread_mutex_init(&hub->lock, NULL) != 0 ||
        pthread_cond_init(&hub->ready, NULL) != 0) {
        fdn_panic_cstr("task external source initialization failed");
    }
#endif
    fdn_task_external_current = hub;
    return hub;
}

static void fdn_task_external_destroy_hub(fdn_task_external_hub *hub) {
#if defined(_WIN32)
    DeleteCriticalSection(&hub->lock);
#else
    if (pthread_cond_destroy(&hub->ready) != 0 ||
        pthread_mutex_destroy(&hub->lock) != 0) {
        fdn_panic_cstr("task external source destroy failed");
    }
#endif
    fdn_dealloc(hub);
}

#if !defined(_WIN32)
static struct timespec fdn_task_external_realtime_deadline(uint64_t remaining) {
    struct timespec deadline;
    const uint64_t maximum_wait = UINT64_C(3600000000000);
    if (remaining > maximum_wait) {
        remaining = maximum_wait;
    }
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        fdn_panic_cstr("task external source clock failed");
    }
    deadline.tv_sec += (time_t)(remaining / UINT64_C(1000000000));
    deadline.tv_nsec += (long)(remaining % UINT64_C(1000000000));
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}
#endif

static void fdn_task_external_wait(bool has_deadline,
                                   uint64_t deadline_nanoseconds) {
    fdn_task_external_hub *hub = fdn_task_external_current;
    if (hub == NULL) {
        fdn_panic_cstr("task external wait has no source");
    }
    fdn_task_external_lock(hub);
    while (!hub->notified) {
        if (!has_deadline) {
#if defined(_WIN32)
            if (SleepConditionVariableCS(&hub->ready, &hub->lock, INFINITE) == 0) {
                fdn_panic_cstr("task external source wait failed");
            }
#else
            if (pthread_cond_wait(&hub->ready, &hub->lock) != 0) {
                fdn_panic_cstr("task external source wait failed");
            }
#endif
            continue;
        }
        const uint64_t now = fdn_monotonic_nanoseconds();
        if (now >= deadline_nanoseconds) {
            break;
        }
        const uint64_t remaining = deadline_nanoseconds - now;
#if defined(_WIN32)
        uint64_t milliseconds = remaining / UINT64_C(1000000);
        if (remaining % UINT64_C(1000000) != 0) {
            ++milliseconds;
        }
        if (milliseconds > UINT32_MAX - 1U) {
            milliseconds = UINT32_MAX - 1U;
        }
        if (SleepConditionVariableCS(&hub->ready, &hub->lock,
                                     (DWORD)milliseconds) == 0 &&
            GetLastError() != ERROR_TIMEOUT) {
            fdn_panic_cstr("task external source timed wait failed");
        }
#else
        const struct timespec timeout =
            fdn_task_external_realtime_deadline(remaining);
        const int status = pthread_cond_timedwait(&hub->ready, &hub->lock, &timeout);
        if (status != 0 && status != ETIMEDOUT && status != EINTR) {
            fdn_panic_cstr("task external source timed wait failed");
        }
#endif
    }
    hub->notified = false;
    fdn_task_external_unlock(hub);
}

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

static bool fdn_task_poll_idle_sources(void) {
    bool woke = false;
    if (fdn_task_timer_wake != NULL) {
        woke = fdn_task_timer_wake();
    }
    if (fdn_task_external_current != NULL) {
        fdn_task_external_source *source = fdn_task_external_current->sources;
        while (source != NULL) {
            woke = source->wake(source->context) || woke;
            source = source->next;
        }
    }
    return woke;
}

static void fdn_task_request_cancellation(fdn_task *task) {
    while (task != NULL && !task->cancellation_requested) {
        task->cancellation_requested = true;
        if (task->cancel_wait != NULL) {
            fdn_task_cancel_wait_fn cancel_wait = task->cancel_wait;
            void *context = task->wait_context;
            if (task->waiting_on != NULL) {
                fdn_panic_cstr("task has multiple wait sources");
            }
            cancel_wait(task, context);
        }
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
    while (task == NULL) {
        uint64_t deadline = 0;
        const bool has_deadline = fdn_task_timer_deadline != NULL &&
                                  fdn_task_timer_deadline(&deadline);
        if (fdn_task_poll_idle_sources()) {
            task = fdn_task_dequeue();
        } else if (fdn_task_external_current != NULL) {
            fdn_task_external_wait(has_deadline, deadline);
            if (fdn_task_poll_idle_sources()) {
                task = fdn_task_dequeue();
            }
        } else if (has_deadline && fdn_task_timer_sleep != NULL) {
            fdn_task_timer_sleep(deadline);
            if (fdn_task_poll_idle_sources()) {
                task = fdn_task_dequeue();
            }
        }
        if (task == NULL) {
            if (fdn_task_external_current != NULL) {
                continue;
            }
            fdn_panic_cstr("task executor made no progress");
        }
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
    if (task->waiting_on == NULL && task->cancel_wait == NULL) {
        fdn_task_enqueue(task);
    }
}

static void fdn_task_run_until(fdn_task *target) {
    while (!target->ready) {
        fdn_task_run_one();
    }
}

static void fdn_task_release(fdn_task *task, void *result, bool move_result) {
    if (!task->ready || task->waiter != NULL || task->waiting_on != NULL ||
        task->cancel_wait != NULL || task->wait_next != NULL || task->wake_ready) {
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
    task->wait_next = NULL;
    task->wait_context = NULL;
    task->wait_value = NULL;
    task->cancel_wait = NULL;
    task->wait_kind = 0;
    task->wake_context = NULL;
    task->wake_kind = 0;
    task->wake_status = 0;
    task->frame = frame;
    task->poll = poll;
    task->move_result = move_result;
    task->drop_frame = drop_frame;
    task->cancellation_requested = false;
    task->wake_ready = false;
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
        if (fdn_task_current->waiting_on != NULL || fdn_task_current->cancel_wait != NULL ||
            owned->waiter != NULL) {
            fdn_panic_cstr("invalid completed task waiter");
        }
        *task = NULL;
        fdn_task_release(owned, result, true);
        return true;
    }
    if (fdn_task_current->waiting_on != NULL || fdn_task_current->cancel_wait != NULL ||
        owned->waiter != NULL) {
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

fdn_task *fdn_task_current_get(void) { return fdn_task_current; }

void fdn_task_park_current(void *context, void *value, unsigned int kind,
                           fdn_task_cancel_wait_fn cancel_wait) {
    if (fdn_task_current == NULL || context == NULL || cancel_wait == NULL) {
        fdn_panic_cstr("invalid task wait registration");
    }
    if (fdn_task_current->waiting_on != NULL || fdn_task_current->cancel_wait != NULL ||
        fdn_task_current->wake_ready) {
        fdn_panic_cstr("task already has a wait source");
    }
    fdn_task_current->wait_context = context;
    fdn_task_current->wait_value = value;
    fdn_task_current->wait_kind = kind;
    fdn_task_current->cancel_wait = cancel_wait;
}

bool fdn_task_take_wake(void *context, unsigned int kind, unsigned int *status) {
    if (fdn_task_current == NULL || status == NULL) {
        fdn_panic_cstr("invalid task wake read");
    }
    if (!fdn_task_current->wake_ready) {
        return false;
    }
    if (fdn_task_current->wake_context != context || fdn_task_current->wake_kind != kind) {
        fdn_panic_cstr("task resumed for a different wait source");
    }
    *status = fdn_task_current->wake_status;
    fdn_task_current->wake_context = NULL;
    fdn_task_current->wake_kind = 0;
    fdn_task_current->wake_status = 0;
    fdn_task_current->wake_ready = false;
    return true;
}

void fdn_task_wake(fdn_task *task, unsigned int status) {
    if (task == NULL || task->cancel_wait == NULL || task->wait_context == NULL ||
        task->wake_ready || task->queued || task->ready) {
        fdn_panic_cstr("invalid task wake");
    }
    task->wake_context = task->wait_context;
    task->wake_kind = task->wait_kind;
    task->wake_status = status;
    task->wake_ready = true;
    task->wait_context = NULL;
    task->wait_value = NULL;
    task->wait_kind = 0;
    task->cancel_wait = NULL;
    task->wait_next = NULL;
    fdn_task_enqueue(task);
}

void fdn_task_set_timer_source(fdn_task_idle_wake_fn wake,
                               fdn_task_idle_deadline_fn deadline,
                               fdn_task_idle_sleep_fn sleep) {
    if (wake == NULL || deadline == NULL || sleep == NULL) {
        fdn_panic_cstr("invalid task idle hooks");
    }
    fdn_task_timer_wake = wake;
    fdn_task_timer_deadline = deadline;
    fdn_task_timer_sleep = sleep;
}

fdn_task_external_source *fdn_task_external_source_open(
    fdn_task_external_wake_fn wake, void *context) {
    fdn_task_external_source *source;
    fdn_task_external_hub *hub;
    if (wake == NULL || context == NULL || fdn_task_current == NULL) {
        fdn_panic_cstr("invalid task external source");
    }
    hub = fdn_task_external_current;
    if (hub == NULL) {
        hub = fdn_task_external_open_hub();
    }
    source = fdn_alloc(sizeof(*source));
    source->next = hub->sources;
    source->hub = hub;
    source->wake = wake;
    source->context = context;
    hub->sources = source;
    return source;
}

void fdn_task_external_source_notify(fdn_task_external_source *source) {
    fdn_task_external_hub *hub;
    if (source == NULL || source->hub == NULL) {
        fdn_panic_cstr("invalid task external source notification");
    }
    hub = source->hub;
    fdn_task_external_lock(hub);
    hub->notified = true;
#if defined(_WIN32)
    WakeConditionVariable(&hub->ready);
#else
    if (pthread_cond_signal(&hub->ready) != 0) {
        fdn_panic_cstr("task external source signal failed");
    }
#endif
    fdn_task_external_unlock(hub);
}

void fdn_task_external_source_close(fdn_task_external_source *source) {
    fdn_task_external_hub *hub;
    fdn_task_external_source **cursor;
    if (source == NULL || source->hub == NULL || fdn_task_current == NULL) {
        fdn_panic_cstr("invalid task external source removal");
    }
    hub = source->hub;
    if (hub != fdn_task_external_current) {
        fdn_panic_cstr("task external source belongs to a different executor");
    }
    cursor = &hub->sources;
    while (*cursor != NULL && *cursor != source) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == NULL) {
        fdn_panic_cstr("task external source was not registered");
    }
    *cursor = source->next;
    source->hub = NULL;
    fdn_dealloc(source);
    if (hub->sources == NULL) {
        fdn_task_external_current = NULL;
        fdn_task_external_destroy_hub(hub);
    }
}

size_t fdn_task_live_count(void) { return fdn_task_count; }
