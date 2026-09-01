#include "foundation/wamr_provider.h"
#include "foundation/wasm_plugin.h"

#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct fdn_wamr_mutex {
#if defined(_WIN32)
    SRWLOCK value;
#else
    pthread_mutex_t value;
#endif
} fdn_wamr_mutex;

typedef struct fdn_wamr_condition {
#if defined(_WIN32)
    CONDITION_VARIABLE value;
#else
    pthread_cond_t value;
#endif
} fdn_wamr_condition;

static int fdn_wamr_mutex_init(fdn_wamr_mutex *mutex) {
    if (mutex == NULL) return 0;
#if defined(_WIN32)
    InitializeSRWLock(&mutex->value);
    return 1;
#else
    return pthread_mutex_init(&mutex->value, NULL) == 0;
#endif
}

static void fdn_wamr_mutex_drop(fdn_wamr_mutex *mutex) {
    if (mutex == NULL) return;
#if defined(_WIN32)
    (void)mutex;
#else
    (void)pthread_mutex_destroy(&mutex->value);
#endif
}

static void fdn_wamr_mutex_lock(fdn_wamr_mutex *mutex) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&mutex->value);
#else
    (void)pthread_mutex_lock(&mutex->value);
#endif
}

static void fdn_wamr_mutex_unlock(fdn_wamr_mutex *mutex) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&mutex->value);
#else
    (void)pthread_mutex_unlock(&mutex->value);
#endif
}

static int fdn_wamr_condition_init(fdn_wamr_condition *condition) {
    if (condition == NULL) return 0;
#if defined(_WIN32)
    InitializeConditionVariable(&condition->value);
    return 1;
#else
    return pthread_cond_init(&condition->value, NULL) == 0;
#endif
}

static void fdn_wamr_condition_drop(fdn_wamr_condition *condition) {
#if !defined(_WIN32)
    if (condition != NULL) (void)pthread_cond_destroy(&condition->value);
#else
    (void)condition;
#endif
}

static void fdn_wamr_condition_wait(fdn_wamr_condition *condition,
                                    fdn_wamr_mutex *mutex) {
#if defined(_WIN32)
    if (!SleepConditionVariableSRW(&condition->value, &mutex->value, INFINITE, 0)) abort();
#else
    if (pthread_cond_wait(&condition->value, &mutex->value) != 0) abort();
#endif
}

static void fdn_wamr_condition_broadcast(fdn_wamr_condition *condition) {
#if defined(_WIN32)
    WakeAllConditionVariable(&condition->value);
#else
    (void)pthread_cond_broadcast(&condition->value);
#endif
}

#include <wasm_export.h>
#include <version.h>

#if WAMR_VERSION_MAJOR != 2 || WAMR_VERSION_MINOR != 4 || WAMR_VERSION_PATCH != 5
#error "Foundation requires WAMR 2.4.5"
#endif

#define FDN_WAMR_MAX_MODULE_BYTES UINT64_C(67108864)
#define FDN_WAMR_MAX_METADATA_BYTES UINT32_C(65536)
#define FDN_WAMR_MAX_WASI_ENTRIES UINT32_C(1024)

#if defined(_WIN32)
#define FDN_WAMR_THREAD_LOCAL __declspec(thread)
#else
#define FDN_WAMR_THREAD_LOCAL _Thread_local
#endif

static FDN_WAMR_THREAD_LOCAL uint32_t fdn_wamr_thread_depth;

static int fdn_wamr_thread_enter(void) {
    if (fdn_wamr_thread_depth == UINT32_MAX) return -1;
    if (fdn_wamr_thread_depth == 0 && !wasm_runtime_init_thread_env()) return -1;
    ++fdn_wamr_thread_depth;
    return 1;
}

static void fdn_wamr_thread_leave(int initialized) {
    if (initialized <= 0 || fdn_wamr_thread_depth == 0) abort();
    --fdn_wamr_thread_depth;
    if (fdn_wamr_thread_depth == 0) wasm_runtime_destroy_thread_env();
}

typedef struct fdn_wamr_strings {
    char **values;
    uint32_t count;
    uint32_t capacity;
} fdn_wamr_strings;

typedef struct fdn_wamr_engine {
    fdn_wamr_mutex lock;
    fdn_wamr_mutex loader_lock;
    fdn_wamr_condition idle;
    int active;
    int closing;
    uint32_t in_flight;
    uint32_t modules;
    struct fdn_wamr_engine_slot *slot;
} fdn_wamr_engine;

typedef struct fdn_wamr_slot fdn_wamr_slot;
typedef struct fdn_wamr_engine_slot fdn_wamr_engine_slot;

typedef struct fdn_wamr_module {
    struct fdn_wamr_module *next;
    fdn_wamr_slot *slot;
    fdn_wamr_engine *engine;
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t execution;
    uint8_t *contents;
    fdn_wamr_strings directories;
    fdn_wamr_strings environment;
    fdn_wamr_strings arguments;
    uint64_t payload_limit;
    fdn_wamr_mutex state_lock;
    fdn_wamr_condition idle;
    fdn_wamr_mutex execution_lock;
    uint32_t in_flight;
    int closing;
    void *host_context;
    fdn_wamr_host_dispatch_fn host_dispatch;
    uint8_t *host_response;
    size_t host_response_length;
    uint8_t *host_error;
    size_t host_error_length;
    atomic_int prepared;
    atomic_int started;
    atomic_int terminated;
#if defined(_WIN32)
    HANDLE standard_input;
    HANDLE standard_output;
    HANDLE standard_error;
#else
    int standard_input;
    int standard_output;
    int standard_error;
#endif
} fdn_wamr_module;

static FDN_WAMR_THREAD_LOCAL fdn_wamr_module *fdn_wamr_current_module;

struct fdn_wamr_slot {
    fdn_wamr_slot *next;
    uint32_t index;
    uint32_t generation;
    fdn_wamr_module *module;
};

struct fdn_wamr_engine_slot {
    fdn_wamr_engine_slot *next;
    uint32_t index;
    uint32_t generation;
    fdn_wamr_engine *engine;
};

static atomic_uint_fast32_t fdn_wamr_engines;
#if defined(_WIN32)
static fdn_wamr_mutex fdn_wamr_engine_lock = {SRWLOCK_INIT};
#else
static fdn_wamr_mutex fdn_wamr_engine_lock = {PTHREAD_MUTEX_INITIALIZER};
#endif
static fdn_wamr_module *fdn_wamr_modules;
static fdn_wamr_slot *fdn_wamr_slots;
static fdn_wamr_engine_slot *fdn_wamr_engine_slots;
static uint32_t fdn_wamr_next_slot = 1;
static uint32_t fdn_wamr_next_engine_slot = 1;

