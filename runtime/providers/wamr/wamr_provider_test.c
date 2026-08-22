#include "foundation/wamr_provider.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_block(void);
FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_wait(void);
FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_engine_close_wait(void);
FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_release(void);

typedef struct provider_open_job {
    const fdn_wamr_provider_v2 *provider;
    uint64_t engine;
    fdn_string path;
    uint64_t module;
    int32_t status;
} provider_open_job;

typedef struct provider_close_job {
    const fdn_wamr_provider_v2 *provider;
    uint64_t engine;
    int32_t status;
} provider_close_job;

#if defined(_WIN32)
static DWORD WINAPI provider_open_worker(LPVOID context) {
#else
static void *provider_open_worker(void *context) {
#endif
    provider_open_job *job = context;
    job->status = job->provider->module_open(
        job->engine, &job->path, UINT64_C(1048576), &job->module
    );
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

#if defined(_WIN32)
static DWORD WINAPI provider_close_worker(LPVOID context) {
#else
static void *provider_close_worker(void *context) {
#endif
    provider_close_job *job = context;
    job->status = job->provider->engine_close(&job->engine);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int close_during_open(const fdn_wamr_provider_v2 *provider,
                             const fdn_string *path) {
    provider_open_job open_job;
    provider_close_job close_job;
    uint64_t stale;
    int32_t status;
#if defined(_WIN32)
    HANDLE open_thread;
    HANDLE close_thread;
#else
    pthread_t open_thread;
    pthread_t close_thread;
#endif

    (void)memset(&open_job, 0, sizeof(open_job));
    (void)memset(&close_job, 0, sizeof(close_job));
    open_job.provider = provider;
    open_job.path = *path;
    close_job.provider = provider;
    status = provider->engine_open(&open_job.engine);
    if (status != FDN_WAMR_OK || open_job.engine == 0) return 10;
    stale = open_job.engine;
    close_job.engine = open_job.engine;
    foundation_wamr_provider_test_open_block();
#if defined(_WIN32)
    open_thread = CreateThread(NULL, 0, provider_open_worker, &open_job, 0, NULL);
    if (open_thread == NULL) return 11;
#else
    if (pthread_create(&open_thread, NULL, provider_open_worker, &open_job) != 0)
        return 11;
#endif
    foundation_wamr_provider_test_open_wait();
#if defined(_WIN32)
    close_thread = CreateThread(NULL, 0, provider_close_worker, &close_job, 0, NULL);
    if (close_thread == NULL) return 12;
#else
    if (pthread_create(&close_thread, NULL, provider_close_worker, &close_job) != 0)
        return 12;
#endif
    foundation_wamr_provider_test_engine_close_wait();
    foundation_wamr_provider_test_open_release();
#if defined(_WIN32)
    if (WaitForSingleObject(open_thread, INFINITE) != WAIT_OBJECT_0 ||
        WaitForSingleObject(close_thread, INFINITE) != WAIT_OBJECT_0) return 13;
    (void)CloseHandle(open_thread);
    (void)CloseHandle(close_thread);
#else
    if (pthread_join(open_thread, NULL) != 0 ||
        pthread_join(close_thread, NULL) != 0) return 13;
#endif
    if (open_job.status != FDN_WAMR_CLOSED || open_job.module != 0) return 14;
    if (close_job.status != FDN_WAMR_OK || close_job.engine != 0) return 15;
    status = provider->engine_close(&stale);
    if (status != FDN_WAMR_CLOSED || stale != 0) return 16;
    return 0;
}

int main(int argc, char **argv) {
    const fdn_wamr_provider_v2 *provider = foundation_wamr_provider_query();
    fdn_string path;
    int iteration;
    int status;

    if (argc != 2 || provider == NULL) return 1;
    path.data = argv[1];
    path.length = strlen(argv[1]);
    path.owned = 0;
    for (iteration = 0; iteration < 8; ++iteration) {
        status = close_during_open(provider, &path);
        if (status != 0) {
            (void)fprintf(stderr, "close_during_open failed: %d\n", status);
            return status;
        }
    }
    return 0;
}
