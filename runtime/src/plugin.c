#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/plugin.h"
#include "foundation/runtime.h"
#include "foundation/wamr_provider.h"
#include "foundation/wasm_plugin.h"
#include "bytes_internal.h"

#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
               "plugin handles require a 64-bit carrier");

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE fdn_plugin_module;
static volatile LONG64 fdn_plugin_live_count;
#else
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
typedef void *fdn_plugin_module;
static atomic_uint_fast64_t fdn_plugin_live_count;
#endif

enum {
    FDN_PLUGIN_OK = 0,
    FDN_PLUGIN_INVALID_PATH = 1,
    FDN_PLUGIN_LOAD_FAILED = 2,
    FDN_PLUGIN_MISSING_QUERY = 3,
    FDN_PLUGIN_QUERY_FAILED = 4,
    FDN_PLUGIN_ABI_MISMATCH = 5,
    /* Reserved by plugin ABI v1; SDK identity is now diagnostic metadata. */
    FDN_PLUGIN_SDK_MISMATCH = 6,
    FDN_PLUGIN_TARGET_MISMATCH = 7,
    FDN_PLUGIN_CONTRACT_MISMATCH = 8,
    FDN_PLUGIN_INVALID_DESCRIPTOR = 9,
    FDN_PLUGIN_CREATE_FAILED = 10,
    FDN_PLUGIN_LIFECYCLE_FAILED = 11,
    FDN_PLUGIN_CLOSED = 12,
};

typedef struct fdn_loaded_plugin {
    fdn_plugin_module module;
    fdn_plugin_descriptor_v1 descriptor;
    void *context;
    int started;
} fdn_loaded_plugin;

static fdn_plugin_text_v1 fdn_plugin_text(const char *value) {
    const fdn_plugin_text_v1 result = {value, strlen(value)};
    return result;
}

static int fdn_plugin_text_valid(fdn_plugin_text_v1 value, size_t maximum,
                                 int allow_empty) {
    size_t offset;
    if (value.length > maximum || (!allow_empty && value.length == 0) ||
        (value.length != 0 && value.data == NULL)) {
        return 0;
    }
    for (offset = 0; offset < value.length; ++offset) {
        if (value.data[offset] == '\0') {
            return 0;
        }
    }
    return fdn_utf8_valid(value.data, value.length) ? 1 : 0;
}

static void fdn_plugin_set_string(fdn_string *output, const char *value,
                                  size_t length) {
    char *copy;
    if (output == NULL) {
        return;
    }
    fdn_string_drop(output);
    if (length == 0) {
        *output = fdn_string_static("", 0);
        return;
    }
    copy = fdn_alloc(length);
    (void)memcpy(copy, value, length);
    output->data = copy;
    output->length = length;
    output->owned = 1;
}

static void fdn_plugin_set_detail(fdn_string *detail, fdn_plugin_text_v1 value,
                                  const char *fallback) {
    if (fdn_plugin_text_valid(value, 65536, 0)) {
        fdn_plugin_set_string(detail, value.data, value.length);
        return;
    }
    fdn_plugin_set_string(detail, fallback, strlen(fallback));
}

static int fdn_plugin_text_equal(fdn_plugin_text_v1 left,
                                 fdn_plugin_text_v1 right) {
    return left.length == right.length &&
           (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

static fdn_plugin_host_v1 fdn_plugin_host(void) {
    fdn_plugin_host_v1 host;
    (void)memset(&host, 0, sizeof(host));
    host.struct_size = (uint32_t)sizeof(host);
    host.abi_major = FDN_PLUGIN_ABI_MAJOR;
    host.abi_minor = FDN_PLUGIN_ABI_MINOR;
    host.sdk_major = FDN_PLUGIN_SDK_MAJOR;
    host.sdk_minor = FDN_PLUGIN_SDK_MINOR;
    host.target_os = fdn_plugin_text(FDN_PLUGIN_TARGET_OS);
    host.target_arch = fdn_plugin_text(FDN_PLUGIN_TARGET_ARCH);
    host.allocate = fdn_alloc;
    host.deallocate = fdn_dealloc;
    return host;
}

static void fdn_plugin_count_add(void) {
#if defined(_WIN32)
    if (InterlockedIncrement64(&fdn_plugin_live_count) <= 0) {
        fdn_panic_cstr("plugin handle count overflow");
    }
#else
    if (atomic_fetch_add_explicit(&fdn_plugin_live_count, 1,
                                  memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("plugin handle count overflow");
    }
#endif
}

static void fdn_plugin_count_remove(void) {
#if defined(_WIN32)
    if (InterlockedDecrement64(&fdn_plugin_live_count) < 0) {
        fdn_panic_cstr("plugin handle count underflow");
    }
#else
    if (atomic_fetch_sub_explicit(&fdn_plugin_live_count, 1,
                                  memory_order_relaxed) == 0) {
        fdn_panic_cstr("plugin handle count underflow");
    }
#endif
}

uint64_t foundation_runtime_plugin_live_handles(void) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(&fdn_plugin_live_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_plugin_live_count, memory_order_relaxed);
#endif
}

#if defined(_WIN32)
static wchar_t *fdn_plugin_windows_path(const fdn_string *path) {
    int length;
    wchar_t *result;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        path->length > (size_t)INT_MAX || !fdn_utf8_valid(path->data, path->length)) {
        return NULL;
    }
    if (memchr(path->data, '\0', path->length) != NULL) {
        return NULL;
    }
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                                 (int)path->length, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    result = fdn_alloc(((size_t)length + 1) * sizeof(wchar_t));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                            (int)path->length, result, length) != length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[length] = L'\0';
    return result;
}

static fdn_plugin_module fdn_plugin_module_open(const fdn_string *path) {
    wchar_t *native = fdn_plugin_windows_path(path);
    fdn_plugin_module module;
    if (native == NULL) {
        return NULL;
    }
    module = LoadLibraryW(native);
    fdn_dealloc(native);
    return module;
}

static void *fdn_plugin_module_symbol(fdn_plugin_module module) {
    FARPROC symbol = GetProcAddress(module, FDN_PLUGIN_QUERY_SYMBOL);
    void *result = NULL;
    _Static_assert(sizeof(symbol) == sizeof(result),
                   "plugin function and data pointers must have equal size");
    (void)memcpy(&result, &symbol, sizeof(result));
    return result;
}

static void fdn_plugin_module_close(fdn_plugin_module module) {
    (void)FreeLibrary(module);
}
#else
static char *fdn_plugin_native_path(const fdn_string *path) {
    char *result;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        !fdn_utf8_valid(path->data, path->length) ||
        memchr(path->data, '\0', path->length) != NULL) {
        return NULL;
    }
    result = fdn_alloc(path->length + 1);
    (void)memcpy(result, path->data, path->length);
    result[path->length] = '\0';
    return result;
}

static fdn_plugin_module fdn_plugin_module_open(const fdn_string *path) {
    char *native = fdn_plugin_native_path(path);
    fdn_plugin_module module;
    if (native == NULL) {
        return NULL;
    }
    module = dlopen(native, RTLD_NOW | RTLD_LOCAL);
    fdn_dealloc(native);
    return module;
}

static void *fdn_plugin_module_symbol(fdn_plugin_module module) {
    return dlsym(module, FDN_PLUGIN_QUERY_SYMBOL);
}

static void fdn_plugin_module_close(fdn_plugin_module module) {
    (void)dlclose(module);
}
#endif

static int32_t fdn_plugin_validate(const fdn_plugin_host_v1 *host,
                                   const fdn_plugin_descriptor_v1 *descriptor,
                                   fdn_string *detail) {
    if (descriptor->struct_size < sizeof(*descriptor) ||
        descriptor->abi_major != host->abi_major ||
        descriptor->abi_minor > host->abi_minor) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin ABI mismatch"),
                              "plugin ABI mismatch");
        return FDN_PLUGIN_ABI_MISMATCH;
    }
    if (!fdn_plugin_text_valid(descriptor->target_os, 32, 0) ||
        !fdn_plugin_text_valid(descriptor->target_arch, 32, 0)) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("invalid plugin descriptor"),
                              "invalid plugin descriptor");
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    if (!fdn_plugin_text_equal(descriptor->target_os, host->target_os) ||
        !fdn_plugin_text_equal(descriptor->target_arch, host->target_arch)) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin target mismatch"),
                              "plugin target mismatch");
        return FDN_PLUGIN_TARGET_MISMATCH;
    }
    if (descriptor->contract_hash != FDN_PLUGIN_LIFECYCLE_CONTRACT_HASH) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin contract mismatch"),
                              "plugin contract mismatch");
        return FDN_PLUGIN_CONTRACT_MISMATCH;
    }
    if (!fdn_plugin_text_valid(descriptor->name, 256, 0) ||
        descriptor->create == NULL ||
        descriptor->start == NULL || descriptor->stop == NULL ||
        descriptor->destroy == NULL) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("invalid plugin descriptor"),
                              "invalid plugin descriptor");
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    return FDN_PLUGIN_OK;
}

