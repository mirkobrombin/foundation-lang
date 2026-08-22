#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "foundation/wamr_provider.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_block(void);
FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_wait(void);
FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_release(void);
FDN_WAMR_PROVIDER_EXPORT uint64_t foundation_wamr_provider_test_live_engines(void);
FDN_WAMR_PROVIDER_EXPORT uint64_t foundation_wamr_provider_test_live_modules(void);
void foundation_runtime_wamr_test_timeout_block(void);
void foundation_runtime_wamr_test_timeout_wait(void);
void foundation_runtime_wamr_test_timeout_release(void);

typedef struct open_job {
    uint64_t engine;
    uint64_t module;
    int32_t status;
} open_job;

typedef struct call_job {
    uint64_t module;
    uint64_t input;
    uint64_t output;
    int32_t status;
} call_job;

static fdn_string text(const char *value) {
    fdn_string result;
    result.data = value;
    result.length = strlen(value);
    result.owned = 0;
    return result;
}

static int wait_for_provider_idle(void) {
    int attempt;
    for (attempt = 0; attempt < 1000; ++attempt) {
        if (foundation_wamr_provider_test_live_engines() == 0 &&
            foundation_wamr_provider_test_live_modules() == 0) {
            return 1;
        }
#if defined(_WIN32)
        Sleep(1);
#else
        const struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
#endif
    }
    return 0;
}

