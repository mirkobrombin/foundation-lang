#include "foundation/runtime.h"
#include "task_internal.h"

#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#if defined(_MSC_VER)
#define FDN_REACTOR_THREAD_LOCAL __declspec(thread)
#else
#define FDN_REACTOR_THREAD_LOCAL _Thread_local
#endif

enum {
    FDN_REACTOR_WAIT = 5,
    FDN_REACTOR_READY = 1,
};

typedef struct fdn_reactor fdn_reactor;

struct fdn_reactor_operation {
    struct fdn_reactor_operation *next;
    fdn_reactor *reactor;
    fdn_task *task;
    void *context;
    fdn_reactor_cancel_fn cancel;
    int32_t status;
    bool completed;
    bool cancellation_requested;
};

struct fdn_reactor {
    fdn_reactor_operation *complete_head;
    fdn_reactor_operation *complete_tail;
    fdn_task_external_source *source;
    size_t operations;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
};

static FDN_REACTOR_THREAD_LOCAL fdn_reactor *fdn_reactor_current;

static void fdn_reactor_lock(fdn_reactor *reactor) {
#if defined(_WIN32)
    EnterCriticalSection(&reactor->lock);
#else
    if (pthread_mutex_lock(&reactor->lock) != 0) {
        fdn_panic_cstr("reactor lock failed");
    }
#endif
}

static void fdn_reactor_unlock(fdn_reactor *reactor) {
#if defined(_WIN32)
    LeaveCriticalSection(&reactor->lock);
#else
    if (pthread_mutex_unlock(&reactor->lock) != 0) {
        fdn_panic_cstr("reactor unlock failed");
    }
#endif
}

static void fdn_reactor_push_complete(fdn_reactor *reactor,
                                      fdn_reactor_operation *operation) {
    operation->next = NULL;
    if (reactor->complete_tail == NULL) {
        reactor->complete_head = operation;
    } else {
        reactor->complete_tail->next = operation;
    }
    reactor->complete_tail = operation;
}

static fdn_reactor_operation *fdn_reactor_pop_complete(fdn_reactor *reactor) {
    fdn_reactor_operation *operation = reactor->complete_head;
    if (operation == NULL) {
        return NULL;
    }
    reactor->complete_head = operation->next;
    if (reactor->complete_head == NULL) {
        reactor->complete_tail = NULL;
    }
    operation->next = NULL;
    return operation;
}

static bool fdn_reactor_wake_completed(void *context) {
    fdn_reactor *reactor = context;
    bool woke = false;
    if (reactor == NULL || reactor != fdn_reactor_current) {
        fdn_panic_cstr("reactor wake has no reactor");
    }
    for (;;) {
        fdn_reactor_operation *operation;
        fdn_reactor_lock(reactor);
        operation = fdn_reactor_pop_complete(reactor);
        fdn_reactor_unlock(reactor);
        if (operation == NULL) {
            return woke;
        }
        fdn_task_wake(operation->task, FDN_REACTOR_READY);
        woke = true;
    }
}

static fdn_reactor *fdn_reactor_open(void) {
    fdn_reactor *reactor = fdn_alloc(sizeof(*reactor));
    reactor->complete_head = NULL;
    reactor->complete_tail = NULL;
    reactor->source = NULL;
    reactor->operations = 0;
#if defined(_WIN32)
    InitializeCriticalSection(&reactor->lock);
#else
    if (pthread_mutex_init(&reactor->lock, NULL) != 0) {
        fdn_panic_cstr("reactor initialization failed");
    }
#endif
    fdn_reactor_current = reactor;
    reactor->source =
        fdn_task_external_source_open(fdn_reactor_wake_completed, reactor);
    return reactor;
}

static void fdn_reactor_destroy(fdn_reactor *reactor) {
    if (reactor->complete_head != NULL || reactor->complete_tail != NULL ||
        reactor->operations != 0 || reactor->source != NULL) {
        fdn_panic_cstr("cannot destroy active reactor");
    }
#if defined(_WIN32)
    DeleteCriticalSection(&reactor->lock);
#else
    if (pthread_mutex_destroy(&reactor->lock) != 0) {
        fdn_panic_cstr("reactor destroy failed");
    }
#endif
    fdn_dealloc(reactor);
}