int32_t foundation_runtime_plugin_open(const fdn_string *path, uint64_t *handle,
                                       fdn_string *name, fdn_string *detail) {
    fdn_plugin_module module;
    void *symbol;
    fdn_plugin_query_v1_fn query = NULL;
    fdn_plugin_descriptor_v1 descriptor;
    fdn_plugin_text_v1 query_error = {NULL, 0};
    fdn_plugin_text_v1 create_error = {NULL, 0};
    fdn_plugin_host_v1 host;
    fdn_loaded_plugin *loaded;
    void *context = NULL;
    int32_t status;

    if (handle == NULL || name == NULL || detail == NULL) {
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    *handle = 0;
    fdn_string_drop(name);
    *name = fdn_string_static("", 0);
    fdn_string_drop(detail);
    *detail = fdn_string_static("", 0);
    module = fdn_plugin_module_open(path);
    if (module == NULL) {
        if (path == NULL || path->data == NULL || path->length == 0 ||
            !fdn_utf8_valid(path->data, path->length) ||
            memchr(path->data, '\0', path->length) != NULL) {
            fdn_plugin_set_detail(detail, fdn_plugin_text("invalid plugin path"),
                                  "invalid plugin path");
            return FDN_PLUGIN_INVALID_PATH;
        }
        fdn_plugin_set_detail(detail, fdn_plugin_text("dynamic library load failed"),
                              "dynamic library load failed");
        return FDN_PLUGIN_LOAD_FAILED;
    }
    symbol = fdn_plugin_module_symbol(module);
    if (symbol == NULL) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin query symbol is missing"),
                              "plugin query symbol is missing");
        fdn_plugin_module_close(module);
        return FDN_PLUGIN_MISSING_QUERY;
    }
    _Static_assert(sizeof(query) == sizeof(symbol),
                   "plugin function and data pointers must have equal size");
    (void)memcpy(&query, &symbol, sizeof(query));
    (void)memset(&descriptor, 0, sizeof(descriptor));
    host = fdn_plugin_host();
    if (query(&host, &descriptor, &query_error) != 0) {
        fdn_plugin_set_detail(detail, query_error, "plugin query failed");
        fdn_plugin_module_close(module);
        return FDN_PLUGIN_QUERY_FAILED;
    }
    status = fdn_plugin_validate(&host, &descriptor, detail);
    if (status != FDN_PLUGIN_OK) {
        fdn_plugin_module_close(module);
        return status;
    }
    if (descriptor.create(&host, &context, &create_error) != 0 || context == NULL) {
        fdn_plugin_set_detail(detail, create_error, "plugin creation failed");
        if (context != NULL) {
            descriptor.destroy(context);
        }
        fdn_plugin_module_close(module);
        return FDN_PLUGIN_CREATE_FAILED;
    }
    loaded = fdn_alloc(sizeof(*loaded));
    loaded->module = module;
    loaded->descriptor = descriptor;
    loaded->context = context;
    loaded->started = 0;
    fdn_plugin_set_string(name, descriptor.name.data, descriptor.name.length);
    *handle = (uint64_t)(uintptr_t)loaded;
    fdn_plugin_count_add();
    return FDN_PLUGIN_OK;
}

int32_t foundation_runtime_plugin_start(uint64_t handle, fdn_string *detail) {
    fdn_loaded_plugin *loaded = (fdn_loaded_plugin *)(uintptr_t)handle;
    fdn_plugin_text_v1 error = {NULL, 0};
    if (detail == NULL) {
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    fdn_string_drop(detail);
    *detail = fdn_string_static("", 0);
    if (loaded == NULL) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin is closed"),
                              "plugin is closed");
        return FDN_PLUGIN_CLOSED;
    }
    if (loaded->started) {
        return FDN_PLUGIN_OK;
    }
    if (loaded->descriptor.start(loaded->context, &error) != 0) {
        fdn_plugin_set_detail(detail, error, "plugin start failed");
        return FDN_PLUGIN_LIFECYCLE_FAILED;
    }
    loaded->started = 1;
    return FDN_PLUGIN_OK;
}

int32_t foundation_runtime_plugin_stop(uint64_t handle, fdn_string *detail) {
    fdn_loaded_plugin *loaded = (fdn_loaded_plugin *)(uintptr_t)handle;
    fdn_plugin_text_v1 error = {NULL, 0};
    if (detail == NULL) {
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    fdn_string_drop(detail);
    *detail = fdn_string_static("", 0);
    if (loaded == NULL) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin is closed"),
                              "plugin is closed");
        return FDN_PLUGIN_CLOSED;
    }
    if (!loaded->started) {
        return FDN_PLUGIN_OK;
    }
    if (loaded->descriptor.stop(loaded->context, &error) != 0) {
        fdn_plugin_set_detail(detail, error, "plugin stop failed");
        return FDN_PLUGIN_LIFECYCLE_FAILED;
    }
    loaded->started = 0;
    return FDN_PLUGIN_OK;
}

int32_t foundation_runtime_plugin_close(uint64_t *handle, fdn_string *detail) {
    fdn_loaded_plugin *loaded;
    int32_t status = FDN_PLUGIN_OK;
    if (handle == NULL || detail == NULL) {
        return FDN_PLUGIN_INVALID_DESCRIPTOR;
    }
    loaded = (fdn_loaded_plugin *)(uintptr_t)*handle;
    *handle = 0;
    fdn_string_drop(detail);
    *detail = fdn_string_static("", 0);
    if (loaded == NULL) {
        return FDN_PLUGIN_OK;
    }
    if (loaded->started) {
        fdn_plugin_text_v1 error = {NULL, 0};
        if (loaded->descriptor.stop(loaded->context, &error) != 0) {
            fdn_plugin_set_detail(detail, error, "plugin stop failed during close");
            status = FDN_PLUGIN_LIFECYCLE_FAILED;
        }
    }
    loaded->descriptor.destroy(loaded->context);
    fdn_plugin_module_close(loaded->module);
    fdn_dealloc(loaded);
    fdn_plugin_count_remove();
    return status;
}

typedef struct fdn_wamr_bridge_mutex {
#if defined(_WIN32)
    SRWLOCK value;
#else
    pthread_mutex_t value;
#endif
} fdn_wamr_bridge_mutex;

static void fdn_wamr_bridge_lock(fdn_wamr_bridge_mutex *lock);
static void fdn_wamr_bridge_unlock(fdn_wamr_bridge_mutex *lock);

#if defined(FOUNDATION_WAMR_BRIDGE_TESTING)
#if defined(_WIN32)
static fdn_wamr_bridge_mutex fdn_wamr_timeout_test_lock = {SRWLOCK_INIT};
static CONDITION_VARIABLE fdn_wamr_timeout_test_ready = CONDITION_VARIABLE_INIT;
#else
static fdn_wamr_bridge_mutex fdn_wamr_timeout_test_lock = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t fdn_wamr_timeout_test_ready = PTHREAD_COND_INITIALIZER;
#endif
static int fdn_wamr_timeout_test_blocked;
static int fdn_wamr_timeout_test_entered;

void foundation_runtime_wamr_test_timeout_block(void) {
    fdn_wamr_bridge_lock(&fdn_wamr_timeout_test_lock);
    fdn_wamr_timeout_test_blocked = 1;
    fdn_wamr_timeout_test_entered = 0;
    fdn_wamr_bridge_unlock(&fdn_wamr_timeout_test_lock);
}

void foundation_runtime_wamr_test_timeout_wait(void) {
    fdn_wamr_bridge_lock(&fdn_wamr_timeout_test_lock);
    while (!fdn_wamr_timeout_test_entered) {
#if defined(_WIN32)
        if (!SleepConditionVariableSRW(&fdn_wamr_timeout_test_ready,
                                       &fdn_wamr_timeout_test_lock.value,
                                       INFINITE, 0)) {
            fdn_panic_cstr("WAMR timeout test wait failed");
        }
#else
        if (pthread_cond_wait(&fdn_wamr_timeout_test_ready,
                              &fdn_wamr_timeout_test_lock.value) != 0) {
            fdn_panic_cstr("WAMR timeout test wait failed");
        }
#endif
    }
    fdn_wamr_bridge_unlock(&fdn_wamr_timeout_test_lock);
}

void foundation_runtime_wamr_test_timeout_release(void) {
    fdn_wamr_bridge_lock(&fdn_wamr_timeout_test_lock);
    fdn_wamr_timeout_test_blocked = 0;
#if defined(_WIN32)
    WakeAllConditionVariable(&fdn_wamr_timeout_test_ready);
#else
    if (pthread_cond_broadcast(&fdn_wamr_timeout_test_ready) != 0) {
        fdn_panic_cstr("WAMR timeout test signal failed");
    }
#endif
    fdn_wamr_bridge_unlock(&fdn_wamr_timeout_test_lock);
}