#if defined(FOUNDATION_WAMR_TESTING)
#if defined(_WIN32)
static fdn_wamr_mutex fdn_wamr_test_lock = {SRWLOCK_INIT};
static fdn_wamr_condition fdn_wamr_test_ready = {CONDITION_VARIABLE_INIT};
#else
static fdn_wamr_mutex fdn_wamr_test_lock = {PTHREAD_MUTEX_INITIALIZER};
static fdn_wamr_condition fdn_wamr_test_ready = {PTHREAD_COND_INITIALIZER};
#endif
static int fdn_wamr_test_open_blocked;
static int fdn_wamr_test_open_entered;
static int fdn_wamr_test_engine_closing;

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_block(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    fdn_wamr_test_open_blocked = 1;
    fdn_wamr_test_open_entered = 0;
    fdn_wamr_test_engine_closing = 0;
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_wait(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    while (!fdn_wamr_test_open_entered)
        fdn_wamr_condition_wait(&fdn_wamr_test_ready, &fdn_wamr_test_lock);
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_engine_close_wait(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    while (!fdn_wamr_test_engine_closing)
        fdn_wamr_condition_wait(&fdn_wamr_test_ready, &fdn_wamr_test_lock);
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_release(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    fdn_wamr_test_open_blocked = 0;
    fdn_wamr_condition_broadcast(&fdn_wamr_test_ready);
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}

static void fdn_wamr_test_open_barrier(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    if (fdn_wamr_test_open_blocked) {
        fdn_wamr_test_open_entered = 1;
        fdn_wamr_condition_broadcast(&fdn_wamr_test_ready);
        while (fdn_wamr_test_open_blocked)
            fdn_wamr_condition_wait(&fdn_wamr_test_ready, &fdn_wamr_test_lock);
    }
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}

static void fdn_wamr_test_engine_close_reached(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_test_lock);
    fdn_wamr_test_engine_closing = 1;
    fdn_wamr_condition_broadcast(&fdn_wamr_test_ready);
    fdn_wamr_mutex_unlock(&fdn_wamr_test_lock);
}
#else
static void fdn_wamr_test_open_barrier(void) {
}

static void fdn_wamr_test_engine_close_reached(void) {
}
#endif

static int fdn_wamr_name_valid(const fdn_string *value);

static wasm_module_t fdn_wamr_load(fdn_wamr_engine *engine,
                                   uint8_t *contents, uint32_t size,
                                   char *error, uint32_t error_size) {
    wasm_module_t module;
    fdn_wamr_mutex_lock(&engine->loader_lock);
    module = wasm_runtime_load(contents, size, error, error_size);
    fdn_wamr_mutex_unlock(&engine->loader_lock);
    return module;
}

static void fdn_wamr_unload(fdn_wamr_module *module) {
    if (module->module == NULL) return;
    fdn_wamr_mutex_lock(&module->engine->loader_lock);
    wasm_runtime_unload(module->module);
    fdn_wamr_mutex_unlock(&module->engine->loader_lock);
    module->module = NULL;
}

static void fdn_wamr_lock(void) {
    fdn_wamr_mutex_lock(&fdn_wamr_engine_lock);
}

static void fdn_wamr_unlock(void) {
    fdn_wamr_mutex_unlock(&fdn_wamr_engine_lock);
}

static void fdn_wamr_state_lock(fdn_wamr_mutex *lock) {
    fdn_wamr_mutex_lock(lock);
}

static void fdn_wamr_state_unlock(fdn_wamr_mutex *lock) {
    fdn_wamr_mutex_unlock(lock);
}

static int fdn_wamr_module_enter(fdn_wamr_module *module) {
    fdn_wamr_state_lock(&module->state_lock);
    if (module->closing || module->module == NULL) {
        fdn_wamr_state_unlock(&module->state_lock);
        return 0;
    }
    ++module->in_flight;
    fdn_wamr_state_unlock(&module->state_lock);
    return 1;
}

static void fdn_wamr_module_leave(fdn_wamr_module *module) {
    fdn_wamr_state_lock(&module->state_lock);
    if (module->in_flight == 0) {
        abort();
    }
    --module->in_flight;
    if (module->in_flight == 0) fdn_wamr_condition_broadcast(&module->idle);
    fdn_wamr_state_unlock(&module->state_lock);
}

static uint64_t fdn_wamr_reference(uint32_t index, uint32_t generation) {
    return ((uint64_t)generation << 32) | index;
}

static fdn_wamr_slot *fdn_wamr_slot_find(uint64_t reference) {
    fdn_wamr_slot *slot;
    const uint32_t index = (uint32_t)reference;
    const uint32_t generation = (uint32_t)(reference >> 32);
    for (slot = fdn_wamr_slots; slot != NULL; slot = slot->next) {
        if (slot->index == index && slot->generation == generation) {
            return slot;
        }
    }
    return NULL;
}

static fdn_wamr_slot *fdn_wamr_slot_assign(fdn_wamr_module *module) {
    fdn_wamr_slot *slot;
    for (slot = fdn_wamr_slots; slot != NULL; slot = slot->next) {
        if (slot->module == NULL && slot->generation != UINT32_MAX) {
            ++slot->generation;
            slot->module = module;
            return slot;
        }
    }
    slot = calloc(1, sizeof(*slot));
    if (slot == NULL || fdn_wamr_next_slot == 0) {
        free(slot);
        return NULL;
    }
    slot->index = fdn_wamr_next_slot++;
    slot->generation = 1;
    slot->module = module;
    slot->next = fdn_wamr_slots;
    fdn_wamr_slots = slot;
    return slot;
}

static fdn_wamr_module *fdn_wamr_module_acquire(uint64_t reference) {
    fdn_wamr_module *module;
    fdn_wamr_slot *slot;
    fdn_wamr_lock();
    slot = fdn_wamr_slot_find(reference);
    module = slot == NULL ? NULL : slot->module;
    if (module != NULL && !fdn_wamr_module_enter(module)) {
        module = NULL;
    }
    fdn_wamr_unlock();
    return module;
}

static fdn_wamr_engine_slot *fdn_wamr_engine_slot_find(uint64_t reference) {
    fdn_wamr_engine_slot *slot;
    const uint32_t index = (uint32_t)reference;
    const uint32_t generation = (uint32_t)(reference >> 32);
    for (slot = fdn_wamr_engine_slots; slot != NULL; slot = slot->next) {
        if (slot->index == index && slot->generation == generation) return slot;
    }
    return NULL;
}

static fdn_wamr_engine_slot *fdn_wamr_engine_slot_assign(fdn_wamr_engine *engine) {
    fdn_wamr_engine_slot *slot;
    for (slot = fdn_wamr_engine_slots; slot != NULL; slot = slot->next) {
        if (slot->engine == NULL && slot->generation != UINT32_MAX) {
            ++slot->generation;
            slot->engine = engine;
            return slot;
        }
    }
    slot = calloc(1, sizeof(*slot));
    if (slot == NULL || fdn_wamr_next_engine_slot == 0) {
        free(slot);
        return NULL;
    }
    slot->index = fdn_wamr_next_engine_slot++;
    slot->generation = 1;
    slot->engine = engine;
    slot->next = fdn_wamr_engine_slots;
    fdn_wamr_engine_slots = slot;
    return slot;
}

static void fdn_wamr_runtime_release(void) {
    const uint_fast32_t previous = atomic_fetch_sub(&fdn_wamr_engines, 1);
    if (previous == 0) abort();
    if (previous == 1) {
        fdn_wamr_slot *slot;
        fdn_wamr_engine_slot *engine_slot;
        if (fdn_wamr_modules != NULL) abort();
        wasm_runtime_destroy();
        while (fdn_wamr_slots != NULL) {
            slot = fdn_wamr_slots;
            if (slot->module != NULL) abort();
            fdn_wamr_slots = slot->next;
            free(slot);
        }
        while (fdn_wamr_engine_slots != NULL) {
            engine_slot = fdn_wamr_engine_slots;
            if (engine_slot->engine != NULL) abort();
            fdn_wamr_engine_slots = engine_slot->next;
            free(engine_slot);
        }
    }
}

static fdn_wamr_engine *fdn_wamr_engine_acquire(uint64_t reference) {
    fdn_wamr_engine_slot *slot;
    fdn_wamr_engine *engine;
    fdn_wamr_lock();
    slot = fdn_wamr_engine_slot_find(reference);
    engine = slot == NULL ? NULL : slot->engine;
    if (engine != NULL) {
        fdn_wamr_state_lock(&engine->lock);
        if (!engine->active || engine->closing) {
            engine = NULL;
        } else {
            ++engine->in_flight;
        }
        if (engine != NULL) fdn_wamr_state_unlock(&engine->lock);
        else fdn_wamr_state_unlock(&slot->engine->lock);
    }
    fdn_wamr_unlock();
    return engine;
}

static void fdn_wamr_engine_release(fdn_wamr_engine *engine) {
    fdn_wamr_state_lock(&engine->lock);
    if (engine->in_flight == 0) abort();
    --engine->in_flight;
    if (engine->in_flight == 0) fdn_wamr_condition_broadcast(&engine->idle);
    fdn_wamr_state_unlock(&engine->lock);
}

static fdn_wamr_module *fdn_wamr_module_for_execution(wasm_exec_env_t execution) {
    fdn_wamr_module *module;
    wasm_module_inst_t instance;
    module = fdn_wamr_current_module;
    if (module != NULL) {
        return fdn_wamr_module_enter(module) ? module : NULL;
    }
    instance = wasm_runtime_get_module_inst(execution);
    fdn_wamr_lock();
    for (module = fdn_wamr_modules; module != NULL; module = module->next) {
        if (module->instance == instance) {
            if (!fdn_wamr_module_enter(module)) {
                module = NULL;
            }
            break;
        }
    }
    fdn_wamr_unlock();
    return module;
}

static bool fdn_wamr_call_wasm(fdn_wamr_module *module,
                               wasm_function_inst_t function, uint32_t argc,
                               uint32_t arguments[]) {
    fdn_wamr_module *previous = fdn_wamr_current_module;
    bool called;
    fdn_wamr_current_module = module;
    called = wasm_runtime_call_wasm(module->execution, function, argc, arguments);
    fdn_wamr_current_module = previous;
    return called;
}

static int32_t fdn_wamr_host_read(wasm_exec_env_t execution, uint32_t pointer,
                                  uint32_t capacity, const uint8_t *source,
                                  size_t length) {
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(execution);
    void *destination;
    if (length > INT32_MAX) {
        return INT32_MIN;
    }
    if (capacity < length) {
        return -(int32_t)length;
    }
    if (length == 0) {
        return 0;
    }
    if (!wasm_runtime_validate_app_addr(instance, pointer, (uint32_t)length)) {
        return INT32_MIN;
    }
    destination = wasm_runtime_addr_app_to_native(instance, pointer);
    if (destination == NULL) {
        return INT32_MIN;
    }
    (void)memcpy(destination, source, length);
    return (int32_t)length;
}

static int32_t fdn_wamr_host_call(wasm_exec_env_t execution,
                                  uint32_t capability_pointer,
                                  uint32_t capability_length,
                                  uint32_t operation_pointer,
                                  uint32_t operation_length,
                                  uint32_t input_pointer,
                                  uint32_t input_length) {
    fdn_wamr_module *module = fdn_wamr_module_for_execution(execution);
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(execution);
    fdn_string capability;
    fdn_string operation;
    const uint8_t *input;
    uint8_t *response = NULL;
    uint8_t *error = NULL;
    size_t response_length = 0;
    size_t error_length = 0;
    void *host_context;
    fdn_wamr_host_dispatch_fn host_dispatch;
    int32_t status = FDN_WASM_HOST_INVALID_REQUEST;
    if (module == NULL) {
        return FDN_WASM_HOST_INVALID_REQUEST;
    }
    if (capability_length == 0 || operation_length == 0 ||
        !wasm_runtime_validate_app_addr(instance, capability_pointer,
                                        capability_length) ||
        !wasm_runtime_validate_app_addr(instance, operation_pointer,
                                        operation_length) ||
        (input_length != 0 && !wasm_runtime_validate_app_addr(
            instance, input_pointer, input_length))) {
        goto done;
    }
    capability.data = wasm_runtime_addr_app_to_native(instance, capability_pointer);
    capability.length = capability_length;
    capability.owned = 0;
    operation.data = wasm_runtime_addr_app_to_native(instance, operation_pointer);
    operation.length = operation_length;
    operation.owned = 0;
    input = input_length == 0 ? NULL : wasm_runtime_addr_app_to_native(instance, input_pointer);
    if (capability.data == NULL || operation.data == NULL ||
        (input_length != 0 && input == NULL)) {
        goto done;
    }
    if (!fdn_wamr_name_valid(&capability) || !fdn_wamr_name_valid(&operation)) {
        goto done;
    }
    fdn_wamr_state_lock(&module->state_lock);
    host_context = module->host_context;
    host_dispatch = module->host_dispatch;
    if (module->closing || host_dispatch == NULL) {
        fdn_wamr_state_unlock(&module->state_lock);
        status = FDN_WASM_HOST_HANDLER_ERROR;
        goto done;
    }
    fdn_wamr_state_unlock(&module->state_lock);
    status = host_dispatch(host_context, &capability, &operation, input, input_length,
                           &response, &response_length, &error, &error_length);
    if (status < FDN_WASM_HOST_OK || status > FDN_WASM_HOST_PAYLOAD_TOO_LARGE ||
        response_length > module->payload_limit || error_length > module->payload_limit ||
        (response_length != 0 && response == NULL) ||
        (error_length != 0 && error == NULL)) {
        free(response);
        free(error);
        status = FDN_WASM_HOST_HANDLER_ERROR;
        goto done;
    }
    fdn_wamr_state_lock(&module->state_lock);
    free(module->host_response);
    free(module->host_error);
    module->host_response = NULL;
    module->host_error = NULL;
    module->host_response_length = 0;
    module->host_error_length = 0;
    if (response_length != 0) {
        module->host_response = malloc(response_length);
        if (module->host_response != NULL) {
            (void)memcpy(module->host_response, response, response_length);
            module->host_response_length = response_length;
        }
    }
    if (error_length != 0) {
        module->host_error = malloc(error_length);
        if (module->host_error != NULL) {
            (void)memcpy(module->host_error, error, error_length);
            module->host_error_length = error_length;
        }
    }
    fdn_wamr_state_unlock(&module->state_lock);
    free(response);
    free(error);
    if ((response_length != 0 && module->host_response == NULL) ||
        (error_length != 0 && module->host_error == NULL)) {
        status = FDN_WASM_HOST_HANDLER_ERROR;
    }
done:
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_host_response_length(wasm_exec_env_t execution) {
    fdn_wamr_module *module = fdn_wamr_module_for_execution(execution);
    int32_t length;
    if (module == NULL) {
        return 0;
    }
    fdn_wamr_state_lock(&module->state_lock);
    length = module->host_response_length > INT32_MAX ? INT32_MAX
                                                       : (int32_t)module->host_response_length;
    fdn_wamr_state_unlock(&module->state_lock);
    fdn_wamr_module_leave(module);
    return length;
}

static int32_t fdn_wamr_host_response_read(wasm_exec_env_t execution,
                                           uint32_t pointer, uint32_t capacity) {
    fdn_wamr_module *module = fdn_wamr_module_for_execution(execution);
    int32_t result;
    if (module == NULL) {
        return INT32_MIN;
    }
    fdn_wamr_state_lock(&module->state_lock);
    result = fdn_wamr_host_read(execution, pointer, capacity, module->host_response,
                                module->host_response_length);
    fdn_wamr_state_unlock(&module->state_lock);
    fdn_wamr_module_leave(module);
    return result;
}

static int32_t fdn_wamr_host_error_length(wasm_exec_env_t execution) {
    fdn_wamr_module *module = fdn_wamr_module_for_execution(execution);
    int32_t length;
    if (module == NULL) {
        return 0;
    }
    fdn_wamr_state_lock(&module->state_lock);
    length = module->host_error_length > INT32_MAX ? INT32_MAX
                                                    : (int32_t)module->host_error_length;
    fdn_wamr_state_unlock(&module->state_lock);
    fdn_wamr_module_leave(module);
    return length;
}

static int32_t fdn_wamr_host_error_read(wasm_exec_env_t execution,
                                        uint32_t pointer, uint32_t capacity) {
    fdn_wamr_module *module = fdn_wamr_module_for_execution(execution);
    int32_t result;
    if (module == NULL) {
        return INT32_MIN;
    }
    fdn_wamr_state_lock(&module->state_lock);
    result = fdn_wamr_host_read(execution, pointer, capacity, module->host_error,
                                module->host_error_length);
    fdn_wamr_state_unlock(&module->state_lock);
    fdn_wamr_module_leave(module);
    return result;
}

static void fdn_wamr_host_call_raw(wasm_exec_env_t execution, uint64_t *arguments) {
    native_raw_return_type(int32_t, arguments);
    native_raw_get_arg(uint32_t, capability_pointer, arguments);
    native_raw_get_arg(uint32_t, capability_length, arguments);
    native_raw_get_arg(uint32_t, operation_pointer, arguments);
    native_raw_get_arg(uint32_t, operation_length, arguments);
    native_raw_get_arg(uint32_t, input_pointer, arguments);
    native_raw_get_arg(uint32_t, input_length, arguments);
    native_raw_set_return(
        fdn_wamr_host_call(execution, capability_pointer, capability_length,
                           operation_pointer, operation_length, input_pointer, input_length));
}

static void fdn_wamr_host_response_length_raw(wasm_exec_env_t execution,
                                              uint64_t *arguments) {
    native_raw_return_type(int32_t, arguments);
    native_raw_set_return(fdn_wamr_host_response_length(execution));
}

static void fdn_wamr_host_response_read_raw(wasm_exec_env_t execution,
                                            uint64_t *arguments) {
    native_raw_return_type(int32_t, arguments);
    native_raw_get_arg(uint32_t, pointer, arguments);
    native_raw_get_arg(uint32_t, capacity, arguments);
    native_raw_set_return(fdn_wamr_host_response_read(execution, pointer, capacity));
}

static void fdn_wamr_host_error_length_raw(wasm_exec_env_t execution,
                                           uint64_t *arguments) {
    native_raw_return_type(int32_t, arguments);
    native_raw_set_return(fdn_wamr_host_error_length(execution));
}

static void fdn_wamr_host_error_read_raw(wasm_exec_env_t execution,
                                         uint64_t *arguments) {
    native_raw_return_type(int32_t, arguments);
    native_raw_get_arg(uint32_t, pointer, arguments);
    native_raw_get_arg(uint32_t, capacity, arguments);
    native_raw_set_return(fdn_wamr_host_error_read(execution, pointer, capacity));
}

static NativeSymbol fdn_wamr_host_symbols[5];

static void fdn_wamr_host_symbols_initialize(void) {
    union {
        void (*function)(wasm_exec_env_t, uint64_t *);
        void *value;
    } host_call = {.function = fdn_wamr_host_call_raw},
      response_length = {.function = fdn_wamr_host_response_length_raw},
      response_read = {.function = fdn_wamr_host_response_read_raw},
      error_length = {.function = fdn_wamr_host_error_length_raw},
      error_read = {.function = fdn_wamr_host_error_read_raw};
    fdn_wamr_host_symbols[0] = (NativeSymbol){FDN_WASM_IMPORT_HOST_CALL,
                                              host_call.value, "(iiiiii)i", NULL};
    fdn_wamr_host_symbols[1] = (NativeSymbol){FDN_WASM_IMPORT_RESPONSE_LENGTH,
                                              response_length.value, "()i", NULL};
    fdn_wamr_host_symbols[2] = (NativeSymbol){FDN_WASM_IMPORT_RESPONSE_READ,
                                              response_read.value, "(ii)i", NULL};
    fdn_wamr_host_symbols[3] = (NativeSymbol){FDN_WASM_IMPORT_ERROR_LENGTH,
                                              error_length.value, "()i", NULL};
    fdn_wamr_host_symbols[4] = (NativeSymbol){FDN_WASM_IMPORT_ERROR_READ,
                                              error_read.value, "(ii)i", NULL};
}

static int fdn_wamr_text_valid(const fdn_string *value, size_t maximum) {
    return value != NULL && value->length <= maximum &&
           (value->length == 0 || value->data != NULL) &&
           (value->length == 0 || memchr(value->data, '\0', value->length) == NULL);
}

static int fdn_wamr_name_valid(const fdn_string *value) {
    size_t index;
    if (!fdn_wamr_text_valid(value, 128) || value->length == 0 ||
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

static char *fdn_wamr_copy_text(const fdn_string *value) {
    char *copy;
    if (!fdn_wamr_text_valid(value, 65536)) {
        return NULL;
    }
    copy = malloc(value->length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (value->length != 0) {
        (void)memcpy(copy, value->data, value->length);
    }
    copy[value->length] = '\0';
    return copy;
}

static void fdn_wamr_strings_drop(fdn_wamr_strings *values) {
    uint32_t index;
    for (index = 0; index < values->count; ++index) {
        free(values->values[index]);
    }
    free(values->values);
    values->values = NULL;
    values->count = 0;
    values->capacity = 0;
}

static int fdn_wamr_strings_push(fdn_wamr_strings *values, char *value) {
    char **next;
    uint32_t capacity;
    if (values->count == values->capacity) {
        capacity = values->capacity == 0 ? 4 : values->capacity * 2;
        if (capacity < values->capacity) {
            return 0;
        }
        next = realloc(values->values, (size_t)capacity * sizeof(*next));
        if (next == NULL) {
            return 0;
        }
        values->values = next;
        values->capacity = capacity;
    }
    values->values[values->count++] = value;
    return 1;
}

static int fdn_wamr_open_null_stdio(fdn_wamr_module *module) {
#if defined(_WIN32)
    module->standard_input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ |
                                         FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, NULL);
    module->standard_output = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ |
                                          FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, NULL);
    module->standard_error = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ |
                                         FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (module->standard_input == INVALID_HANDLE_VALUE ||
        module->standard_output == INVALID_HANDLE_VALUE ||
        module->standard_error == INVALID_HANDLE_VALUE) {
        return 0;
    }
#else
    module->standard_input = open("/dev/null", O_RDONLY);
    module->standard_output = open("/dev/null", O_WRONLY);
    module->standard_error = open("/dev/null", O_WRONLY);
    if (module->standard_input < 0 || module->standard_output < 0 ||
        module->standard_error < 0) {
        return 0;
    }
#endif
    return 1;
}

static void fdn_wamr_close_null_stdio(fdn_wamr_module *module) {
#if defined(_WIN32)
    if (module->standard_input != NULL && module->standard_input != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(module->standard_input);
    }
    if (module->standard_output != NULL && module->standard_output != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(module->standard_output);
    }
    if (module->standard_error != NULL && module->standard_error != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(module->standard_error);
    }
    module->standard_input = NULL;
    module->standard_output = NULL;
    module->standard_error = NULL;
#else
    if (module->standard_input >= 0) (void)close(module->standard_input);
    if (module->standard_output >= 0) (void)close(module->standard_output);
    if (module->standard_error >= 0) (void)close(module->standard_error);
    module->standard_input = -1;
    module->standard_output = -1;
    module->standard_error = -1;
#endif
}

static int32_t fdn_wamr_engine_open(uint64_t *output) {
    RuntimeInitArgs arguments;
    fdn_wamr_engine *engine;
    if (output == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *output = 0;
    fdn_wamr_lock();
    if (atomic_load(&fdn_wamr_engines) == 0) {
        fdn_wamr_host_symbols_initialize();
        (void)memset(&arguments, 0, sizeof(arguments));
        arguments.mem_alloc_type = Alloc_With_System_Allocator;
        if (!wasm_runtime_full_init(&arguments)) {
            fdn_wamr_unlock();
            return FDN_WAMR_HANDLER_ERROR;
        }
        if (!wasm_runtime_register_natives_raw(
                FDN_WASM_HOST_MODULE, fdn_wamr_host_symbols,
                (uint32_t)(sizeof(fdn_wamr_host_symbols) /
                           sizeof(fdn_wamr_host_symbols[0])))) {
            wasm_runtime_destroy();
            fdn_wamr_unlock();
            return FDN_WAMR_HANDLER_ERROR;
        }
        wasm_runtime_destroy_thread_env();
        atomic_store(&fdn_wamr_engines, 1);
    } else {
        (void)atomic_fetch_add(&fdn_wamr_engines, 1);
    }
    fdn_wamr_unlock();
    engine = calloc(1, sizeof(*engine));
    if (engine == NULL) {
        fdn_wamr_lock();
        fdn_wamr_runtime_release();
        fdn_wamr_unlock();
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_mutex_init(&engine->lock)) {
        free(engine);
        fdn_wamr_lock();
        fdn_wamr_runtime_release();
        fdn_wamr_unlock();
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_mutex_init(&engine->loader_lock)) {
        fdn_wamr_mutex_drop(&engine->lock);
        free(engine);
        fdn_wamr_lock();
        fdn_wamr_runtime_release();
        fdn_wamr_unlock();
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_condition_init(&engine->idle)) {
        fdn_wamr_mutex_drop(&engine->loader_lock);
        fdn_wamr_mutex_drop(&engine->lock);
        free(engine);
        fdn_wamr_lock();
        fdn_wamr_runtime_release();
        fdn_wamr_unlock();
        return FDN_WAMR_HANDLER_ERROR;
    }
    engine->active = 1;
    fdn_wamr_lock();
    engine->slot = fdn_wamr_engine_slot_assign(engine);
    if (engine->slot == NULL) {
        fdn_wamr_runtime_release();
        fdn_wamr_unlock();
        fdn_wamr_condition_drop(&engine->idle);
        fdn_wamr_mutex_drop(&engine->loader_lock);
        fdn_wamr_mutex_drop(&engine->lock);
        free(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    *output = fdn_wamr_reference(engine->slot->index, engine->slot->generation);
    fdn_wamr_unlock();
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_engine_close(uint64_t *value) {
    fdn_wamr_engine *engine;
    fdn_wamr_engine_slot *slot;
    const uint64_t reference = value == NULL ? 0 : *value;
    if (reference == 0) {
        return FDN_WAMR_OK;
    }
    fdn_wamr_lock();
    slot = fdn_wamr_engine_slot_find(reference);
    engine = slot == NULL ? NULL : slot->engine;
    if (engine == NULL) {
        fdn_wamr_unlock();
        *value = 0;
        return FDN_WAMR_CLOSED;
    }
    fdn_wamr_state_lock(&engine->lock);
    if (engine->modules != 0) {
        fdn_wamr_state_unlock(&engine->lock);
        fdn_wamr_unlock();
        return FDN_WAMR_CLOSED;
    }
    if (engine->closing) {
        fdn_wamr_state_unlock(&engine->lock);
        fdn_wamr_unlock();
        *value = 0;
        return FDN_WAMR_CLOSED;
    }
    engine->closing = 1;
    engine->active = 0;
    slot->engine = NULL;
    fdn_wamr_test_engine_close_reached();
    fdn_wamr_unlock();
    while (engine->in_flight != 0) {
        fdn_wamr_condition_wait(&engine->idle, &engine->lock);
    }
    fdn_wamr_state_unlock(&engine->lock);
    fdn_wamr_lock();
    fdn_wamr_runtime_release();
    fdn_wamr_unlock();
    fdn_wamr_condition_drop(&engine->idle);
    fdn_wamr_mutex_drop(&engine->loader_lock);
    fdn_wamr_mutex_drop(&engine->lock);
    free(engine);
    *value = 0;
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_open(uint64_t engine_value, const fdn_string *path,
                                    uint64_t payload_limit, uint64_t *output) {
    fdn_wamr_engine *engine;
    fdn_wamr_module *module;
    char *native_path;
    uint8_t *contents;
    long size;
    FILE *file;
    char error[256];
    if (output == NULL || !fdn_wamr_text_valid(path, 4096) || path->length == 0 ||
        payload_limit == 0 || payload_limit > FDN_WAMR_MAX_MODULE_BYTES) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *output = 0;
    engine = fdn_wamr_engine_acquire(engine_value);
    if (engine == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_test_open_barrier();
    native_path = fdn_wamr_copy_text(path);
    if (native_path == NULL) {
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
#if defined(_MSC_VER)
    (void)fopen_s(&file, native_path, "rb");
#else
    file = fopen(native_path, "rb");
#endif
    free(native_path);
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if ((uint64_t)size > UINT32_MAX ||
        (uint64_t)size > FDN_WAMR_MAX_MODULE_BYTES) {
        (void)fclose(file);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_PAYLOAD_TOO_LARGE;
    }
    contents = malloc((size_t)size == 0 ? 1 : (size_t)size);
    if (contents == NULL || fread(contents, 1, (size_t)size, file) != (size_t)size) {
        free(contents);
        (void)fclose(file);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (fclose(file) != 0) {
        free(contents);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)memset(error, 0, sizeof(error));
    module = calloc(1, sizeof(*module));
    if (module == NULL) {
        free(contents);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    module->engine = engine;
    if (!fdn_wamr_mutex_init(&module->state_lock)) {
        free(contents);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_condition_init(&module->idle)) {
        fdn_wamr_mutex_drop(&module->state_lock);
        free(contents);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_mutex_init(&module->execution_lock)) {
        fdn_wamr_condition_drop(&module->idle);
        fdn_wamr_mutex_drop(&module->state_lock);
        free(contents);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    atomic_init(&module->prepared, 0);
    atomic_init(&module->started, 0);
    atomic_init(&module->terminated, 0);
    module->module = fdn_wamr_load(
        engine, contents, (uint32_t)size, error, sizeof(error)
    );
    if (module->module == NULL) {
        free(contents);
        fdn_wamr_mutex_drop(&module->execution_lock);
        fdn_wamr_condition_drop(&module->idle);
        fdn_wamr_mutex_drop(&module->state_lock);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    module->contents = contents;
    module->payload_limit = payload_limit;
#if !defined(_WIN32)
    module->standard_input = -1;
    module->standard_output = -1;
    module->standard_error = -1;
#endif
    fdn_wamr_lock();
    fdn_wamr_state_lock(&engine->lock);
    if (!engine->active || engine->closing) {
        fdn_wamr_state_unlock(&engine->lock);
        fdn_wamr_unlock();
        fdn_wamr_unload(module);
        free(module->contents);
        fdn_wamr_mutex_drop(&module->execution_lock);
        fdn_wamr_condition_drop(&module->idle);
        fdn_wamr_mutex_drop(&module->state_lock);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_CLOSED;
    }
    module->slot = fdn_wamr_slot_assign(module);
    if (module->slot == NULL) {
        fdn_wamr_state_unlock(&engine->lock);
        fdn_wamr_unlock();
        fdn_wamr_unload(module);
        free(module->contents);
        fdn_wamr_mutex_drop(&module->execution_lock);
        fdn_wamr_condition_drop(&module->idle);
        fdn_wamr_mutex_drop(&module->state_lock);
        free(module);
        fdn_wamr_engine_release(engine);
        return FDN_WAMR_HANDLER_ERROR;
    }
    module->next = fdn_wamr_modules;
    fdn_wamr_modules = module;
    *output = fdn_wamr_reference(module->slot->index, module->slot->generation);
    ++engine->modules;
    fdn_wamr_state_unlock(&engine->lock);
    fdn_wamr_unlock();
    fdn_wamr_engine_release(engine);
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_allow_read_acquired(void *value,
                                                   const fdn_string *path) {
    fdn_wamr_module *module = value;
    if (module == NULL || module->instance != NULL ||
        module->directories.count >= FDN_WAMR_MAX_WASI_ENTRIES ||
        !fdn_wamr_text_valid(path, 4096) || path->length == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    return FDN_WAMR_READ_ONLY_UNSUPPORTED;
}

static int32_t fdn_wamr_module_allow_write_acquired(void *value,
                                                    const fdn_string *path) {
    fdn_wamr_module *module = value;
    char *copy;
    if (module == NULL || module->instance != NULL ||
        module->directories.count >= FDN_WAMR_MAX_WASI_ENTRIES ||
        !fdn_wamr_text_valid(path, 4096) || path->length == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    copy = fdn_wamr_copy_text(path);
    if (copy == NULL || !fdn_wamr_strings_push(&module->directories, copy)) {
        free(copy);
        return FDN_WAMR_HANDLER_ERROR;
    }
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_set_environment_acquired(
    void *value, const fdn_string *key, const fdn_string *entry_value) {
    fdn_wamr_module *module = value;
    char *name;
    char *contents;
    char *entry;
    size_t length;
    if (module == NULL || module->instance != NULL ||
        module->environment.count >= FDN_WAMR_MAX_WASI_ENTRIES ||
        !fdn_wamr_text_valid(key, 128) ||
        key->length == 0 || memchr(key->data, '=', key->length) != NULL ||
        !fdn_wamr_text_valid(entry_value, 4096)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    name = fdn_wamr_copy_text(key);
    contents = fdn_wamr_copy_text(entry_value);
    if (name == NULL || contents == NULL) {
        free(name);
        free(contents);
        return FDN_WAMR_HANDLER_ERROR;
    }
    length = strlen(name) + strlen(contents) + 2;
    entry = malloc(length);
    if (entry == NULL) {
        free(name);
        free(contents);
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)snprintf(entry, length, "%s=%s", name, contents);
    free(name);
    free(contents);
    if (!fdn_wamr_strings_push(&module->environment, entry)) {
        free(entry);
        return FDN_WAMR_HANDLER_ERROR;
    }
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_add_argument_acquired(
    void *value, const fdn_string *argument) {
    fdn_wamr_module *module = value;
    char *copy;
    if (module == NULL || module->instance != NULL ||
        module->arguments.count >= FDN_WAMR_MAX_WASI_ENTRIES ||
        !fdn_wamr_text_valid(argument, 4096)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    copy = fdn_wamr_copy_text(argument);
    if (copy == NULL || !fdn_wamr_strings_push(&module->arguments, copy)) {
        free(copy);
        return FDN_WAMR_HANDLER_ERROR;
    }
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_set_host_dispatch_acquired(
    void *value, void *context, fdn_wamr_host_dispatch_fn dispatch) {
    fdn_wamr_module *module = value;
    if (module == NULL || module->instance != NULL || dispatch == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    fdn_wamr_state_lock(&module->state_lock);
    module->host_context = context;
    module->host_dispatch = dispatch;
    fdn_wamr_state_unlock(&module->state_lock);
    return FDN_WAMR_OK;
}

static wasm_function_inst_t fdn_wamr_required_export(fdn_wamr_module *module,
                                                      const char *name) {
    return wasm_runtime_lookup_function(module->instance, name);
}

static int fdn_wamr_memory_exported(fdn_wamr_module *module,
                                    const char *name) {
    const int32_t count = wasm_runtime_get_export_count(module->module);
    int32_t index;
    if (count < 0) return 0;
    for (index = 0; index < count; ++index) {
        wasm_export_t value;
        (void)memset(&value, 0, sizeof(value));
        wasm_runtime_get_export_type(module->module, index, &value);
        if (value.kind == WASM_IMPORT_EXPORT_KIND_MEMORY && value.name != NULL &&
            strcmp(value.name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fdn_wamr_export_type(fdn_wamr_module *module, const char *name,
                                const wasm_valkind_t *parameters,
                                uint32_t parameter_count,
                                const wasm_valkind_t *results,
                                uint32_t result_count) {
    wasm_function_inst_t function = fdn_wamr_required_export(module, name);
    wasm_valkind_t actual_parameters[4];
    wasm_valkind_t actual_results[1];
    if (function == NULL ||
        wasm_func_get_param_count(function, module->instance) != parameter_count ||
        wasm_func_get_result_count(function, module->instance) != result_count ||
        parameter_count > sizeof(actual_parameters) / sizeof(actual_parameters[0]) ||
        result_count > sizeof(actual_results) / sizeof(actual_results[0])) {
        return 0;
    }
    if (parameter_count != 0) {
        wasm_func_get_param_types(function, module->instance, actual_parameters);
        if (memcmp(actual_parameters, parameters,
                   parameter_count * sizeof(actual_parameters[0])) != 0) {
            return 0;
        }
    }
    if (result_count != 0) {
        wasm_func_get_result_types(function, module->instance, actual_results);
        if (memcmp(actual_results, results,
                   result_count * sizeof(actual_results[0])) != 0) {
            return 0;
        }
    }
    return 1;
}

static int32_t fdn_wamr_call_status(fdn_wamr_module *module, const char *name) {
    wasm_function_inst_t function;
    uint32_t result[1] = {0};
    if (atomic_load(&module->terminated)) {
        return FDN_WAMR_CLOSED;
    }
    function = fdn_wamr_required_export(module, name);
    if (function == NULL || !fdn_wamr_call_wasm(module, function, 0, result)) {
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    return result[0] == 0 ? FDN_WAMR_OK : FDN_WAMR_HANDLER_ERROR;
}

static int32_t fdn_wamr_validate_contract(fdn_wamr_module *module) {
    static const wasm_valkind_t i32[] = {WASM_I32};
    static const wasm_valkind_t i64[] = {WASM_I64};
    static const wasm_valkind_t two_i32[] = {WASM_I32, WASM_I32};
    static const wasm_valkind_t four_i32[] = {
        WASM_I32, WASM_I32, WASM_I32, WASM_I32,
    };
    wasm_function_inst_t version;
    uint32_t result[2] = {0, 0};
    uint64_t packed;
    if (!fdn_wamr_memory_exported(module, FDN_WASM_EXPORT_MEMORY) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_ABI_VERSION, NULL, 0,
                              i64, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_ALLOCATE, i32, 1,
                              i32, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_FREE, two_i32, 2,
                              NULL, 0) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_METADATA, NULL, 0,
                              i64, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_START, NULL, 0,
                              i32, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_STOP, NULL, 0,
                              i32, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_CALL, four_i32, 4,
                              i64, 1) ||
        !fdn_wamr_export_type(module, FDN_WASM_EXPORT_LAST_ERROR, NULL, 0,
                              i64, 1)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    version = fdn_wamr_required_export(module, FDN_WASM_EXPORT_ABI_VERSION);
    if (!fdn_wamr_call_wasm(module, version, 0, result)) {
        return FDN_WAMR_HANDLER_ERROR;
    }
    (void)memcpy(&packed, result, sizeof(packed));
    if ((uint32_t)(packed >> 32) != FDN_WASM_ABI_MAJOR ||
        (uint32_t)packed != FDN_WASM_ABI_MINOR) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_prepare_acquired(void *value) {
    fdn_wamr_module *module = value;
    const InstantiationArgs instantiation = {
        65536,
        0,
        0,
    };
    char error[256];
    int32_t status;
    if (module == NULL || module->module == NULL || module->instance != NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (!fdn_wamr_open_null_stdio(module)) {
        fdn_wamr_close_null_stdio(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    wasm_runtime_set_wasi_args_ex(module->module,
                                  (const char **)module->directories.values,
                                  module->directories.count, NULL, 0,
                                  (const char **)module->environment.values,
                                  module->environment.count, module->arguments.values,
                                  (int)module->arguments.count,
                                  (int64_t)(intptr_t)module->standard_input,
                                  (int64_t)(intptr_t)module->standard_output,
                                  (int64_t)(intptr_t)module->standard_error);
    wasm_runtime_set_wasi_addr_pool(module->module, NULL, 0);
    wasm_runtime_set_wasi_ns_lookup_pool(module->module, NULL, 0);
    (void)memset(error, 0, sizeof(error));
    module->instance = wasm_runtime_instantiate_ex(module->module, &instantiation,
                                                   error, sizeof(error));
    if (module->instance == NULL) {
        fdn_wamr_close_null_stdio(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    module->execution = wasm_runtime_create_exec_env(module->instance, 65536);
    if (module->execution == NULL) {
        wasm_runtime_deinstantiate(module->instance);
        module->instance = NULL;
        fdn_wamr_close_null_stdio(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_validate_contract(module);
    if (status != FDN_WAMR_OK) {
        wasm_runtime_destroy_exec_env(module->execution);
        module->execution = NULL;
        wasm_runtime_deinstantiate(module->instance);
        module->instance = NULL;
        fdn_wamr_close_null_stdio(module);
        return status;
    }
    atomic_store(&module->prepared, 1);
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_start_acquired(void *value) {
    fdn_wamr_module *module = value;
    int32_t status;
    if (module == NULL) return FDN_WAMR_INVALID_REQUEST;
    fdn_wamr_state_lock(&module->execution_lock);
    if (!atomic_load(&module->prepared) || module->execution == NULL ||
        atomic_load(&module->terminated) || atomic_load(&module->started)) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_INVALID_REQUEST;
    }
    status = fdn_wamr_call_status(module, FDN_WASM_EXPORT_START);
    if (status == FDN_WAMR_OK) {
        atomic_store(&module->started, 1);
    }
    fdn_wamr_state_unlock(&module->execution_lock);
    return status;
}

static int32_t fdn_wamr_module_stop_acquired(void *value) {
    fdn_wamr_module *module = value;
    int32_t status;
    if (module == NULL) return FDN_WAMR_INVALID_REQUEST;
    fdn_wamr_state_lock(&module->execution_lock);
    if (!atomic_load(&module->prepared) || module->execution == NULL) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (!atomic_load(&module->started)) {
        status = atomic_load(&module->terminated) ? FDN_WAMR_CLOSED : FDN_WAMR_OK;
        fdn_wamr_state_unlock(&module->execution_lock);
        return status;
    }
    status = fdn_wamr_call_status(module, FDN_WASM_EXPORT_STOP);
    if (status == FDN_WAMR_OK) {
        atomic_store(&module->started, 0);
    }
    fdn_wamr_state_unlock(&module->execution_lock);
    return status;
}

static int32_t fdn_wamr_module_cancel_acquired(void *value) {
    fdn_wamr_module *module = value;
    if (module == NULL || !atomic_load(&module->prepared)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (atomic_exchange(&module->terminated, 1)) {
        return FDN_WAMR_CLOSED;
    }
    atomic_store(&module->started, 0);
    wasm_runtime_terminate(module->instance);
    return FDN_WAMR_OK;
}

static int fdn_wamr_app_range_valid(wasm_module_inst_t instance,
                                    uint32_t offset, uint32_t length) {
    uint64_t start;
    uint64_t end;
    if (length == 0) return 1;
    return wasm_runtime_get_app_addr_range(instance, offset, &start, &end) &&
           (uint64_t)offset >= start &&
           (uint64_t)offset + length <= end;
}

static int32_t fdn_wamr_guest_alloc(fdn_wamr_module *module, uint32_t length,
                                    uint32_t *offset) {
    wasm_function_inst_t function;
    uint32_t arguments[1];
    if (offset == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    function = fdn_wamr_required_export(module, FDN_WASM_EXPORT_ALLOCATE);
    if (function == NULL) {
        return FDN_WAMR_HANDLER_ERROR;
    }
    arguments[0] = length;
    if (!fdn_wamr_call_wasm(module, function, 1, arguments) ||
        (length != 0 && (arguments[0] == 0 || !fdn_wamr_app_range_valid(
            module->instance, arguments[0], length)))) {
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    *offset = arguments[0];
    return FDN_WAMR_OK;
}

static void fdn_wamr_guest_free(fdn_wamr_module *module, uint32_t offset,
                                uint32_t length) {
    wasm_function_inst_t function;
    uint32_t arguments[2];
    if (offset == 0) {
        return;
    }
    function = fdn_wamr_required_export(module, FDN_WASM_EXPORT_FREE);
    if (function == NULL) {
        return;
    }
    arguments[0] = offset;
    arguments[1] = length;
    (void)fdn_wamr_call_wasm(module, function, 2, arguments);
}

static int fdn_wamr_ranges_overlap(uint32_t left_offset, uint32_t left_length,
                                   uint32_t right_offset, uint32_t right_length) {
    const uint64_t left_end = (uint64_t)left_offset + left_length;
    const uint64_t right_end = (uint64_t)right_offset + right_length;
    return left_length != 0 && right_length != 0 &&
           (uint64_t)left_offset < right_end &&
           (uint64_t)right_offset < left_end;
}

static int32_t fdn_wamr_read_last_error(fdn_wamr_module *module) {
    wasm_function_inst_t function =
        fdn_wamr_required_export(module, FDN_WASM_EXPORT_LAST_ERROR);
    uint32_t arguments[2] = {0, 0};
    uint64_t packed;
    uint32_t offset;
    uint32_t length;
    if (function == NULL ||
        !fdn_wamr_call_wasm(module, function, 0, arguments)) {
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    (void)memcpy(&packed, arguments, sizeof(packed));
    offset = fdn_wasm_buffer_pointer(packed);
    length = fdn_wasm_buffer_length(packed);
    if (length == 0 || length > module->payload_limit ||
        !fdn_wamr_app_range_valid(module->instance, offset, length) ||
        wasm_runtime_addr_app_to_native(module->instance, offset) == NULL) {
        return length > module->payload_limit ? FDN_WAMR_PAYLOAD_TOO_LARGE
                                              : FDN_WAMR_HANDLER_ERROR;
    }
    return FDN_WAMR_HANDLER_ERROR;
}

static int32_t fdn_wamr_module_metadata_acquired(void *value, uint8_t **output,
                                                 size_t *output_length) {
    fdn_wamr_module *module = value;
    wasm_function_inst_t function;
    uint32_t arguments[2] = {0, 0};
    uint64_t packed;
    uint32_t offset;
    uint32_t length;
    void *data;
    if (output == NULL || output_length == NULL || module == NULL ||
        !atomic_load(&module->prepared) || atomic_load(&module->terminated)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *output = NULL;
    *output_length = 0;
    fdn_wamr_state_lock(&module->execution_lock);
    function = fdn_wamr_required_export(module, FDN_WASM_EXPORT_METADATA);
    if (function == NULL ||
        !fdn_wamr_call_wasm(module, function, 0, arguments)) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    (void)memcpy(&packed, arguments, sizeof(packed));
    offset = fdn_wasm_buffer_pointer(packed);
    length = fdn_wasm_buffer_length(packed);
    if (length == 0 || length > FDN_WAMR_MAX_METADATA_BYTES ||
        !fdn_wamr_app_range_valid(module->instance, offset, length)) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return length > FDN_WAMR_MAX_METADATA_BYTES ? FDN_WAMR_PAYLOAD_TOO_LARGE
                                                    : FDN_WAMR_HANDLER_ERROR;
    }
    data = length == 0 ? NULL : wasm_runtime_addr_app_to_native(module->instance, offset);
    if (length != 0 && data == NULL) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    *output = malloc(length);
    if (*output == NULL) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (length != 0) {
        (void)memcpy(*output, data, length);
    }
    *output_length = length;
    fdn_wamr_state_unlock(&module->execution_lock);
    return FDN_WAMR_OK;
}

static int32_t fdn_wamr_module_call_acquired(
    void *value, const fdn_string *method, const uint8_t *input,
    size_t input_length, uint8_t **output, size_t *output_length) {
    fdn_wamr_module *module = value;
    wasm_function_inst_t function;
    uint32_t method_offset = 0;
    uint32_t input_offset = 0;
    uint32_t input_allocation_length;
    uint32_t output_offset;
    uint32_t arguments[4];
    uint64_t packed;
    uint32_t result_length;
    void *method_data;
    void *input_data;
    void *result_data;
    if (output == NULL || output_length == NULL || module == NULL ||
        !atomic_load(&module->prepared) || !fdn_wamr_name_valid(method) ||
        input_length > module->payload_limit || input_length > UINT32_MAX ||
        (input_length != 0 && input == NULL)) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    *output = NULL;
    *output_length = 0;
    input_allocation_length = input_length == 0 ? 1 : (uint32_t)input_length;
    fdn_wamr_state_lock(&module->execution_lock);
    if (module->execution == NULL || !atomic_load(&module->started) ||
        atomic_load(&module->terminated)) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_INVALID_REQUEST;
    }
    function = wasm_runtime_lookup_function(module->instance, FDN_WASM_EXPORT_CALL);
    if (function == NULL) {
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (fdn_wamr_guest_alloc(module, (uint32_t)method->length, &method_offset) != FDN_WAMR_OK ||
        fdn_wamr_guest_alloc(module, input_allocation_length, &input_offset) != FDN_WAMR_OK) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    method_data = wasm_runtime_addr_app_to_native(module->instance, method_offset);
    input_data = wasm_runtime_addr_app_to_native(module->instance, input_offset);
    if (method_data == NULL || input_data == NULL) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (method->length != 0) (void)memcpy(method_data, method->data, method->length);
    if (input_length != 0) (void)memcpy(input_data, input, input_length);
    arguments[0] = method_offset;
    arguments[1] = (uint32_t)method->length;
    arguments[2] = input_offset;
    arguments[3] = (uint32_t)input_length;
    if (!fdn_wamr_call_wasm(module, function, 4, arguments)) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return atomic_load(&module->terminated) ? FDN_WAMR_CLOSED
                                                : FDN_WAMR_HANDLER_ERROR;
    }
    (void)memcpy(&packed, arguments, sizeof(packed));
    if (packed == FDN_WASM_ERROR_RESULT) {
        const int32_t status = fdn_wamr_read_last_error(module);
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return status;
    }
    output_offset = fdn_wasm_buffer_pointer(packed);
    result_length = fdn_wasm_buffer_length(packed);
    if (result_length > module->payload_limit ||
        fdn_wamr_ranges_overlap(output_offset, result_length, method_offset,
                                (uint32_t)method->length) ||
        fdn_wamr_ranges_overlap(output_offset, result_length, input_offset,
                                input_allocation_length)) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return result_length > module->payload_limit ? FDN_WAMR_PAYLOAD_TOO_LARGE
                                                     : FDN_WAMR_HANDLER_ERROR;
    }
    if (!fdn_wamr_app_range_valid(module->instance, output_offset,
                                  result_length)) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    result_data = result_length == 0 ? NULL :
        wasm_runtime_addr_app_to_native(module->instance, output_offset);
    if (result_length != 0 && result_data == NULL) {
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (result_length != 0) {
        *output = malloc(result_length);
    }
    if (result_length != 0 && *output == NULL) {
        fdn_wamr_guest_free(module, output_offset, result_length);
        fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
        fdn_wamr_guest_free(module, input_offset, input_allocation_length);
        fdn_wamr_state_unlock(&module->execution_lock);
        return FDN_WAMR_HANDLER_ERROR;
    }
    if (result_length != 0) {
        (void)memcpy(*output, result_data, result_length);
    }
    fdn_wamr_guest_free(module, output_offset, result_length);
    fdn_wamr_guest_free(module, method_offset, (uint32_t)method->length);
    fdn_wamr_guest_free(module, input_offset, input_allocation_length);
    *output_length = result_length;
    fdn_wamr_state_unlock(&module->execution_lock);
    return FDN_WAMR_OK;
}

static void fdn_wamr_free_output(uint64_t value, uint8_t *output, size_t output_length) {
    (void)value;
    (void)output_length;
    free(output);
}

static int32_t fdn_wamr_module_allow_read(uint64_t value, const fdn_string *path) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_allow_read_acquired(module, path);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_allow_write(uint64_t value, const fdn_string *path) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_allow_write_acquired(module, path);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_set_environment(
    uint64_t value, const fdn_string *key, const fdn_string *entry_value) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_set_environment_acquired(module, key, entry_value);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_add_argument(uint64_t value,
                                             const fdn_string *argument) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_add_argument_acquired(module, argument);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_set_host_dispatch(
    uint64_t value, void *context, fdn_wamr_host_dispatch_fn dispatch) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_set_host_dispatch_acquired(module, context, dispatch);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_prepare(uint64_t value) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int thread;
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    thread = fdn_wamr_thread_enter();
    if (thread < 0) {
        fdn_wamr_module_leave(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    fdn_wamr_state_lock(&module->execution_lock);
    status = fdn_wamr_module_prepare_acquired(module);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_thread_leave(thread);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_metadata(uint64_t value, uint8_t **output,
                                        size_t *output_length) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int thread;
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    thread = fdn_wamr_thread_enter();
    if (thread < 0) {
        fdn_wamr_module_leave(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_module_metadata_acquired(module, output, output_length);
    fdn_wamr_thread_leave(thread);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_start(uint64_t value) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int thread;
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    thread = fdn_wamr_thread_enter();
    if (thread < 0) {
        fdn_wamr_module_leave(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_module_start_acquired(module);
    fdn_wamr_thread_leave(thread);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_stop(uint64_t value) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int thread;
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    thread = fdn_wamr_thread_enter();
    if (thread < 0) {
        fdn_wamr_module_leave(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_module_stop_acquired(module);
    fdn_wamr_thread_leave(thread);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_cancel(uint64_t value) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    status = fdn_wamr_module_cancel_acquired(module);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_call(uint64_t value, const fdn_string *method,
                                    const uint8_t *input, size_t input_length,
                                    uint8_t **output, size_t *output_length) {
    fdn_wamr_module *module = fdn_wamr_module_acquire(value);
    int thread;
    int32_t status;
    if (module == NULL) return FDN_WAMR_CLOSED;
    thread = fdn_wamr_thread_enter();
    if (thread < 0) {
        fdn_wamr_module_leave(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    status = fdn_wamr_module_call_acquired(
        module, method, input, input_length, output, output_length
    );
    fdn_wamr_thread_leave(thread);
    fdn_wamr_module_leave(module);
    return status;
}

static int32_t fdn_wamr_module_close(uint64_t *value) {
    fdn_wamr_module *module;
    fdn_wamr_module **current;
    fdn_wamr_slot *slot;
    int thread;
    const uint64_t reference = value == NULL ? 0 : *value;
    if (reference == 0) {
        return FDN_WAMR_OK;
    }
    module = NULL;
    fdn_wamr_lock();
    slot = fdn_wamr_slot_find(reference);
    if (slot != NULL) {
        module = slot->module;
    }
    current = &fdn_wamr_modules;
    while (module != NULL && *current != NULL && *current != module)
        current = &(*current)->next;
    if (module == NULL) {
        fdn_wamr_unlock();
        *value = 0;
        return FDN_WAMR_CLOSED;
    }
    if (*current != module) abort();
    fdn_wamr_state_lock(&module->state_lock);
    if (module->closing) {
        fdn_wamr_state_unlock(&module->state_lock);
        fdn_wamr_unlock();
        *value = 0;
        return FDN_WAMR_CLOSED;
    }
    module->closing = 1;
    slot->module = NULL;
    *current = module->next;
    module->next = NULL;
    *value = 0;
    fdn_wamr_state_unlock(&module->state_lock);
    fdn_wamr_unlock();
    fdn_wamr_state_lock(&module->state_lock);
    while (module->in_flight != 0) {
        fdn_wamr_condition_wait(&module->idle, &module->state_lock);
    }
    fdn_wamr_state_unlock(&module->state_lock);
    if (module->engine == NULL) abort();
    fdn_wamr_state_lock(&module->execution_lock);
    if (atomic_load(&module->started) && !atomic_load(&module->terminated)) {
        thread = fdn_wamr_thread_enter();
        if (thread < 0) {
            atomic_store(&module->terminated, 1);
            wasm_runtime_terminate(module->instance);
        } else {
            (void)fdn_wamr_call_status(module, FDN_WASM_EXPORT_STOP);
            fdn_wamr_thread_leave(thread);
        }
    }
    atomic_store(&module->started, 0);
    if (module->execution != NULL) wasm_runtime_destroy_exec_env(module->execution);
    if (module->instance != NULL) wasm_runtime_deinstantiate(module->instance);
    fdn_wamr_close_null_stdio(module);
    fdn_wamr_unload(module);
    free(module->contents);
    fdn_wamr_strings_drop(&module->directories);
    fdn_wamr_strings_drop(&module->environment);
    fdn_wamr_strings_drop(&module->arguments);
    free(module->host_response);
    free(module->host_error);
    fdn_wamr_state_unlock(&module->execution_lock);
    fdn_wamr_state_lock(&module->engine->lock);
    if (module->engine->modules == 0) abort();
    --module->engine->modules;
    fdn_wamr_condition_broadcast(&module->engine->idle);
    fdn_wamr_state_unlock(&module->engine->lock);
    fdn_wamr_mutex_drop(&module->execution_lock);
    fdn_wamr_condition_drop(&module->idle);
    fdn_wamr_mutex_drop(&module->state_lock);
    free(module);
    return FDN_WAMR_OK;
}

FDN_WAMR_PROVIDER_EXPORT const fdn_wamr_provider_v2 *foundation_wamr_provider_query(void) {
    static const fdn_wamr_provider_v2 provider = {
        FDN_WAMR_PROVIDER_ABI_MAJOR,
        FDN_WAMR_PROVIDER_ABI_MINOR,
        sizeof(fdn_wamr_provider_v2),
        fdn_wamr_engine_open,
        fdn_wamr_engine_close,
        fdn_wamr_module_open,
        fdn_wamr_module_allow_read,
        fdn_wamr_module_allow_write,
        fdn_wamr_module_set_environment,
        fdn_wamr_module_add_argument,
        fdn_wamr_module_set_host_dispatch,
        fdn_wamr_module_prepare,
        fdn_wamr_module_metadata,
        fdn_wamr_module_start,
        fdn_wamr_module_stop,
        fdn_wamr_module_cancel,
        fdn_wamr_module_call,
        fdn_wamr_free_output,
        fdn_wamr_module_close,
    };
    return &provider;
}
