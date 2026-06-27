#include "foundation/runtime.h"
#include "task_internal.h"

#include <limits.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <stdatomic.h>
#endif

typedef struct fdn_parallel_job fdn_parallel_job;
typedef struct fdn_parallel_pool fdn_parallel_pool;

struct fdn_parallel_job {
    fdn_parallel_job *pending_next;
    fdn_parallel_job *all_next;
    fdn_task *task;
#if defined(_WIN32)
    volatile LONG cancellation_requested;
#else
    atomic_bool cancellation_requested;
#endif
};

struct fdn_parallel_pool {
    fdn_parallel_job *pending_head;
    fdn_parallel_job *pending_tail;
    fdn_parallel_job *all_head;
    size_t jobs;
    size_t worker_count;
    bool stopping;
    bool joined;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE work_ready;
    HANDLE *workers;
#else
    pthread_mutex_t lock;
    pthread_cond_t work_ready;
    pthread_t *workers;
#endif
};

#if defined(_WIN32)
static volatile LONG64 fdn_parallel_pool_count;
#else
static atomic_uint_fast64_t fdn_parallel_pool_count;
#endif

static fdn_parallel_pool *fdn_parallel_from_handle(uint64_t handle) {
    fdn_parallel_pool *pool = (fdn_parallel_pool *)(uintptr_t)handle;
    if (pool == NULL) {
        fdn_panic_cstr("parallel pool handle is closed");
    }
    return pool;
}

static void fdn_parallel_lock(fdn_parallel_pool *pool) {
#if defined(_WIN32)
    EnterCriticalSection(&pool->lock);
#else
    if (pthread_mutex_lock(&pool->lock) != 0) {
        fdn_panic_cstr("parallel pool lock failed");
    }
#endif
}

static void fdn_parallel_unlock(fdn_parallel_pool *pool) {
#if defined(_WIN32)
    LeaveCriticalSection(&pool->lock);
#else
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        fdn_panic_cstr("parallel pool unlock failed");
    }
#endif
}

static void fdn_parallel_signal(fdn_parallel_pool *pool) {
#if defined(_WIN32)
    WakeConditionVariable(&pool->work_ready);
#else
    if (pthread_cond_signal(&pool->work_ready) != 0) {
        fdn_panic_cstr("parallel pool work signal failed");
    }
#endif
}

static void fdn_parallel_broadcast(fdn_parallel_pool *pool) {
#if defined(_WIN32)
    WakeAllConditionVariable(&pool->work_ready);
#else
    if (pthread_cond_broadcast(&pool->work_ready) != 0) {
        fdn_panic_cstr("parallel pool shutdown signal failed");
    }
#endif
}

static void fdn_parallel_request_cancellation(fdn_parallel_job *job) {
#if defined(_WIN32)
    (void)InterlockedExchange(&job->cancellation_requested, 1);
#else
    atomic_store_explicit(&job->cancellation_requested, true, memory_order_release);
#endif
}

static bool fdn_parallel_cancellation_requested(void *context) {
    fdn_parallel_job *job = context;
#if defined(_WIN32)
    return InterlockedCompareExchange(&job->cancellation_requested, 0, 0) != 0;
#else
    return atomic_load_explicit(&job->cancellation_requested, memory_order_acquire);
#endif
}

static fdn_parallel_job *fdn_parallel_pop(fdn_parallel_pool *pool) {
    fdn_parallel_job *job = pool->pending_head;
    if (job == NULL) {
        return NULL;
    }
    pool->pending_head = job->pending_next;
    if (pool->pending_head == NULL) {
        pool->pending_tail = NULL;
    }
    job->pending_next = NULL;
    return job;
}

static void fdn_parallel_remove(fdn_parallel_pool *pool, fdn_parallel_job *job) {
    fdn_parallel_job **current = &pool->all_head;
    while (*current != NULL && *current != job) {
        current = &(*current)->all_next;
    }
    if (*current == NULL || pool->jobs == 0) {
        fdn_panic_cstr("parallel pool lost submitted work");
    }
    *current = job->all_next;
    job->all_next = NULL;
    --pool->jobs;
}