static void fdn_wamr_timeout_test_barrier(void) {
    fdn_wamr_bridge_lock(&fdn_wamr_timeout_test_lock);
    if (fdn_wamr_timeout_test_blocked) {
        fdn_wamr_timeout_test_entered = 1;
#if defined(_WIN32)
        WakeAllConditionVariable(&fdn_wamr_timeout_test_ready);
        while (fdn_wamr_timeout_test_blocked) {
            if (!SleepConditionVariableSRW(&fdn_wamr_timeout_test_ready,
                                           &fdn_wamr_timeout_test_lock.value,
                                           INFINITE, 0)) {
                fdn_panic_cstr("WAMR timeout test barrier failed");
            }
        }
#else
        if (pthread_cond_broadcast(&fdn_wamr_timeout_test_ready) != 0) {
            fdn_panic_cstr("WAMR timeout test signal failed");
        }
        while (fdn_wamr_timeout_test_blocked) {
            if (pthread_cond_wait(&fdn_wamr_timeout_test_ready,
                                  &fdn_wamr_timeout_test_lock.value) != 0) {
                fdn_panic_cstr("WAMR timeout test barrier failed");
            }
        }
#endif
    }
    fdn_wamr_bridge_unlock(&fdn_wamr_timeout_test_lock);
}
#else
static void fdn_wamr_timeout_test_barrier(void) {
}
#endif

typedef struct fdn_wamr_engine_bridge {
    fdn_plugin_module library;
    const fdn_wamr_provider_v2 *provider;
    uint64_t engine;
    fdn_wamr_bridge_mutex lock;
    size_t in_flight;
    size_t module_count;
    int close_requested;
    struct fdn_wamr_engine_slot *slot;
} fdn_wamr_engine_bridge;

typedef struct fdn_wamr_engine_slot {
    struct fdn_wamr_engine_slot *next;
    uint32_t index;
    uint32_t generation;
    fdn_wamr_engine_bridge *bridge;
} fdn_wamr_engine_slot;

typedef struct fdn_wamr_capability_binding {
    struct fdn_wamr_capability_binding *next;
    fdn_string name;
    uint64_t context;
    fdn_wamr_capability_fn handler;
} fdn_wamr_capability_binding;

typedef struct fdn_wamr_module_slot {
    struct fdn_wamr_module_slot *next;
    uint32_t index;
    uint32_t generation;
    struct fdn_wamr_module_bridge *bridge;
} fdn_wamr_module_slot;

typedef struct fdn_wamr_module_bridge {
    fdn_wamr_engine_bridge *engine;
    uint64_t module;
    uint64_t payload_limit;
    fdn_wamr_bridge_mutex lock;
    size_t in_flight;
    int closing;
    int configuring;
    int prepared;
    int started;
    int module_released;
    fdn_wamr_capability_binding *capabilities;
    fdn_wamr_module_slot *slot;
} fdn_wamr_module_bridge;

typedef struct fdn_wamr_call_job {
    fdn_wamr_module_bridge *bridge;
    fdn_string method;
    uint8_t *input;
    size_t input_length;
    uint8_t *output;
    size_t output_length;
    int32_t status;
    int operation;
    unsigned int references;
    int complete;
    int abandoned;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE ready;
#else
    pthread_mutex_t lock;
    pthread_cond_t ready;
#endif
} fdn_wamr_call_job;

enum {
    FDN_WAMR_EXECUTE_CALL,
    FDN_WAMR_EXECUTE_PREPARE,
    FDN_WAMR_EXECUTE_METADATA,
    FDN_WAMR_EXECUTE_START,
    FDN_WAMR_EXECUTE_STOP,
};

static int32_t fdn_wamr_adopt_output(fdn_wamr_module_bridge *bridge,
                                     uint8_t *output, size_t output_length,
                                     uint64_t *result);
static int fdn_wamr_string_valid(const fdn_string *value, size_t maximum,
                                 int require_text);
static int fdn_wamr_name_valid(const fdn_string *value);
static int32_t fdn_wamr_status(int32_t status);
static int32_t fdn_wamr_module_finalize(fdn_wamr_module_bridge *bridge);
#if defined(_WIN32)
static fdn_wamr_bridge_mutex fdn_wamr_module_slots_lock = {SRWLOCK_INIT};
#else
static fdn_wamr_bridge_mutex fdn_wamr_module_slots_lock = {PTHREAD_MUTEX_INITIALIZER};
#endif
static fdn_wamr_module_slot *fdn_wamr_module_slots;
static uint32_t fdn_wamr_next_module_slot = 1;
static fdn_wamr_engine_slot *fdn_wamr_engine_slots;
static uint32_t fdn_wamr_next_engine_slot = 1;

static int fdn_wamr_bridge_mutex_init(fdn_wamr_bridge_mutex *lock) {
    if (lock == NULL) return 0;
#if defined(_WIN32)
    InitializeSRWLock(&lock->value);
    return 1;
#else
    return pthread_mutex_init(&lock->value, NULL) == 0;
#endif
}

static void fdn_wamr_bridge_mutex_drop(fdn_wamr_bridge_mutex *lock) {
    if (lock == NULL) return;
#if defined(_WIN32)
    (void)lock;
#else
    (void)pthread_mutex_destroy(&lock->value);
#endif
}

static void fdn_wamr_bridge_lock(fdn_wamr_bridge_mutex *lock) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&lock->value);
#else
    (void)pthread_mutex_lock(&lock->value);
#endif
}

static void fdn_wamr_bridge_unlock(fdn_wamr_bridge_mutex *lock) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&lock->value);
#else
    (void)pthread_mutex_unlock(&lock->value);
#endif
}

static void fdn_wamr_slots_lock(void) {
    fdn_wamr_bridge_lock(&fdn_wamr_module_slots_lock);
}

static void fdn_wamr_slots_unlock(void) {
    fdn_wamr_bridge_unlock(&fdn_wamr_module_slots_lock);
}

static uint64_t fdn_wamr_module_reference(const fdn_wamr_module_slot *slot) {
    return ((uint64_t)slot->generation << 32) | slot->index;
}

static uint64_t fdn_wamr_engine_reference(const fdn_wamr_engine_slot *slot) {
    return ((uint64_t)slot->generation << 32) | slot->index;
}

static fdn_wamr_engine_slot *fdn_wamr_engine_slot_find(uint64_t handle) {
    fdn_wamr_engine_slot *slot;
    for (slot = fdn_wamr_engine_slots; slot != NULL; slot = slot->next) {
        if (slot->index == (uint32_t)handle && slot->generation == (uint32_t)(handle >> 32)) return slot;
    }
    return NULL;
}

static fdn_wamr_engine_slot *fdn_wamr_engine_slot_assign(fdn_wamr_engine_bridge *bridge) {
    fdn_wamr_engine_slot *slot;
    for (slot = fdn_wamr_engine_slots; slot != NULL; slot = slot->next) {
        if (slot->bridge == NULL && slot->generation != UINT32_MAX) {
            ++slot->generation;
            slot->bridge = bridge;
            return slot;
        }
    }
    slot = fdn_alloc(sizeof(*slot));
    if (slot == NULL || fdn_wamr_next_engine_slot == 0) {
        fdn_dealloc(slot);
        return NULL;
    }
    (void)memset(slot, 0, sizeof(*slot));
    slot->index = fdn_wamr_next_engine_slot++;
    slot->generation = 1;
    slot->bridge = bridge;
    slot->next = fdn_wamr_engine_slots;
    fdn_wamr_engine_slots = slot;
    return slot;
}

static fdn_wamr_module_slot *fdn_wamr_module_slot_find(uint64_t handle) {
    fdn_wamr_module_slot *slot;
    const uint32_t index = (uint32_t)handle;
    const uint32_t generation = (uint32_t)(handle >> 32);
    for (slot = fdn_wamr_module_slots; slot != NULL; slot = slot->next) {
        if (slot->index == index && slot->generation == generation) return slot;
    }
    return NULL;
}

static fdn_wamr_module_slot *fdn_wamr_module_slot_assign(fdn_wamr_module_bridge *bridge) {
    fdn_wamr_module_slot *slot;
    for (slot = fdn_wamr_module_slots; slot != NULL; slot = slot->next) {
        if (slot->bridge == NULL && slot->generation != UINT32_MAX) {
            ++slot->generation;
            slot->bridge = bridge;
            return slot;
        }
    }
    slot = fdn_alloc(sizeof(*slot));
    if (slot == NULL || fdn_wamr_next_module_slot == 0) {
        fdn_dealloc(slot);
        return NULL;
    }
    (void)memset(slot, 0, sizeof(*slot));
    slot->index = fdn_wamr_next_module_slot++;
    slot->generation = 1;
    slot->bridge = bridge;
    slot->next = fdn_wamr_module_slots;
    fdn_wamr_module_slots = slot;
    return slot;
}

static int fdn_wamr_module_enter(fdn_wamr_module_bridge *bridge) {
    if (bridge == NULL) {
        return 0;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->closing || bridge->module == 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        return 0;
    }
    ++bridge->in_flight;
    fdn_wamr_bridge_unlock(&bridge->lock);
    return 1;
}

