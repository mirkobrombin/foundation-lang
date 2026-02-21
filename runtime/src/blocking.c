#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "task_internal.h"

#include <errno.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

#if defined(_MSC_VER)
#define FDN_BLOCKING_THREAD_LOCAL __declspec(thread)
#else
#define FDN_BLOCKING_THREAD_LOCAL _Thread_local
#endif

enum {
    FDN_BLOCKING_WAIT = 4,
    FDN_BLOCKING_READY = 1,
    FDN_BLOCKING_WORKER_COUNT = 4,
};

typedef struct fdn_blocking_executor fdn_blocking_executor;

struct fdn_blocking_job {
    struct fdn_blocking_job *next;
    fdn_blocking_executor *executor;
    fdn_task *task;
    void *context;
    fdn_blocking_work_fn work;
};

struct fdn_blocking_executor {
    fdn_blocking_job *pending_head;
    fdn_blocking_job *pending_tail;
    fdn_blocking_job *complete_head;
    fdn_blocking_job *complete_tail;
    size_t jobs;
    bool stopping;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE work_ready;
    CONDITION_VARIABLE completion_ready;
    HANDLE workers[FDN_BLOCKING_WORKER_COUNT];
#else
    pthread_mutex_t lock;
    pthread_cond_t work_ready;
    pthread_cond_t completion_ready;
    pthread_t workers[FDN_BLOCKING_WORKER_COUNT];
#endif
    size_t worker_count;
};

static FDN_BLOCKING_THREAD_LOCAL fdn_blocking_executor *fdn_blocking_current;

static void fdn_blocking_lock(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    EnterCriticalSection(&executor->lock);
#else
    if (pthread_mutex_lock(&executor->lock) != 0) {
        fdn_panic_cstr("blocking executor lock failed");
    }
#endif
}

static void fdn_blocking_unlock(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    LeaveCriticalSection(&executor->lock);
#else
    if (pthread_mutex_unlock(&executor->lock) != 0) {
        fdn_panic_cstr("blocking executor unlock failed");
    }
#endif
}

static void fdn_blocking_signal_work(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    WakeConditionVariable(&executor->work_ready);
#else
    if (pthread_cond_signal(&executor->work_ready) != 0) {
        fdn_panic_cstr("blocking executor work signal failed");
    }
#endif
}

static void fdn_blocking_signal_completion(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    WakeConditionVariable(&executor->completion_ready);
#else
    if (pthread_cond_signal(&executor->completion_ready) != 0) {
        fdn_panic_cstr("blocking executor completion signal failed");
    }
#endif
}

static void fdn_blocking_broadcast_work(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    WakeAllConditionVariable(&executor->work_ready);
#else
    if (pthread_cond_broadcast(&executor->work_ready) != 0) {
        fdn_panic_cstr("blocking executor shutdown signal failed");
    }
#endif
}

static fdn_blocking_job *fdn_blocking_pop(fdn_blocking_job **head,
                                          fdn_blocking_job **tail) {
    fdn_blocking_job *job = *head;
    if (job == NULL) {
        return NULL;
    }
    *head = job->next;
    if (*head == NULL) {
        *tail = NULL;
    }
    job->next = NULL;
    return job;
}

static void fdn_blocking_push(fdn_blocking_job **head, fdn_blocking_job **tail,
                              fdn_blocking_job *job) {
    job->next = NULL;
    if (*tail == NULL) {
        *head = job;
    } else {
        (*tail)->next = job;
    }
    *tail = job;
}