static void fdn_reactor_request_cancellation(
    fdn_reactor_operation *operation) {
    fdn_reactor_cancel_fn cancel = NULL;
    fdn_reactor *reactor = operation->reactor;
    fdn_reactor_lock(reactor);
    if (!operation->completed && !operation->cancellation_requested) {
        operation->cancellation_requested = true;
        cancel = operation->cancel;
    }
    fdn_reactor_unlock(reactor);
    if (cancel != NULL) {
        cancel(operation->context);
    }
}

static void fdn_reactor_cancel_wait(fdn_task *task, void *context) {
    fdn_reactor_operation *operation = context;
    if (operation == NULL || operation->task != task) {
        fdn_panic_cstr("reactor cancellation has no operation");
    }
    fdn_reactor_request_cancellation(operation);
}

bool fdn_reactor_poll(fdn_reactor_operation **slot, void *context,
                      fdn_reactor_start_fn start, fdn_reactor_cancel_fn cancel,
                      int32_t *status) {
    fdn_task *current = fdn_task_current_get();
    if (current == NULL || slot == NULL || context == NULL || start == NULL ||
        status == NULL) {
        fdn_panic_cstr("invalid reactor operation");
    }
    if (*slot != NULL) {
        fdn_reactor_operation *operation = *slot;
        unsigned int wake_status;
        if (operation->task != current ||
            !fdn_task_take_wake(operation, FDN_REACTOR_WAIT, &wake_status) ||
            wake_status != FDN_REACTOR_READY || !operation->completed) {
            fdn_panic_cstr("reactor operation resumed without completion");
        }
        fdn_reactor *reactor = operation->reactor;
        *status = operation->status;
        *slot = NULL;
        fdn_reactor_lock(reactor);
        if (reactor->operations == 0) {
            fdn_panic_cstr("reactor operation count underflow");
        }
        --reactor->operations;
        const bool stop = reactor->operations == 0;
        fdn_reactor_unlock(reactor);
        fdn_dealloc(operation);
        if (stop) {
            fdn_task_external_source_close(reactor->source);
            reactor->source = NULL;
            fdn_reactor_current = NULL;
            fdn_reactor_destroy(reactor);
        }
        return true;
    }

    fdn_reactor *reactor = fdn_reactor_current;
    if (reactor == NULL) {
        reactor = fdn_reactor_open();
    }
    fdn_reactor_operation *operation = fdn_alloc(sizeof(*operation));
    operation->next = NULL;
    operation->reactor = reactor;
    operation->task = current;
    operation->context = context;
    operation->cancel = cancel;
    operation->status = 0;
    operation->completed = false;
    operation->cancellation_requested = false;
    *slot = operation;
    fdn_task_park_current(operation, NULL, FDN_REACTOR_WAIT,
                          fdn_reactor_cancel_wait);
    fdn_reactor_lock(reactor);
    if (reactor->operations == SIZE_MAX) {
        fdn_panic_cstr("reactor operation count overflow");
    }
    ++reactor->operations;
    fdn_reactor_unlock(reactor);
    start(context, operation);
    if (fdn_task_cancellation_requested(current)) {
        fdn_reactor_request_cancellation(operation);
    }
    return false;
}

void fdn_reactor_complete(fdn_reactor_operation *operation, int32_t status) {
    fdn_reactor *reactor;
    if (operation == NULL || operation->reactor == NULL) {
        fdn_panic_cstr("invalid reactor completion");
    }
    reactor = operation->reactor;
    fdn_reactor_lock(reactor);
    if (operation->completed) {
        fdn_panic_cstr("reactor operation completed twice");
    }
    operation->status = status;
    operation->completed = true;
    fdn_reactor_push_complete(reactor, operation);
    fdn_reactor_unlock(reactor);
    fdn_task_external_source_notify(reactor->source);
}

size_t fdn_reactor_live_operations(void) {
    return fdn_reactor_current == NULL ? 0 : fdn_reactor_current->operations;
}