static void fdn_wamr_module_retain(fdn_wamr_module_bridge *bridge) {
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->in_flight == 0 || bridge->in_flight == SIZE_MAX) {
        fdn_panic_cstr("WAMR module operation reference invariant failed");
    }
    ++bridge->in_flight;
    fdn_wamr_bridge_unlock(&bridge->lock);
}

static void fdn_wamr_module_leave(fdn_wamr_module_bridge *bridge) {
    int finalize;
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->in_flight == 0) {
        fdn_panic_cstr("WAMR module operation underflow");
    }
    --bridge->in_flight;
    finalize = bridge->closing && bridge->in_flight == 0;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (finalize) {
        (void)fdn_wamr_module_finalize(bridge);
    }
}

static void fdn_wamr_capabilities_drop(fdn_wamr_capability_binding *binding) {
    while (binding != NULL) {
        fdn_wamr_capability_binding *next = binding->next;
        fdn_string_drop(&binding->name);
        fdn_dealloc(binding);
        binding = next;
    }
}

static fdn_string fdn_wamr_string_copy(const fdn_string *value) {
    fdn_string copy = fdn_string_static("", 0);
    char *data;
    if (value == NULL || value->length == 0) {
        return copy;
    }
    data = fdn_alloc(value->length);
    if (data == NULL) {
        return copy;
    }
    (void)memcpy(data, value->data, value->length);
    copy.data = data;
    copy.length = value->length;
    copy.owned = 1;
    return copy;
}

static int32_t fdn_wamr_dispatch_error(const char *message, uint8_t **error,
                                       size_t *error_length) {
    const size_t length = strlen(message);
    uint8_t *copy = malloc(length == 0 ? 1 : length);
    if (copy == NULL) {
        return FDN_WASM_HOST_HANDLER_ERROR;
    }
    if (length != 0) {
        (void)memcpy(copy, message, length);
    }
    *error = copy;
    *error_length = length;
    return FDN_WASM_HOST_HANDLER_ERROR;
}

static int32_t fdn_wamr_bridge_dispatch(
    void *context, const fdn_string *capability, const fdn_string *operation,
    const uint8_t *input, size_t input_length, uint8_t **response,
    size_t *response_length, uint8_t **error, size_t *error_length) {
    fdn_wamr_module_bridge *bridge = context;
    fdn_wamr_capability_binding *binding;
    fdn_wamr_capability_fn handler = NULL;
    uint64_t handler_context = 0;
    uint64_t input_builder = 0;
    uint64_t input_handle = 0;
    uint64_t output_handle = 0;
    const uint8_t *output = NULL;
    size_t output_length = 0;
    size_t index;
    int32_t status;
    if (response == NULL || response_length == NULL || error == NULL ||
        error_length == NULL || bridge == NULL ||
        !fdn_wamr_name_valid(capability) ||
        !fdn_wamr_name_valid(operation) ||
        input_length > bridge->payload_limit ||
        (input_length != 0 && input == NULL)) {
        return FDN_WASM_HOST_INVALID_REQUEST;
    }
    *response = NULL;
    *response_length = 0;
    *error = NULL;
    *error_length = 0;
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->started != 1) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        return FDN_WASM_HOST_DENIED;
    }
    for (binding = bridge->capabilities; binding != NULL; binding = binding->next) {
        if (binding->name.length == capability->length &&
            memcmp(binding->name.data, capability->data, capability->length) == 0) {
            handler = binding->handler;
            handler_context = binding->context;
            break;
        }
    }
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (binding == NULL || handler == NULL) {
        return FDN_WASM_HOST_DENIED;
    }
    if (foundation_runtime_bytes_builder_open(bridge->payload_limit, &input_builder) != 0) {
        return fdn_wamr_dispatch_error("capability input allocation failed", error,
                                       error_length);
    }
    for (index = 0; index < input_length; ++index) {
        if (foundation_runtime_bytes_builder_append_byte(input_builder, input[index]) != 0) {
            foundation_runtime_bytes_builder_close(&input_builder);
            return fdn_wamr_dispatch_error("capability input exceeds limit", error,
                                           error_length);
        }
    }
    if (foundation_runtime_bytes_builder_finish(&input_builder, &input_handle) != 0) {
        foundation_runtime_bytes_builder_close(&input_builder);
        return fdn_wamr_dispatch_error("capability input allocation failed", error,
                                       error_length);
    }
    status = handler(handler_context, operation, input_handle, &output_handle);
    input_handle = 0;
    if (status != FDN_WASM_HOST_OK) {
        foundation_runtime_bytes_close(&output_handle);
        if (status < FDN_WASM_HOST_DENIED || status > FDN_WASM_HOST_PAYLOAD_TOO_LARGE) {
            return fdn_wamr_dispatch_error("capability handler failed", error,
                                           error_length);
        }
        return status;
    }
    if (fdn_bytes_view(output_handle, &output, &output_length) != 0 ||
        output_length > bridge->payload_limit) {
        foundation_runtime_bytes_close(&output_handle);
        return fdn_wamr_dispatch_error("capability output exceeds limit", error,
                                       error_length);
    }
    if (output_length != 0) {
        *response = malloc(output_length);
        if (*response == NULL) {
            foundation_runtime_bytes_close(&output_handle);
            return fdn_wamr_dispatch_error("capability output allocation failed", error,
                                           error_length);
        }
        (void)memcpy(*response, output, output_length);
    }
    *response_length = output_length;
    foundation_runtime_bytes_close(&output_handle);
    return FDN_WASM_HOST_OK;
}

static int fdn_wamr_string_valid(const fdn_string *value, size_t maximum,
                                 int require_text) {
    return value != NULL && value->length <= maximum &&
           (!require_text || value->length != 0) &&
           (value->length == 0 || value->data != NULL) &&
           (value->length == 0 || memchr(value->data, '\0', value->length) == NULL) &&
           fdn_utf8_valid(value->data, value->length);
}

static int fdn_wamr_name_valid(const fdn_string *value) {
    size_t index;
    if (!fdn_wamr_string_valid(value, 128, 1) ||
        value->data[0] < 'a' || value->data[0] > 'z') {
        return 0;
    }
    for (index = 1; index < value->length; ++index) {
        const unsigned char current = (unsigned char)value->data[index];
        if ((current < 'a' || current > 'z') &&
            (current < '0' || current > '9') && current != '.' &&
            current != '_' && current != '-') {
            return 0;
        }
    }
    return 1;
}

static void fdn_wamr_call_lock(fdn_wamr_call_job *job) {
#if defined(_WIN32)
    EnterCriticalSection(&job->lock);
#else
    if (pthread_mutex_lock(&job->lock) != 0) {
        fdn_panic_cstr("WAMR call lock failed");
    }
#endif
}

static void fdn_wamr_call_unlock(fdn_wamr_call_job *job) {
#if defined(_WIN32)
    LeaveCriticalSection(&job->lock);
#else
    if (pthread_mutex_unlock(&job->lock) != 0) {
        fdn_panic_cstr("WAMR call unlock failed");
    }
#endif
}

static void fdn_wamr_call_signal(fdn_wamr_call_job *job) {
#if defined(_WIN32)
    WakeConditionVariable(&job->ready);
#else
    if (pthread_cond_signal(&job->ready) != 0) {
        fdn_panic_cstr("WAMR call signal failed");
    }
#endif
}

static int fdn_wamr_call_wait(fdn_wamr_call_job *job, uint64_t deadline) {
    const uint64_t slice_limit = UINT64_C(50000000);
    while (!job->complete) {
        const uint64_t now = fdn_monotonic_nanoseconds();
        uint64_t remaining;
        uint64_t slice;
        if (now >= deadline) return 0;
        remaining = deadline - now;
        slice = remaining < slice_limit ? remaining : slice_limit;
#if defined(_WIN32)
        const DWORD milliseconds = (DWORD)((slice + UINT64_C(999999)) /
                                            UINT64_C(1000000));
        if (!SleepConditionVariableCS(&job->ready, &job->lock,
                                      milliseconds == 0 ? 1 : milliseconds) &&
            GetLastError() != ERROR_TIMEOUT) {
            return -1;
        }
#else
        struct timespec timeout;
        int status;
        if (clock_gettime(CLOCK_REALTIME, &timeout) != 0) return -1;
        timeout.tv_sec += (time_t)(slice / UINT64_C(1000000000));
        timeout.tv_nsec += (long)(slice % UINT64_C(1000000000));
        if (timeout.tv_nsec >= 1000000000L) {
            ++timeout.tv_sec;
            timeout.tv_nsec -= 1000000000L;
        }
        status = pthread_cond_timedwait(&job->ready, &job->lock, &timeout);
        if (status != 0 && status != ETIMEDOUT) return -1;
#endif
    }
    return 1;
}

static void fdn_wamr_call_job_release(fdn_wamr_call_job *job) {
    int destroy;
    fdn_wamr_call_lock(job);
    if (job->references == 0) {
        fdn_panic_cstr("WAMR call reference underflow");
    }
    --job->references;
    destroy = job->references == 0;
    fdn_wamr_call_unlock(job);
    if (!destroy) return;
#if defined(_WIN32)
    DeleteCriticalSection(&job->lock);
#else
    if (pthread_cond_destroy(&job->ready) != 0 ||
        pthread_mutex_destroy(&job->lock) != 0) {
        fdn_panic_cstr("WAMR call synchronization destroy failed");
    }
#endif
    fdn_string_drop(&job->method);
    fdn_dealloc(job->input);
    fdn_dealloc(job);
}