#if defined(_WIN32)
static DWORD WINAPI fdn_blocking_worker(void *context)
#else
static void *fdn_blocking_worker(void *context)
#endif
{
    fdn_blocking_executor *executor = context;
    for (;;) {
        fdn_blocking_job *job;
        fdn_blocking_lock(executor);
        while (executor->pending_head == NULL && !executor->stopping) {
#if defined(_WIN32)
            if (SleepConditionVariableCS(&executor->work_ready, &executor->lock,
                                         INFINITE) == 0) {
                fdn_panic_cstr("blocking executor work wait failed");
            }
#else
            if (pthread_cond_wait(&executor->work_ready, &executor->lock) != 0) {
                fdn_panic_cstr("blocking executor work wait failed");
            }
#endif
        }
        if (executor->stopping) {
            fdn_blocking_unlock(executor);
            break;
        }
        job = fdn_blocking_pop(&executor->pending_head, &executor->pending_tail);
        fdn_blocking_unlock(executor);
        if (job == NULL) {
            fdn_panic_cstr("blocking executor lost pending work");
        }
        job->work(job->context);
        fdn_blocking_lock(executor);
        fdn_blocking_push(&executor->complete_head, &executor->complete_tail, job);
        fdn_blocking_signal_completion(executor);
        fdn_blocking_unlock(executor);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void fdn_blocking_destroy(fdn_blocking_executor *executor) {
#if defined(_WIN32)
    DeleteCriticalSection(&executor->lock);
#else
    if (pthread_cond_destroy(&executor->completion_ready) != 0 ||
        pthread_cond_destroy(&executor->work_ready) != 0 ||
        pthread_mutex_destroy(&executor->lock) != 0) {
        fdn_panic_cstr("blocking executor destroy failed");
    }
#endif
    fdn_dealloc(executor);
}

static void fdn_blocking_stop(fdn_blocking_executor *executor) {
    fdn_blocking_lock(executor);
    executor->stopping = true;
    fdn_blocking_broadcast_work(executor);
    fdn_blocking_unlock(executor);
    for (size_t index = 0; index < executor->worker_count; ++index) {
#if defined(_WIN32)
        if (WaitForSingleObject(executor->workers[index], INFINITE) != WAIT_OBJECT_0 ||
            CloseHandle(executor->workers[index]) == 0) {
            fdn_panic_cstr("blocking executor worker join failed");
        }
#else
        if (pthread_join(executor->workers[index], NULL) != 0) {
            fdn_panic_cstr("blocking executor worker join failed");
        }
#endif
    }
    fdn_blocking_destroy(executor);
}

static bool fdn_blocking_wake_completed(void) {
    fdn_blocking_executor *executor = fdn_blocking_current;
    bool woke = false;
    if (executor == NULL) {
        return false;
    }
    for (;;) {
        fdn_blocking_job *job;
        fdn_blocking_lock(executor);
        job = fdn_blocking_pop(&executor->complete_head, &executor->complete_tail);
        fdn_blocking_unlock(executor);
        if (job == NULL) {
            return woke;
        }
        fdn_task_wake(job->task, FDN_BLOCKING_READY);
        woke = true;
    }
}

#if !defined(_WIN32)
static struct timespec fdn_blocking_realtime_deadline(uint64_t remaining) {
    struct timespec deadline;
    const uint64_t maximum_wait = UINT64_C(3600000000000);
    if (remaining > maximum_wait) {
        remaining = maximum_wait;
    }
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        fdn_panic_cstr("blocking executor clock failed");
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

static void fdn_blocking_wait(bool has_deadline, uint64_t deadline_nanoseconds) {
    fdn_blocking_executor *executor = fdn_blocking_current;
    if (executor == NULL) {
        fdn_panic_cstr("blocking executor wait has no executor");
    }
    fdn_blocking_lock(executor);
    while (executor->complete_head == NULL) {
        if (!has_deadline) {
#if defined(_WIN32)
            if (SleepConditionVariableCS(&executor->completion_ready, &executor->lock,
                                         INFINITE) == 0) {
                fdn_panic_cstr("blocking executor completion wait failed");
            }
#else
            if (pthread_cond_wait(&executor->completion_ready, &executor->lock) != 0) {
                fdn_panic_cstr("blocking executor completion wait failed");
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
        if (SleepConditionVariableCS(&executor->completion_ready, &executor->lock,
                                     (DWORD)milliseconds) == 0 &&
            GetLastError() != ERROR_TIMEOUT) {
            fdn_panic_cstr("blocking executor timed wait failed");
        }
#else
        const struct timespec timeout = fdn_blocking_realtime_deadline(remaining);
        const int status = pthread_cond_timedwait(&executor->completion_ready,
                                                  &executor->lock, &timeout);
        if (status != 0 && status != ETIMEDOUT && status != EINTR) {
            fdn_panic_cstr("blocking executor timed wait failed");
        }
#endif
    }
    fdn_blocking_unlock(executor);
}

static fdn_blocking_executor *fdn_blocking_open(void) {
    fdn_blocking_executor *executor = fdn_alloc(sizeof(*executor));
    executor->pending_head = NULL;
    executor->pending_tail = NULL;
    executor->complete_head = NULL;
    executor->complete_tail = NULL;
    executor->jobs = 0;
    executor->stopping = false;
    executor->worker_count = 0;
#if defined(_WIN32)
    InitializeCriticalSection(&executor->lock);
    InitializeConditionVariable(&executor->work_ready);
    InitializeConditionVariable(&executor->completion_ready);
#else
    if (pthread_mutex_init(&executor->lock, NULL) != 0 ||
        pthread_cond_init(&executor->work_ready, NULL) != 0 ||
        pthread_cond_init(&executor->completion_ready, NULL) != 0) {
        fdn_panic_cstr("blocking executor initialization failed");
    }
#endif
    for (size_t index = 0; index < FDN_BLOCKING_WORKER_COUNT; ++index) {
#if defined(_WIN32)
        executor->workers[index] =
            CreateThread(NULL, 0, fdn_blocking_worker, executor, 0, NULL);
        if (executor->workers[index] == NULL) {
            fdn_panic_cstr("blocking executor worker creation failed");
        }
#else
        if (pthread_create(&executor->workers[index], NULL, fdn_blocking_worker,
                           executor) != 0) {
            fdn_panic_cstr("blocking executor worker creation failed");
        }
#endif
        ++executor->worker_count;
    }
    fdn_blocking_current = executor;
    fdn_task_set_external_source(fdn_blocking_wake_completed, fdn_blocking_wait);
    return executor;
}

static void fdn_blocking_cancel_wait(fdn_task *task, void *context) {
    fdn_blocking_job *job = context;
    if (job == NULL || job->task != task) {
        fdn_panic_cstr("blocking executor cancellation has no job");
    }
}

bool fdn_blocking_poll(fdn_blocking_job **slot, void *context,
                       fdn_blocking_work_fn work) {
    fdn_task *current = fdn_task_current_get();
    if (current == NULL || slot == NULL || context == NULL || work == NULL) {
        fdn_panic_cstr("invalid blocking operation");
    }
    if (*slot != NULL) {
        fdn_blocking_job *job = *slot;
        unsigned int status;
        if (job->task != current ||
            !fdn_task_take_wake(job, FDN_BLOCKING_WAIT, &status) ||
            status != FDN_BLOCKING_READY) {
            fdn_panic_cstr("blocking operation resumed without completion");
        }
        fdn_blocking_executor *executor = job->executor;
        *slot = NULL;
        fdn_blocking_lock(executor);
        if (executor->jobs == 0) {
            fdn_panic_cstr("blocking executor job count underflow");
        }
        --executor->jobs;
        const bool stop = executor->jobs == 0;
        fdn_blocking_unlock(executor);
        fdn_dealloc(job);
        if (stop) {
            fdn_task_clear_external_source(fdn_blocking_wake_completed,
                                           fdn_blocking_wait);
            fdn_blocking_current = NULL;
            fdn_blocking_stop(executor);
        }
        return true;
    }

    fdn_blocking_executor *executor = fdn_blocking_current;
    if (executor == NULL) {
        executor = fdn_blocking_open();
    }
    fdn_blocking_job *job = fdn_alloc(sizeof(*job));
    job->next = NULL;
    job->executor = executor;
    job->task = current;
    job->context = context;
    job->work = work;
    *slot = job;
    fdn_task_park_current(job, NULL, FDN_BLOCKING_WAIT,
                          fdn_blocking_cancel_wait);
    fdn_blocking_lock(executor);
    if (executor->jobs == SIZE_MAX) {
        fdn_panic_cstr("blocking executor job count overflow");
    }
    ++executor->jobs;
    fdn_blocking_push(&executor->pending_head, &executor->pending_tail, job);
    fdn_blocking_signal_work(executor);
    fdn_blocking_unlock(executor);
    return false;
}

size_t fdn_blocking_live_jobs(void) {
    return fdn_blocking_current == NULL ? 0 : fdn_blocking_current->jobs;
}