#if defined(_WIN32)
static DWORD WINAPI fdn_parallel_worker(void *context)
#else
static void *fdn_parallel_worker(void *context)
#endif
{
    fdn_parallel_pool *pool = context;
    for (;;) {
        fdn_parallel_job *job;
        fdn_parallel_lock(pool);
        while (pool->pending_head == NULL && !pool->stopping) {
#if defined(_WIN32)
            if (SleepConditionVariableCS(&pool->work_ready, &pool->lock, INFINITE) == 0) {
                fdn_panic_cstr("parallel pool work wait failed");
            }
#else
            if (pthread_cond_wait(&pool->work_ready, &pool->lock) != 0) {
                fdn_panic_cstr("parallel pool work wait failed");
            }
#endif
        }
        if (pool->pending_head == NULL && pool->stopping) {
            fdn_parallel_unlock(pool);
            break;
        }
        job = fdn_parallel_pop(pool);
        fdn_parallel_unlock(pool);
        if (job == NULL) {
            fdn_panic_cstr("parallel pool lost pending work");
        }

        fdn_task_run_transferred(job->task, fdn_parallel_cancellation_requested, job);
        job->task = NULL;

        fdn_parallel_lock(pool);
        fdn_parallel_remove(pool, job);
        fdn_parallel_unlock(pool);
        fdn_dealloc(job);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void fdn_parallel_join(fdn_parallel_pool *pool, bool cancel) {
    fdn_parallel_lock(pool);
    if (pool->joined) {
        fdn_parallel_unlock(pool);
        return;
    }
    pool->stopping = true;
    if (cancel) {
        fdn_parallel_job *job = pool->all_head;
        while (job != NULL) {
            fdn_parallel_request_cancellation(job);
            job = job->all_next;
        }
    }
    fdn_parallel_broadcast(pool);
    fdn_parallel_unlock(pool);

    for (size_t index = 0; index < pool->worker_count; ++index) {
#if defined(_WIN32)
        if (WaitForSingleObject(pool->workers[index], INFINITE) != WAIT_OBJECT_0 ||
            CloseHandle(pool->workers[index]) == 0) {
            fdn_panic_cstr("parallel pool worker join failed");
        }
#else
        if (pthread_join(pool->workers[index], NULL) != 0) {
            fdn_panic_cstr("parallel pool worker join failed");
        }
#endif
    }

    fdn_parallel_lock(pool);
    if (pool->jobs != 0 || pool->pending_head != NULL || pool->pending_tail != NULL ||
        pool->all_head != NULL) {
        fdn_panic_cstr("parallel pool joined with live work");
    }
    pool->joined = true;
    fdn_parallel_unlock(pool);
}

uint64_t foundation_runtime_pool_open(uint64_t workers) {
#if defined(_WIN32)
    const size_t worker_size = sizeof(HANDLE);
#else
    const size_t worker_size = sizeof(pthread_t);
#endif
    if (workers == 0 || workers > (uint64_t)(SIZE_MAX / worker_size)) {
        fdn_panic_cstr("parallel pool requires a valid worker count");
    }
    fdn_parallel_pool *pool = fdn_alloc(sizeof(*pool));
    pool->pending_head = NULL;
    pool->pending_tail = NULL;
    pool->all_head = NULL;
    pool->jobs = 0;
    pool->worker_count = 0;
    pool->stopping = false;
    pool->joined = false;
    pool->workers = fdn_alloc((size_t)workers * sizeof(*pool->workers));
#if defined(_WIN32)
    InitializeCriticalSection(&pool->lock);
    InitializeConditionVariable(&pool->work_ready);
#else
    if (pthread_mutex_init(&pool->lock, NULL) != 0 ||
        pthread_cond_init(&pool->work_ready, NULL) != 0) {
        fdn_panic_cstr("parallel pool initialization failed");
    }
#endif
    for (size_t index = 0; index < (size_t)workers; ++index) {
#if defined(_WIN32)
        pool->workers[index] = CreateThread(NULL, 0, fdn_parallel_worker, pool, 0, NULL);
        if (pool->workers[index] == NULL) {
            fdn_panic_cstr("parallel pool worker creation failed");
        }
#else
        if (pthread_create(&pool->workers[index], NULL, fdn_parallel_worker, pool) != 0) {
            fdn_panic_cstr("parallel pool worker creation failed");
        }
#endif
        ++pool->worker_count;
    }
#if defined(_WIN32)
    if (InterlockedIncrement64(&fdn_parallel_pool_count) <= 0) {
        fdn_panic_cstr("parallel pool count overflow");
    }
#else
    if (atomic_fetch_add_explicit(&fdn_parallel_pool_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("parallel pool count overflow");
    }
#endif
    return (uint64_t)(uintptr_t)pool;
}

void foundation_runtime_pool_submit(uint64_t handle, fdn_task *task) {
    fdn_parallel_pool *pool = fdn_parallel_from_handle(handle);
    if (task == NULL) {
        fdn_panic_cstr("parallel pool requires a task");
    }
    fdn_parallel_job *job = fdn_alloc(sizeof(*job));
    job->pending_next = NULL;
    job->all_next = NULL;
    job->task = task;
#if defined(_WIN32)
    job->cancellation_requested = 0;
#else
    atomic_init(&job->cancellation_requested, false);
#endif

    fdn_parallel_lock(pool);
    if (pool->stopping || pool->joined) {
        fdn_panic_cstr("parallel pool is closed");
    }
    if (pool->jobs == SIZE_MAX) {
        fdn_panic_cstr("parallel pool job count overflow");
    }
    fdn_task_transfer_out(task);
    if (pool->pending_tail == NULL) {
        pool->pending_head = job;
    } else {
        pool->pending_tail->pending_next = job;
    }
    pool->pending_tail = job;
    job->all_next = pool->all_head;
    pool->all_head = job;
    ++pool->jobs;
    fdn_parallel_signal(pool);
    fdn_parallel_unlock(pool);
}

void foundation_runtime_pool_wait(uint64_t handle) {
    fdn_parallel_join(fdn_parallel_from_handle(handle), false);
}

void foundation_runtime_pool_cancel(uint64_t handle) {
    fdn_parallel_join(fdn_parallel_from_handle(handle), true);
}

void foundation_runtime_pool_release(uint64_t handle) {
    fdn_parallel_pool *pool = fdn_parallel_from_handle(handle);
    if (!pool->joined || pool->jobs != 0) {
        fdn_panic_cstr("parallel pool must join before release");
    }
#if defined(_WIN32)
    DeleteCriticalSection(&pool->lock);
    if (InterlockedDecrement64(&fdn_parallel_pool_count) < 0) {
        fdn_panic_cstr("parallel pool count underflow");
    }
#else
    if (pthread_cond_destroy(&pool->work_ready) != 0 ||
        pthread_mutex_destroy(&pool->lock) != 0) {
        fdn_panic_cstr("parallel pool destroy failed");
    }
    if (atomic_fetch_sub_explicit(&fdn_parallel_pool_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("parallel pool count underflow");
    }
#endif
    fdn_dealloc(pool->workers);
    fdn_dealloc(pool);
}

uint64_t foundation_runtime_pool_live_count(void) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(&fdn_parallel_pool_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_parallel_pool_count, memory_order_relaxed);
#endif
}