#if defined(_WIN32)
static DWORD WINAPI fdn_wamr_call_worker(void *context)
#else
static void *fdn_wamr_call_worker(void *context)
#endif
{
    fdn_wamr_call_job *job = context;
    fdn_wamr_module_bridge *bridge = job->bridge;
    uint8_t *output = NULL;
    size_t output_length = 0;
    int abandoned;
    int32_t status;
    switch (job->operation) {
    case FDN_WAMR_EXECUTE_PREPARE:
        status = fdn_wamr_status(bridge->engine->provider->module_prepare(bridge->module));
        break;
    case FDN_WAMR_EXECUTE_METADATA:
        status = fdn_wamr_status(bridge->engine->provider->module_metadata(
            bridge->module, &output, &output_length));
        break;
    case FDN_WAMR_EXECUTE_START:
        status = fdn_wamr_status(bridge->engine->provider->module_start(bridge->module));
        break;
    case FDN_WAMR_EXECUTE_STOP:
        status = fdn_wamr_status(bridge->engine->provider->module_stop(bridge->module));
        break;
    default:
        status = fdn_wamr_status(bridge->engine->provider->module_call(
            bridge->module, &job->method, job->input, job->input_length,
            &output, &output_length
        ));
        break;
    }
    fdn_wamr_call_lock(job);
    abandoned = job->abandoned;
    job->status = status;
    job->complete = 1;
    if (!abandoned) {
        job->output = output;
        job->output_length = output_length;
        output = NULL;
        output_length = 0;
    }
    fdn_wamr_call_signal(job);
    fdn_wamr_call_unlock(job);
    if (output != NULL || output_length != 0) {
        bridge->engine->provider->free_output(
            bridge->module, output, output_length
        );
    }
    if (abandoned) {
        fdn_wamr_module_leave(bridge);
    }
    fdn_wamr_call_job_release(job);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int fdn_wamr_call_start(fdn_wamr_call_job *job) {
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, fdn_wamr_call_worker, job, 0, NULL);
    if (thread == NULL) return 0;
    (void)CloseHandle(thread);
    return 1;
#else
    pthread_attr_t attributes;
    pthread_t thread;
    int status;
    if (pthread_attr_init(&attributes) != 0) return 0;
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) != 0) {
        (void)pthread_attr_destroy(&attributes);
        return 0;
    }
    status = pthread_create(&thread, &attributes, fdn_wamr_call_worker, job);
    (void)pthread_attr_destroy(&attributes);
    return status == 0;
#endif
}

static fdn_wamr_call_job *fdn_wamr_call_job_open(
    fdn_wamr_module_bridge *bridge, const fdn_string *method,
    const uint8_t *input, size_t input_length, int operation) {
    fdn_wamr_call_job *job = fdn_alloc(sizeof(*job));
    if (job == NULL) return NULL;
    (void)memset(job, 0, sizeof(*job));
    job->bridge = bridge;
    job->operation = operation;
    job->method = method == NULL ? fdn_string_static("", 0) : fdn_wamr_string_copy(method);
    if (job->method.data == NULL) {
        fdn_dealloc(job);
        return NULL;
    }
    if (input_length != 0) {
        job->input = fdn_alloc(input_length);
        if (job->input == NULL) {
            fdn_string_drop(&job->method);
            fdn_dealloc(job);
            return NULL;
        }
        (void)memcpy(job->input, input, input_length);
    }
    job->input_length = input_length;
    job->references = 2;
#if defined(_WIN32)
    InitializeCriticalSection(&job->lock);
    InitializeConditionVariable(&job->ready);
#else
    if (pthread_mutex_init(&job->lock, NULL) != 0) {
        fdn_string_drop(&job->method);
        fdn_dealloc(job->input);
        fdn_dealloc(job);
        return NULL;
    }
    if (pthread_cond_init(&job->ready, NULL) != 0) {
        (void)pthread_mutex_destroy(&job->lock);
        fdn_string_drop(&job->method);
        fdn_dealloc(job->input);
        fdn_dealloc(job);
        return NULL;
    }
#endif
    return job;
}

static void *fdn_wamr_provider_symbol(fdn_plugin_module library) {
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(library, FDN_WAMR_PROVIDER_QUERY);
    void *result = NULL;
    _Static_assert(sizeof(symbol) == sizeof(result),
                   "plugin function and data pointers must have equal size");
    (void)memcpy(&result, &symbol, sizeof(result));
    return result;
#else
    return dlsym(library, FDN_WAMR_PROVIDER_QUERY);
#endif
}

static int32_t fdn_wamr_status(int32_t status) {
    switch (status) {
    case FDN_WAMR_OK:
    case FDN_WAMR_DENIED:
    case FDN_WAMR_INVALID_REQUEST:
    case FDN_WAMR_HANDLER_ERROR:
    case FDN_WAMR_PAYLOAD_TOO_LARGE:
    case FDN_WAMR_CLOSED:
    case FDN_WAMR_READ_ONLY_UNSUPPORTED:
        return status;
    default:
        return FDN_WAMR_HANDLER_ERROR;
    }
}

static void fdn_wamr_provider_engine_drop(const fdn_wamr_provider_v2 *provider,
                                          uint64_t *engine) {
    const int32_t status = fdn_wamr_status(provider->engine_close(engine));
    if (status != FDN_WAMR_OK || *engine != 0)
        fdn_panic_cstr("WAMR provider engine_close contract violation");
}

static void fdn_wamr_provider_module_drop(const fdn_wamr_provider_v2 *provider,
                                          uint64_t *module) {
    const int32_t status = fdn_wamr_status(provider->module_close(module));
    if (status != FDN_WAMR_OK || *module != 0)
        fdn_panic_cstr("WAMR provider module_close contract violation");
}

static void fdn_wamr_engine_finalize(fdn_wamr_engine_bridge *bridge) {
    fdn_wamr_provider_engine_drop(bridge->provider, &bridge->engine);
    fdn_plugin_module_close(bridge->library);
    fdn_wamr_bridge_mutex_drop(&bridge->lock);
    fdn_dealloc(bridge);
}

static fdn_wamr_engine_bridge *fdn_wamr_engine_acquire(uint64_t handle) {
    fdn_wamr_engine_slot *slot;
    fdn_wamr_engine_bridge *bridge = NULL;
    fdn_wamr_slots_lock();
    slot = fdn_wamr_engine_slot_find(handle);
    if (slot != NULL && slot->bridge != NULL) {
        fdn_wamr_bridge_lock(&slot->bridge->lock);
        if (!slot->bridge->close_requested && slot->bridge->engine != 0) {
            bridge = slot->bridge;
            ++bridge->in_flight;
        }
        fdn_wamr_bridge_unlock(&slot->bridge->lock);
    }
    fdn_wamr_slots_unlock();
    return bridge;
}

static void fdn_wamr_engine_release(fdn_wamr_engine_bridge *bridge) {
    int finalize;
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->in_flight == 0) fdn_panic_cstr("WAMR engine operation underflow");
    --bridge->in_flight;
    finalize = bridge->close_requested && bridge->module_count == 0 &&
               bridge->in_flight == 0;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (finalize) fdn_wamr_engine_finalize(bridge);
}

static fdn_wamr_module_bridge *fdn_wamr_module_value(uint64_t handle) {
    fdn_wamr_module_bridge *bridge = NULL;
    fdn_wamr_module_slot *slot;
    fdn_wamr_slots_lock();
    slot = fdn_wamr_module_slot_find(handle);
    if (slot != NULL && slot->bridge != NULL && fdn_wamr_module_enter(slot->bridge)) {
        bridge = slot->bridge;
    }
    fdn_wamr_slots_unlock();
    return bridge;
}

static int fdn_wamr_provider_valid(const fdn_wamr_provider_v2 *provider) {
    return provider != NULL &&
           provider->abi_major == FDN_WAMR_PROVIDER_ABI_MAJOR &&
           provider->abi_minor == FDN_WAMR_PROVIDER_ABI_MINOR &&
           provider->struct_size >= FDN_WAMR_PROVIDER_V2_SIZE &&
           provider->engine_open != NULL && provider->engine_close != NULL &&
           provider->module_open != NULL && provider->module_allow_read != NULL &&
           provider->module_allow_write != NULL &&
           provider->module_set_environment != NULL &&
           provider->module_add_argument != NULL &&
           provider->module_set_host_dispatch != NULL &&
           provider->module_prepare != NULL && provider->module_metadata != NULL &&
           provider->module_start != NULL &&
           provider->module_stop != NULL && provider->module_cancel != NULL &&
           provider->module_call != NULL && provider->free_output != NULL &&
           provider->module_close != NULL;
}