#if defined(_WIN32)
static DWORD WINAPI open_worker(LPVOID context) {
#else
static void *open_worker(void *context) {
#endif
    open_job *job = context;
    const fdn_string path = text("blocked.wasm");
    job->status = foundation_runtime_wamr_module_open(
        job->engine, &path, UINT64_C(1048576), &job->module
    );
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

#if defined(_WIN32)
static DWORD WINAPI call_worker(LPVOID context) {
#else
static void *call_worker(void *context) {
#endif
    call_job *job = context;
    const fdn_string method = text("hang");
    job->status = foundation_runtime_wamr_module_call(
        job->module, &method, job->input, UINT64_C(1000000), &job->output
    );
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int close_during_open(const fdn_string *adapter) {
    open_job job;
    uint64_t engine = 0;
    uint64_t stale;
    int32_t status;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif

    (void)memset(&job, 0, sizeof(job));
    status = foundation_runtime_wamr_engine_open(adapter, &engine);
    if (status != FDN_WAMR_OK || engine == 0) return 10;
    stale = engine;
    job.engine = engine;
    foundation_wamr_provider_test_open_block();
#if defined(_WIN32)
    thread = CreateThread(NULL, 0, open_worker, &job, 0, NULL);
    if (thread == NULL) return 11;
#else
    if (pthread_create(&thread, NULL, open_worker, &job) != 0) return 11;
#endif
    foundation_wamr_provider_test_open_wait();
    status = foundation_runtime_wamr_engine_close(&engine);
    foundation_wamr_provider_test_open_release();
#if defined(_WIN32)
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) return 13;
    (void)CloseHandle(thread);
#else
    if (pthread_join(thread, NULL) != 0) return 13;
#endif
    if (status != FDN_WAMR_OK || engine != 0) return 12;
    if (job.status != FDN_WAMR_CLOSED || job.module != 0) {
        (void)fprintf(stderr, "close_during_open result: status=%d module=%llu\n",
                      job.status, (unsigned long long)job.module);
        return 14;
    }
    status = foundation_runtime_wamr_engine_close(&stale);
    if (status != FDN_WAMR_CLOSED || stale != 0) return 15;
    return 0;
}

static int close_with_live_module(const fdn_string *adapter) {
    const fdn_string path = text("fixture.wasm");
    uint64_t engine = 0;
    uint64_t stale_engine;
    uint64_t module = 0;
    uint64_t stale_module;
    int32_t status;

    status = foundation_runtime_wamr_engine_open(adapter, &engine);
    if (status != FDN_WAMR_OK || engine == 0) return 20;
    status = foundation_runtime_wamr_module_open(
        engine, &path, UINT64_C(1048576), &module
    );
    if (status != FDN_WAMR_OK || module == 0) {
        (void)fprintf(stderr, "close_with_live_module open: status=%d module=%llu\n",
                      status, (unsigned long long)module);
        return 21;
    }
    stale_engine = engine;
    stale_module = module;
    status = foundation_runtime_wamr_engine_close(&engine);
    if (status != FDN_WAMR_OK || engine != 0) return 22;
    status = foundation_runtime_wamr_module_close(&module);
    if (status != FDN_WAMR_OK || module != 0) return 23;
    status = foundation_runtime_wamr_engine_close(&stale_engine);
    if (status != FDN_WAMR_CLOSED || stale_engine != 0) return 24;
    status = foundation_runtime_wamr_module_close(&stale_module);
    if (status != FDN_WAMR_CLOSED || stale_module != 0) return 25;
    return 0;
}

static int close_during_timeout(const fdn_string *adapter) {
    const fdn_string path = text("fixture.wasm");
    const fdn_string write_path = text(".");
    const fdn_string key = text("MODE");
    const fdn_string value = text("test");
    const fdn_string argument = text("fixture");
    const fdn_string input = text("x");
    call_job job;
    uint64_t engine = 0;
    uint64_t module = 0;
    uint64_t stale_module;
    int unexpected_output;
    int32_t status;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif

    (void)memset(&job, 0, sizeof(job));
    status = foundation_runtime_wamr_engine_open(adapter, &engine);
    if (status != FDN_WAMR_OK || engine == 0) return 30;
    status = foundation_runtime_wamr_module_open(
        engine, &path, UINT64_C(1048576), &module
    );
    if (status != FDN_WAMR_OK || module == 0) return 31;
    if (foundation_runtime_wamr_module_allow_write(module, &write_path) != FDN_WAMR_OK ||
        foundation_runtime_wamr_module_set_environment(module, &key, &value) != FDN_WAMR_OK ||
        foundation_runtime_wamr_module_add_argument(module, &argument) != FDN_WAMR_OK ||
        foundation_runtime_wamr_module_prepare_timed(module, UINT64_C(1000000000)) != FDN_WAMR_OK ||
        foundation_runtime_wamr_module_start_timed(module, UINT64_C(1000000000)) != FDN_WAMR_OK) {
        return 32;
    }
    job.module = module;
    job.input = foundation_runtime_bytes_from_text(&input);
    if (job.input == 0) return 33;
    foundation_runtime_wamr_test_timeout_block();
#if defined(_WIN32)
    thread = CreateThread(NULL, 0, call_worker, &job, 0, NULL);
    if (thread == NULL) return 34;
#else
    if (pthread_create(&thread, NULL, call_worker, &job) != 0) return 34;
#endif
    foundation_runtime_wamr_test_timeout_wait();
    stale_module = module;
    status = foundation_runtime_wamr_module_close(&module);
    if (status != FDN_WAMR_OK || module != 0) {
        foundation_runtime_wamr_test_timeout_release();
        return 35;
    }
    foundation_runtime_wamr_test_timeout_release();
#if defined(_WIN32)
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) return 36;
    (void)CloseHandle(thread);
#else
    if (pthread_join(thread, NULL) != 0) return 36;
#endif
    unexpected_output = job.output != 0;
    status = job.status;
    foundation_runtime_bytes_close(&job.input);
    foundation_runtime_bytes_close(&job.output);
    if (status != FDN_WAMR_CLOSED || unexpected_output) return 37;
    status = foundation_runtime_wamr_module_close(&stale_module);
    if (status != FDN_WAMR_CLOSED || stale_module != 0) return 38;
    status = foundation_runtime_wamr_engine_close(&engine);
    if (status != FDN_WAMR_OK || engine != 0) return 39;
    if (!wait_for_provider_idle()) {
        (void)fprintf(stderr, "provider remains live: engines=%llu modules=%llu\n",
                      (unsigned long long)foundation_wamr_provider_test_live_engines(),
                      (unsigned long long)foundation_wamr_provider_test_live_modules());
        return 40;
    }
    return 0;
}

int main(int argc, char **argv) {
    fdn_string adapter;
    int iteration;
    int status;

    if (argc != 2) return 1;
    adapter = text(argv[1]);
    for (iteration = 0; iteration < 32; ++iteration) {
        status = close_during_open(&adapter);
        if (status != 0) {
            (void)fprintf(stderr, "close_during_open failed: %d\n", status);
            return status;
        }
        status = close_with_live_module(&adapter);
        if (status != 0) {
            (void)fprintf(stderr, "close_with_live_module failed: %d\n", status);
            return status;
        }
        status = close_during_timeout(&adapter);
        if (status != 0) {
            (void)fprintf(stderr, "close_during_timeout failed: %d\n", status);
            return status;
        }
    }
    return 0;
}