int32_t foundation_runtime_wamr_engine_open(const fdn_string *adapter_path,
                                            uint64_t *handle) {
    fdn_plugin_module library;
    fdn_wamr_provider_query_fn query;
    const fdn_wamr_provider_v2 *provider;
    fdn_wamr_engine_bridge *bridge;
    uint64_t engine = 0;
    void *symbol;
    int32_t status;

    if (handle == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *handle = 0;
    library = fdn_plugin_module_open(adapter_path);
    if (library == NULL) {
        return FDN_WAMR_HANDLER_ERROR;
    }
    symbol = fdn_wamr_provider_symbol(library);
    if (symbol == NULL) {
        fdn_plugin_module_close(library);
        return FDN_WAMR_HANDLER_ERROR;
    }
    _Static_assert(sizeof(query) == sizeof(symbol),
                   "plugin function and data pointers must have equal size");
    (void)memcpy(&query, &symbol, sizeof(query));
    provider = query();
    if (!fdn_wamr_provider_valid(provider)) {
        fdn_plugin_module_close(library);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_status(provider->engine_open(&engine));
    if (status != FDN_WAMR_OK || engine == 0) {
        if (engine != 0) fdn_wamr_provider_engine_drop(provider, &engine);
        fdn_plugin_module_close(library);
        return status == FDN_WAMR_OK ? FDN_WAMR_HANDLER_ERROR : status;
    }
    bridge = fdn_alloc(sizeof(*bridge));
    if (bridge == NULL) {
        fdn_wamr_provider_engine_drop(provider, &engine);
        fdn_plugin_module_close(library);
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)memset(bridge, 0, sizeof(*bridge));
    if (!fdn_wamr_bridge_mutex_init(&bridge->lock)) {
        fdn_wamr_provider_engine_drop(provider, &engine);
        fdn_plugin_module_close(library);
        fdn_dealloc(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    bridge->library = library;
    bridge->provider = provider;
    bridge->engine = engine;
    fdn_wamr_slots_lock();
    bridge->slot = fdn_wamr_engine_slot_assign(bridge);
    fdn_wamr_slots_unlock();
    if (bridge->slot == NULL) {
        fdn_wamr_provider_engine_drop(provider, &engine);
        fdn_plugin_module_close(library);
        fdn_wamr_bridge_mutex_drop(&bridge->lock);
        fdn_dealloc(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    *handle = fdn_wamr_engine_reference(bridge->slot);
    return FDN_WAMR_OK;
}

int32_t foundation_runtime_wamr_engine_close(uint64_t *handle) {
    fdn_wamr_engine_slot *slot;
    fdn_wamr_engine_bridge *bridge;
    int finalize;

    if (handle == NULL || *handle == 0) {
        return FDN_WAMR_OK;
    }
    fdn_wamr_slots_lock();
    slot = fdn_wamr_engine_slot_find(*handle);
    bridge = slot == NULL ? NULL : slot->bridge;
    if (bridge == NULL) {
        fdn_wamr_slots_unlock();
        *handle = 0;
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    bridge->close_requested = 1;
    slot->bridge = NULL;
    finalize = bridge->module_count == 0 && bridge->in_flight == 0;
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_slots_unlock();
    *handle = 0;
    if (finalize) fdn_wamr_engine_finalize(bridge);
    return FDN_WAMR_OK;
}

int32_t foundation_runtime_wamr_module_open(uint64_t engine_handle,
                                             const fdn_string *path,
                                             uint64_t payload_limit,
                                             uint64_t *handle) {
    fdn_wamr_engine_bridge *engine;
    fdn_wamr_module_bridge *bridge;
    uint64_t module = 0;
    int closed;
    int32_t status;

    if (handle == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *handle = 0;
    if (!fdn_wamr_string_valid(path, 4096, 1) || payload_limit == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    engine = fdn_wamr_engine_acquire(engine_handle);
    if (engine == NULL) return FDN_WAMR_CLOSED;
    status = fdn_wamr_status(engine->provider->module_open(
        engine->engine, path, payload_limit, &module));
    fdn_wamr_bridge_lock(&engine->lock);
    closed = engine->close_requested || engine->engine == 0;
    fdn_wamr_bridge_unlock(&engine->lock);
    if (status != FDN_WAMR_OK || module == 0) {
        if (module != 0)
            fdn_wamr_provider_module_drop(engine->provider, &module);
        fdn_wamr_engine_release(engine);
        if (closed) return FDN_WAMR_CLOSED;
        return status == FDN_WAMR_OK ? FDN_WAMR_HANDLER_ERROR : status;
    }
    bridge = fdn_alloc(sizeof(*bridge));
    if (bridge == NULL) {
        fdn_wamr_provider_module_drop(engine->provider, &module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)memset(bridge, 0, sizeof(*bridge));
    if (!fdn_wamr_bridge_mutex_init(&bridge->lock)) {
        fdn_wamr_provider_module_drop(engine->provider, &module);
        fdn_dealloc(bridge);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    bridge->engine = engine;
    bridge->module = module;
    bridge->payload_limit = payload_limit;
    fdn_wamr_slots_lock();
    fdn_wamr_bridge_lock(&engine->lock);
    if (engine->close_requested || engine->engine == 0) {
        fdn_wamr_bridge_unlock(&engine->lock);
        fdn_wamr_slots_unlock();
        fdn_wamr_provider_module_drop(engine->provider, &module);
        fdn_wamr_bridge_mutex_drop(&bridge->lock);
        fdn_dealloc(bridge);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_CLOSED;
    }
    bridge->slot = fdn_wamr_module_slot_assign(bridge);
    if (bridge->slot == NULL) {
        fdn_wamr_bridge_unlock(&engine->lock);
        fdn_wamr_slots_unlock();
        fdn_wamr_provider_module_drop(engine->provider, &module);
        fdn_wamr_bridge_mutex_drop(&bridge->lock);
        fdn_dealloc(bridge);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    ++engine->module_count;
    *handle = fdn_wamr_module_reference(bridge->slot);
    fdn_wamr_bridge_unlock(&engine->lock);
    fdn_wamr_slots_unlock();
    fdn_wamr_engine_release(engine);
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_configure(uint64_t handle, const fdn_string *first,
                                   const fdn_string *second, int kind) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    int closed;
    int32_t status;
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    if (first == NULL || (kind == 2 && second == NULL)) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (((kind == 0 || kind == 1) &&
         !fdn_wamr_string_valid(first, 4096, 1)) ||
        (kind == 2 &&
         (!fdn_wamr_string_valid(first, 128, 1) ||
          memchr(first->data, '=', first->length) != NULL ||
          !fdn_wamr_string_valid(second, 4096, 0))) ||
        (kind == 3 && !fdn_wamr_string_valid(first, 4096, 0)) ||
        kind < 0 || kind > 3) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->configuring || bridge->prepared != 0 || bridge->started != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_CLOSED;
    }
    bridge->configuring = 1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (kind == 0) {
        status = fdn_wamr_status(
            bridge->engine->provider->module_allow_read(bridge->module, first));
    } else if (kind == 1) {
        status = fdn_wamr_status(
            bridge->engine->provider->module_allow_write(bridge->module, first));
    } else if (kind == 2) {
        status = fdn_wamr_status(bridge->engine->provider->module_set_environment(
            bridge->module, first, second));
    } else {
        status = fdn_wamr_status(
            bridge->engine->provider->module_add_argument(bridge->module, first));
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    bridge->configuring = 0;
    closed = bridge->closing;
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_module_leave(bridge);
    return closed ? FDN_WAMR_CLOSED : status;
}

int32_t foundation_runtime_wamr_module_allow_read(uint64_t handle,
                                                   const fdn_string *path) {
    return fdn_wamr_configure(handle, path, NULL, 0);
}

int32_t foundation_runtime_wamr_module_allow_write(uint64_t handle,
                                                    const fdn_string *path) {
    return fdn_wamr_configure(handle, path, NULL, 1);
}

int32_t foundation_runtime_wamr_module_set_environment(uint64_t handle,
                                                        const fdn_string *key,
                                                        const fdn_string *value) {
    return fdn_wamr_configure(handle, key, value, 2);
}

int32_t foundation_runtime_wamr_module_add_argument(uint64_t handle,
                                                     const fdn_string *value) {
    return fdn_wamr_configure(handle, value, NULL, 3);
}

int32_t foundation_runtime_wamr_module_declare_capability(
    uint64_t handle, const fdn_string *name) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    fdn_wamr_capability_binding *binding;
    fdn_wamr_capability_binding *current;
    if (!fdn_wamr_name_valid(name)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->prepared != 0 || bridge->started != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    for (current = bridge->capabilities; current != NULL; current = current->next) {
        if (current->name.length == name->length &&
            memcmp(current->name.data, name->data, name->length) == 0) {
            fdn_wamr_bridge_unlock(&bridge->lock);
            fdn_wamr_module_leave(bridge);
            return FDN_WAMR_INVALID_REQUEST;
        }
    }
    binding = fdn_alloc(sizeof(*binding));
    if (binding == NULL) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)memset(binding, 0, sizeof(*binding));
    binding->name = fdn_wamr_string_copy(name);
    if (binding->name.data == NULL) {
        fdn_dealloc(binding);
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    binding->next = bridge->capabilities;
    bridge->capabilities = binding;
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_module_leave(bridge);
    return FDN_WAMR_OK;
}

int32_t foundation_runtime_wamr_module_add_capability(
    uint64_t handle, const fdn_string *name, uint64_t context,
    fdn_wamr_capability_fn handler) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    fdn_wamr_capability_binding *current;
    if (handler == NULL || !fdn_wamr_name_valid(name)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->started != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    for (current = bridge->capabilities; current != NULL; current = current->next) {
        if (current->name.length == name->length &&
            memcmp(current->name.data, name->data, name->length) == 0) {
            if (current->handler != NULL) {
                fdn_wamr_bridge_unlock(&bridge->lock);
                fdn_wamr_module_leave(bridge);
                return FDN_WAMR_INVALID_REQUEST;
            }
            current->context = context;
            current->handler = handler;
            fdn_wamr_bridge_unlock(&bridge->lock);
            fdn_wamr_module_leave(bridge);
            return FDN_WAMR_OK;
        }
    }
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_module_leave(bridge);
    return FDN_WAMR_DENIED;
}

int32_t foundation_runtime_wamr_module_start(uint64_t handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    int started_after;
    int32_t status;
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->prepared != 1 || bridge->started != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    bridge->started = -1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    status = fdn_wamr_status(bridge->engine->provider->module_start(bridge->module));
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->started == -1) {
        bridge->started = status == FDN_WAMR_OK ? 1 : 0;
    }
    started_after = bridge->started;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (status == FDN_WAMR_OK && started_after != 1) {
        status = FDN_WAMR_CLOSED;
    }
    fdn_wamr_module_leave(bridge);
    return status;
}

int32_t foundation_runtime_wamr_module_prepare(uint64_t handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    int32_t status;
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->configuring || bridge->prepared != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    bridge->prepared = -1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    status = fdn_wamr_status(bridge->engine->provider->module_set_host_dispatch(
        bridge->module, bridge, fdn_wamr_bridge_dispatch));
    if (status == FDN_WAMR_OK) {
        status = fdn_wamr_status(bridge->engine->provider->module_prepare(bridge->module));
    }
    if (status == FDN_WAMR_OK) {
        fdn_wamr_bridge_lock(&bridge->lock);
        bridge->prepared = 1;
        fdn_wamr_bridge_unlock(&bridge->lock);
    } else {
        fdn_wamr_bridge_lock(&bridge->lock);
        bridge->prepared = 0;
        fdn_wamr_bridge_unlock(&bridge->lock);
    }
    fdn_wamr_module_leave(bridge);
    return status;
}

int32_t foundation_runtime_wamr_module_metadata(uint64_t handle,
                                                uint64_t *output_handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    uint8_t *metadata = NULL;
    size_t metadata_length = 0;
    int32_t status;
    if (output_handle == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    *output_handle = 0;
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->prepared != 1 || bridge->started != 0) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    fdn_wamr_bridge_unlock(&bridge->lock);
    status = fdn_wamr_status(bridge->engine->provider->module_metadata(
        bridge->module, &metadata, &metadata_length));
    if (status == FDN_WAMR_OK) {
        status = fdn_wamr_adopt_output(bridge, metadata, metadata_length, output_handle);
    }
    if (metadata != NULL || metadata_length != 0) {
        bridge->engine->provider->free_output(bridge->module, metadata, metadata_length);
    }
    fdn_wamr_module_leave(bridge);
    return status;
}

int32_t foundation_runtime_wamr_module_stop(uint64_t handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    int started_after;
    int32_t status;
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->started != 1) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_OK;
    }
    bridge->started = -1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    status = fdn_wamr_status(bridge->engine->provider->module_stop(bridge->module));
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->started == -1) {
        bridge->started = status == FDN_WAMR_OK || status == FDN_WAMR_CLOSED
                              ? 0
                              : 1;
    }
    started_after = bridge->started;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (started_after == 0 && status != FDN_WAMR_OK &&
        status != FDN_WAMR_CLOSED) {
        status = FDN_WAMR_CLOSED;
    }
    fdn_wamr_module_leave(bridge);
    return status;
}

int32_t foundation_runtime_wamr_module_cancel(uint64_t handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    int32_t status;
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    status = fdn_wamr_status(bridge->engine->provider->module_cancel(bridge->module));
    if (status == FDN_WAMR_OK || status == FDN_WAMR_CLOSED) {
        fdn_wamr_bridge_lock(&bridge->lock);
        bridge->started = 0;
        fdn_wamr_bridge_unlock(&bridge->lock);
    }
    fdn_wamr_module_leave(bridge);
    return status;
}

static int32_t fdn_wamr_adopt_output(fdn_wamr_module_bridge *bridge,
                                     uint8_t *output, size_t output_length,
                                     uint64_t *result) {
    uint64_t builder = 0;
    size_t index;
    int32_t status;
    if (result == NULL || output_length > bridge->payload_limit ||
        (output_length != 0 && output == NULL)) {
        return output_length > bridge->payload_limit ? FDN_WAMR_PAYLOAD_TOO_LARGE
                                                      : FDN_WAMR_INVALID_REQUEST;
    }
    status = foundation_runtime_bytes_builder_open(bridge->payload_limit, &builder);
    if (status != 0) {
        return FDN_WAMR_HANDLER_ERROR;
    }
    for (index = 0; index < output_length; ++index) {
        status = foundation_runtime_bytes_builder_append_byte(builder, output[index]);
        if (status != 0) {
            foundation_runtime_bytes_builder_close(&builder);
            return status == 2 ? FDN_WAMR_PAYLOAD_TOO_LARGE : FDN_WAMR_HANDLER_ERROR;
        }
    }
    status = foundation_runtime_bytes_builder_finish(&builder, result);
    if (status != 0) {
        foundation_runtime_bytes_builder_close(&builder);
        return FDN_WAMR_HANDLER_ERROR;
    }
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_execute_timed(
    uint64_t handle, int operation, uint64_t timeout_nanoseconds,
    uint64_t *output_handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    fdn_wamr_call_job *job;
    uint8_t *output = NULL;
    size_t output_length = 0;
    uint64_t now;
    uint64_t deadline;
    int wait_status;
    int32_t status;
    if (timeout_nanoseconds == 0 ||
        (operation == FDN_WAMR_EXECUTE_METADATA && output_handle == NULL)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (bridge == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_bridge_lock(&bridge->lock);
    if ((operation == FDN_WAMR_EXECUTE_PREPARE &&
         (bridge->configuring || bridge->prepared != 0 || bridge->started != 0)) ||
        (operation == FDN_WAMR_EXECUTE_METADATA && bridge->prepared != 1) ||
        (operation == FDN_WAMR_EXECUTE_START &&
         (bridge->prepared != 1 || bridge->started != 0))) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_CLOSED;
    }
    if (operation == FDN_WAMR_EXECUTE_STOP && bridge->started != 1) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_OK;
    }
    if (operation == FDN_WAMR_EXECUTE_PREPARE) bridge->prepared = -1;
    if (operation == FDN_WAMR_EXECUTE_START || operation == FDN_WAMR_EXECUTE_STOP)
        bridge->started = -1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (output_handle != NULL) *output_handle = 0;
    if (operation == FDN_WAMR_EXECUTE_PREPARE) {
        status = fdn_wamr_status(bridge->engine->provider->module_set_host_dispatch(
            bridge->module, bridge, fdn_wamr_bridge_dispatch
        ));
        if (status != FDN_WAMR_OK) {
            fdn_wamr_bridge_lock(&bridge->lock);
            bridge->prepared = 0;
            fdn_wamr_bridge_unlock(&bridge->lock);
            fdn_wamr_module_leave(bridge);
            return status;
        }
    }
    job = fdn_wamr_call_job_open(bridge, NULL, NULL, 0, operation);
    if (job == NULL) {
        fdn_wamr_bridge_lock(&bridge->lock);
        if (operation == FDN_WAMR_EXECUTE_PREPARE) bridge->prepared = 0;
        if (operation == FDN_WAMR_EXECUTE_START) bridge->started = 0;
        if (operation == FDN_WAMR_EXECUTE_STOP) bridge->started = 1;
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_call_start(job)) {
        fdn_wamr_call_job_release(job);
        fdn_wamr_call_job_release(job);
        fdn_wamr_bridge_lock(&bridge->lock);
        if (operation == FDN_WAMR_EXECUTE_PREPARE) bridge->prepared = 0;
        if (operation == FDN_WAMR_EXECUTE_START) bridge->started = 0;
        if (operation == FDN_WAMR_EXECUTE_STOP) bridge->started = 1;
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    now = fdn_monotonic_nanoseconds();
    deadline = timeout_nanoseconds > UINT64_MAX - now ? UINT64_MAX : now + timeout_nanoseconds;
    fdn_wamr_call_lock(job);
    wait_status = fdn_wamr_call_wait(job, deadline);
    if (wait_status == 1) {
        status = job->status;
        output = job->output;
        output_length = job->output_length;
        job->output = NULL;
        job->output_length = 0;
    } else {
        fdn_wamr_module_retain(bridge);
        job->abandoned = 1;
        status = wait_status == 0 ? FDN_WAMR_CLOSED : FDN_WAMR_HANDLER_ERROR;
    }
    fdn_wamr_call_unlock(job);
    if (wait_status != 1) {
        fdn_wamr_timeout_test_barrier();
        (void)bridge->engine->provider->module_cancel(bridge->module);
        fdn_wamr_bridge_lock(&bridge->lock);
        bridge->prepared = operation == FDN_WAMR_EXECUTE_PREPARE ? 0 : bridge->prepared;
        if (operation == FDN_WAMR_EXECUTE_START || operation == FDN_WAMR_EXECUTE_STOP)
            bridge->started = 0;
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        fdn_wamr_call_job_release(job);
        return status;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (operation == FDN_WAMR_EXECUTE_PREPARE) bridge->prepared = status == FDN_WAMR_OK ? 1 : 0;
    if (operation == FDN_WAMR_EXECUTE_START) bridge->started = status == FDN_WAMR_OK ? 1 : 0;
    if (operation == FDN_WAMR_EXECUTE_STOP) bridge->started = 0;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (status == FDN_WAMR_OK && operation == FDN_WAMR_EXECUTE_METADATA)
        status = fdn_wamr_adopt_output(bridge, output, output_length, output_handle);
    if (output != NULL || output_length != 0)
        bridge->engine->provider->free_output(bridge->module, output, output_length);
    fdn_wamr_module_leave(bridge);
    fdn_wamr_call_job_release(job);
    return status;
}

int32_t foundation_runtime_wamr_module_prepare_timed(uint64_t handle,
                                                      uint64_t timeout_nanoseconds) {
    return fdn_wamr_module_execute_timed(
        handle, FDN_WAMR_EXECUTE_PREPARE, timeout_nanoseconds, NULL
    );
}

int32_t foundation_runtime_wamr_module_metadata_timed(
    uint64_t handle, uint64_t timeout_nanoseconds, uint64_t *output_handle) {
    return fdn_wamr_module_execute_timed(
        handle, FDN_WAMR_EXECUTE_METADATA, timeout_nanoseconds, output_handle
    );
}

int32_t foundation_runtime_wamr_module_start_timed(uint64_t handle,
                                                    uint64_t timeout_nanoseconds) {
    return fdn_wamr_module_execute_timed(
        handle, FDN_WAMR_EXECUTE_START, timeout_nanoseconds, NULL
    );
}

int32_t foundation_runtime_wamr_module_stop_timed(uint64_t handle,
                                                   uint64_t timeout_nanoseconds) {
    return fdn_wamr_module_execute_timed(
        handle, FDN_WAMR_EXECUTE_STOP, timeout_nanoseconds, NULL
    );
}

int32_t foundation_runtime_wamr_module_call(uint64_t handle,
                                             const fdn_string *method,
                                             uint64_t input_handle,
                                             uint64_t timeout_nanoseconds,
                                             uint64_t *output_handle) {
    fdn_wamr_module_bridge *bridge = fdn_wamr_module_value(handle);
    const uint8_t *input = NULL;
    size_t input_length = 0;
    fdn_wamr_call_job *job;
    uint8_t *output = NULL;
    size_t output_length = 0;
    uint64_t now;
    uint64_t deadline;
    int wait_status;
    int32_t status;
    if (output_handle == NULL || timeout_nanoseconds == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (bridge == NULL) {
        return FDN_WAMR_CLOSED;
    }
    *output_handle = 0;
    fdn_wamr_bridge_lock(&bridge->lock);
    const int callable = bridge->started == 1;
    fdn_wamr_bridge_unlock(&bridge->lock);
    if (!callable || !fdn_wamr_name_valid(method)) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (fdn_bytes_view(input_handle, &input, &input_length) != 0) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (input_length > bridge->payload_limit) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_PAYLOAD_TOO_LARGE;
    }
    job = fdn_wamr_call_job_open(
        bridge, method, input, input_length, FDN_WAMR_EXECUTE_CALL
    );
    if (job == NULL) {
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_call_start(job)) {
        fdn_wamr_call_job_release(job);
        fdn_wamr_call_job_release(job);
        fdn_wamr_module_leave(bridge);
        return FDN_WAMR_HANDLER_ERROR;
    }
    now = fdn_monotonic_nanoseconds();
    deadline = timeout_nanoseconds > UINT64_MAX - now
                   ? UINT64_MAX
                   : now + timeout_nanoseconds;
    fdn_wamr_call_lock(job);
    wait_status = fdn_wamr_call_wait(job, deadline);
    if (wait_status == 1) {
        status = job->status;
        output = job->output;
        output_length = job->output_length;
        job->output = NULL;
        job->output_length = 0;
    } else {
        fdn_wamr_module_retain(bridge);
        job->abandoned = 1;
        status = wait_status == 0 ? FDN_WAMR_CLOSED : FDN_WAMR_HANDLER_ERROR;
    }
    fdn_wamr_call_unlock(job);
    if (wait_status != 1) {
        fdn_wamr_timeout_test_barrier();
        (void)bridge->engine->provider->module_cancel(bridge->module);
        fdn_wamr_bridge_lock(&bridge->lock);
        bridge->started = 0;
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_module_leave(bridge);
        fdn_wamr_call_job_release(job);
        return status;
    }
    if (status != FDN_WAMR_OK) {
        if (output != NULL || output_length != 0) {
            bridge->engine->provider->free_output(bridge->module, output, output_length);
        }
        fdn_wamr_module_leave(bridge);
        fdn_wamr_call_job_release(job);
        return status;
    }
    status = fdn_wamr_adopt_output(bridge, output, output_length, output_handle);
    bridge->engine->provider->free_output(bridge->module, output, output_length);
    fdn_wamr_module_leave(bridge);
    fdn_wamr_call_job_release(job);
    return status;
}

static int32_t fdn_wamr_module_finalize(fdn_wamr_module_bridge *bridge) {
    int close_engine = 0;
    if (!bridge->module_released) {
        fdn_wamr_provider_module_drop(bridge->engine->provider, &bridge->module);
        fdn_wamr_capabilities_drop(bridge->capabilities);
        bridge->capabilities = NULL;
        fdn_wamr_bridge_lock(&bridge->engine->lock);
        if (bridge->engine->module_count == 0) {
            fdn_wamr_bridge_unlock(&bridge->engine->lock);
            return FDN_WAMR_HANDLER_ERROR;
        }
        --bridge->engine->module_count;
        bridge->module_released = 1;
        close_engine = bridge->engine->module_count == 0 &&
                       bridge->engine->in_flight == 0 &&
                       bridge->engine->close_requested;
        fdn_wamr_bridge_unlock(&bridge->engine->lock);
    } else {
        fdn_wamr_bridge_lock(&bridge->engine->lock);
        close_engine = bridge->engine->module_count == 0 &&
                       bridge->engine->in_flight == 0 &&
                       bridge->engine->close_requested;
        fdn_wamr_bridge_unlock(&bridge->engine->lock);
    }
    if (close_engine) {
        fdn_wamr_engine_finalize(bridge->engine);
    }
    fdn_wamr_bridge_mutex_drop(&bridge->lock);
    fdn_dealloc(bridge);
    return FDN_WAMR_OK;
}

int32_t foundation_runtime_wamr_module_close(uint64_t *handle) {
    fdn_wamr_module_bridge *bridge;
    fdn_wamr_module_slot *slot;
    size_t in_flight;
    int32_t status;
    if (handle == NULL || *handle == 0) {
        return FDN_WAMR_OK;
    }
    fdn_wamr_slots_lock();
    slot = fdn_wamr_module_slot_find(*handle);
    bridge = slot == NULL ? NULL : slot->bridge;
    if (bridge == NULL) {
        fdn_wamr_slots_unlock();
        *handle = 0;
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    if (bridge->closing) {
        fdn_wamr_bridge_unlock(&bridge->lock);
        fdn_wamr_slots_unlock();
        *handle = 0;
        return FDN_WAMR_CLOSED;
    }
    bridge->closing = 1;
    bridge->started = 0;
    slot->bridge = NULL;
    in_flight = bridge->in_flight;
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_slots_unlock();
    if (in_flight != 0) {
        (void)bridge->engine->provider->module_cancel(bridge->module);
        *handle = 0;
        return FDN_WAMR_OK;
    }
    status = fdn_wamr_module_finalize(bridge);
    if (status == FDN_WAMR_OK) {
        *handle = 0;
        return FDN_WAMR_OK;
    }
    fdn_wamr_bridge_lock(&bridge->lock);
    bridge->closing = 0;
    fdn_wamr_bridge_unlock(&bridge->lock);
    fdn_wamr_slots_lock();
    if (bridge->slot != NULL) bridge->slot->bridge = bridge;
    fdn_wamr_slots_unlock();
    return status;
}
