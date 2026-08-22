#include "foundation/wamr_provider.h"
#include "foundation/wasm_plugin.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct fake_engine {
    int active;
    unsigned int modules;
} fake_engine;

typedef struct fake_module {
    fake_engine *engine;
    int write_path;
    int environment;
    int argument;
    int prepared;
    void *host_context;
    fdn_wamr_host_dispatch_fn host_dispatch;
    atomic_int started;
    atomic_int cancelled;
} fake_module;

typedef struct fake_slot {
    uint32_t generation;
    fake_module *module;
} fake_slot;

enum { FAKE_MODULE_SLOT_COUNT = 64 };

static fake_slot fake_module_slots[FAKE_MODULE_SLOT_COUNT];
static uint64_t fake_live_engines;
static uint64_t fake_live_modules;

#if defined(_WIN32)
static SRWLOCK fake_open_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE fake_open_ready = CONDITION_VARIABLE_INIT;
#else
static pthread_mutex_t fake_open_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fake_open_ready = PTHREAD_COND_INITIALIZER;
#endif
static int fake_open_blocked;
static int fake_open_entered;

static void fake_open_lock_acquire(void) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&fake_open_lock);
#else
    if (pthread_mutex_lock(&fake_open_lock) != 0) abort();
#endif
}

static void fake_open_lock_release(void) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&fake_open_lock);
#else
    if (pthread_mutex_unlock(&fake_open_lock) != 0) abort();
#endif
}

static void fake_open_wait(void) {
#if defined(_WIN32)
    if (!SleepConditionVariableSRW(&fake_open_ready, &fake_open_lock,
                                   INFINITE, 0)) abort();
#else
    if (pthread_cond_wait(&fake_open_ready, &fake_open_lock) != 0) abort();
#endif
}

static void fake_open_broadcast(void) {
#if defined(_WIN32)
    WakeAllConditionVariable(&fake_open_ready);
#else
    if (pthread_cond_broadcast(&fake_open_ready) != 0) abort();
#endif
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_block(void) {
    fake_open_lock_acquire();
    fake_open_entered = 0;
    fake_open_blocked = 1;
    fake_open_lock_release();
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_wait(void) {
    fake_open_lock_acquire();
    while (!fake_open_entered) fake_open_wait();
    fake_open_lock_release();
}

FDN_WAMR_PROVIDER_EXPORT void foundation_wamr_provider_test_open_release(void) {
    fake_open_lock_acquire();
    fake_open_blocked = 0;
    fake_open_broadcast();
    fake_open_lock_release();
}

FDN_WAMR_PROVIDER_EXPORT uint64_t foundation_wamr_provider_test_live_engines(void) {
    uint64_t count;
    fake_open_lock_acquire();
    count = fake_live_engines;
    fake_open_lock_release();
    return count;
}

FDN_WAMR_PROVIDER_EXPORT uint64_t foundation_wamr_provider_test_live_modules(void) {
    uint64_t count;
    fake_open_lock_acquire();
    count = fake_live_modules;
    fake_open_lock_release();
    return count;
}

static void fake_open_barrier(void) {
    fake_open_lock_acquire();
    if (fake_open_blocked) {
        fake_open_entered = 1;
        fake_open_broadcast();
        while (fake_open_blocked) fake_open_wait();
    }
    fake_open_lock_release();
}

static uint64_t fake_module_reference(size_t index) {
    return ((uint64_t)fake_module_slots[index].generation << 32) |
           (uint64_t)(index + 1);
}

static fake_slot *fake_module_slot_find(uint64_t value) {
    const uint32_t reference = (uint32_t)value;
    const uint32_t generation = (uint32_t)(value >> 32);
    fake_slot *slot;
    if (reference == 0 || reference > FAKE_MODULE_SLOT_COUNT) return NULL;
    slot = &fake_module_slots[reference - 1];
    if (slot->generation != generation) return NULL;
    return slot;
}

static fake_module *fake_module_value(uint64_t value) {
    fake_module *module;
    fake_slot *slot;
    fake_open_lock_acquire();
    slot = fake_module_slot_find(value);
    module = slot == NULL ? NULL : slot->module;
    fake_open_lock_release();
    return module;
}

static size_t fake_module_slot_assign(fake_module *module) {
    size_t index;
    for (index = 0; index < FAKE_MODULE_SLOT_COUNT; ++index) {
        fake_slot *slot = &fake_module_slots[index];
        if (slot->module == NULL && slot->generation != UINT32_MAX) {
            ++slot->generation;
            slot->module = module;
            return index;
        }
    }
    return FAKE_MODULE_SLOT_COUNT;
}

static int32_t fake_engine_open(uint64_t *output) {
    fake_engine *engine;
    if (output == NULL) return FDN_WAMR_INVALID_REQUEST;
    engine = calloc(1, sizeof(*engine));
    if (engine == NULL) return FDN_WAMR_HANDLER_ERROR;
    fake_open_lock_acquire();
    engine->active = 1;
    ++fake_live_engines;
    fake_open_lock_release();
    *output = (uint64_t)(uintptr_t)engine;
    return FDN_WAMR_OK;
}

static int32_t fake_engine_close(uint64_t *value) {
    fake_engine *engine;
    if (value == NULL || *value == 0) return FDN_WAMR_OK;
    engine = (fake_engine *)(uintptr_t)*value;
    fake_open_lock_acquire();
    if (engine->modules != 0) {
        fake_open_lock_release();
        return FDN_WAMR_CLOSED;
    }
    engine->active = 0;
    if (fake_live_engines == 0) abort();
    --fake_live_engines;
    fake_open_broadcast();
    fake_open_lock_release();
    free(engine);
    *value = 0;
    return FDN_WAMR_OK;
}

static int32_t fake_module_open(uint64_t engine_value, const fdn_string *path,
                                uint64_t payload_limit, uint64_t *output) {
    fake_engine *engine = (fake_engine *)(uintptr_t)engine_value;
    fake_module *module;
    size_t slot;
    fake_open_barrier();
    if (output == NULL || engine == NULL || path == NULL || path->length == 0 ||
        payload_limit == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    module = calloc(1, sizeof(*module));
    if (module == NULL) return FDN_WAMR_HANDLER_ERROR;
    module->engine = engine;
    fake_open_lock_acquire();
    if (!engine->active) {
        fake_open_lock_release();
        free(module);
        return FDN_WAMR_CLOSED;
    }
    slot = fake_module_slot_assign(module);
    if (slot == FAKE_MODULE_SLOT_COUNT) {
        fake_open_lock_release();
        free(module);
        return FDN_WAMR_HANDLER_ERROR;
    }
    ++engine->modules;
    ++fake_live_modules;
    *output = fake_module_reference(slot);
    fake_open_lock_release();
    return FDN_WAMR_OK;
}

static int32_t fake_allow_read(uint64_t value, const fdn_string *path) {
    if (fake_module_value(value) == NULL) return FDN_WAMR_CLOSED;
    (void)path;
    return FDN_WAMR_READ_ONLY_UNSUPPORTED;
}

static int32_t fake_allow_write(uint64_t value, const fdn_string *path) {
    fake_module *module = fake_module_value(value);
    if (module == NULL || path == NULL || path->length == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    module->write_path = 1;
    return FDN_WAMR_OK;
}

static int32_t fake_set_environment(uint64_t value, const fdn_string *key,
                                    const fdn_string *entry_value) {
    fake_module *module = fake_module_value(value);
    if (module == NULL || key == NULL || entry_value == NULL || key->length == 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    module->environment = 1;
    return FDN_WAMR_OK;
}

static int32_t fake_add_argument(uint64_t value, const fdn_string *argument) {
    fake_module *module = fake_module_value(value);
    if (module == NULL || argument == NULL) return FDN_WAMR_INVALID_REQUEST;
    module->argument = 1;
    return FDN_WAMR_OK;
}

static int32_t fake_set_host_dispatch(uint64_t value, void *context,
                                      fdn_wamr_host_dispatch_fn dispatch) {
    fake_module *module = fake_module_value(value);
    if (module == NULL || dispatch == NULL) return FDN_WAMR_INVALID_REQUEST;
    module->host_context = context;
    module->host_dispatch = dispatch;
    return FDN_WAMR_OK;
}

static int32_t fake_prepare(uint64_t value) {
    fake_module *module = fake_module_value(value);
    if (module == NULL) return FDN_WAMR_INVALID_REQUEST;
    module->prepared = 1;
    return FDN_WAMR_OK;
}

static int32_t fake_metadata(uint64_t value, uint8_t **metadata,
                             size_t *metadata_length) {
    static const uint8_t text[] =
        "{\"name\":\"fixture\",\"version\":\"1.0.0\","
        "\"description\":\"Foundation WAMR fixture\","
        "\"methods\":[\"echo\",\"capability\",\"undeclared\",\"bad-range\",\"error\","
        "\"last-error-probe\",\"alias-input\",\"alias-method\",\"hang\"],"
        "\"capabilities\":[\"test.echo\"],"
        "\"properties\":{\"language\":\"c\"}}";
    uint8_t *copy;
    uint8_t *response = NULL;
    uint8_t *error = NULL;
    size_t response_length = 0;
    size_t error_length = 0;
    const fdn_string capability = {"test.echo", 9, 0};
    const fdn_string operation = {"metadata", 8, 0};
    fake_module *module = fake_module_value(value);
    if (module == NULL || metadata == NULL || metadata_length == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (module->host_dispatch == NULL ||
        module->host_dispatch(module->host_context, &capability, &operation,
                              NULL, 0, &response, &response_length, &error,
                              &error_length) != FDN_WASM_HOST_DENIED) {
        free(response);
        free(error);
        return FDN_WAMR_HANDLER_ERROR;
    }
    free(response);
    free(error);
    copy = malloc(sizeof(text) - 1);
    if (copy == NULL) return FDN_WAMR_HANDLER_ERROR;
    (void)memcpy(copy, text, sizeof(text) - 1);
    *metadata = copy;
    *metadata_length = sizeof(text) - 1;
    return FDN_WAMR_OK;
}

static int32_t fake_module_start(uint64_t value) {
    fake_module *module = fake_module_value(value);
    if (module == NULL || !module->prepared || !module->write_path ||
        !module->environment || !module->argument) {
        return FDN_WAMR_DENIED;
    }
    atomic_store(&module->started, 1);
    return FDN_WAMR_OK;
}

static int32_t fake_module_stop(uint64_t value) {
    fake_module *module = fake_module_value(value);
    if (module == NULL) return FDN_WAMR_INVALID_REQUEST;
    if (atomic_load(&module->cancelled)) return FDN_WAMR_CLOSED;
    atomic_store(&module->started, 0);
    return FDN_WAMR_OK;
}

static int32_t fake_module_cancel(uint64_t value) {
    fake_module *module = fake_module_value(value);
    if (module == NULL) return FDN_WAMR_INVALID_REQUEST;
    atomic_store(&module->cancelled, 1);
    atomic_store(&module->started, 0);
    return FDN_WAMR_OK;
}

static int32_t fake_module_call(uint64_t value, const fdn_string *method,
                                const uint8_t *input, size_t input_length,
                                uint8_t **output, size_t *output_length) {
    static const char denied[] = "denied";
    const fdn_string declared_capability = {"test.echo", 9, 0};
    const fdn_string undeclared_capability = {"test.admin", 10, 0};
    const fdn_string operation = {"invoke", 6, 0};
    fake_module *module = fake_module_value(value);
    uint8_t *copy;
    int32_t status;
    uint8_t *response = NULL;
    uint8_t *error = NULL;
    size_t response_length = 0;
    size_t error_length = 0;
    if (module == NULL || method == NULL ||
        (input_length != 0 && input == NULL) ||
        output == NULL ||
        output_length == NULL) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    if (!atomic_load(&module->started) || atomic_load(&module->cancelled)) {
        return FDN_WAMR_CLOSED;
    }
    if (memcmp(method->data, "hang", 4) == 0) {
        while (!atomic_load(&module->cancelled)) {
        }
        return FDN_WAMR_CLOSED;
    }
    if (method->length == 10 && memcmp(method->data, "capability", 10) == 0) {
        status = module->host_dispatch(module->host_context, &declared_capability,
                                       &operation, input, input_length, &response,
                                       &response_length, &error, &error_length);
        free(error);
        if (status != FDN_WASM_HOST_OK) {
            free(response);
            return FDN_WAMR_HANDLER_ERROR;
        }
        *output = response;
        *output_length = response_length;
        return FDN_WAMR_OK;
    }
    if (method->length == 10 && memcmp(method->data, "undeclared", 10) == 0) {
        status = module->host_dispatch(module->host_context, &undeclared_capability,
                                       &operation, input, input_length, &response,
                                       &response_length, &error, &error_length);
        free(response);
        free(error);
        if (status != FDN_WASM_HOST_DENIED) return FDN_WAMR_HANDLER_ERROR;
        copy = malloc(sizeof(denied) - 1);
        if (copy == NULL) return FDN_WAMR_HANDLER_ERROR;
        (void)memcpy(copy, denied, sizeof(denied) - 1);
        *output = copy;
        *output_length = sizeof(denied) - 1;
        return FDN_WAMR_OK;
    }
    if (method->length != 4 || memcmp(method->data, "echo", 4) != 0) {
        return FDN_WAMR_INVALID_REQUEST;
    }
    copy = malloc(input_length == 0 ? 1 : input_length);
    if (copy == NULL) return FDN_WAMR_HANDLER_ERROR;
    if (input_length != 0) (void)memcpy(copy, input, input_length);
    *output = copy;
    *output_length = input_length;
    return FDN_WAMR_OK;
}

static void fake_free_output(uint64_t value, uint8_t *output, size_t output_length) {
    (void)value;
    (void)output_length;
    free(output);
}

static int32_t fake_module_close(uint64_t *value) {
    fake_module *module;
    fake_slot *slot;
    if (value == NULL || *value == 0) return FDN_WAMR_OK;
    fake_open_lock_acquire();
    slot = fake_module_slot_find(*value);
    module = slot == NULL ? NULL : slot->module;
    if (module == NULL) {
        fake_open_lock_release();
        *value = 0;
        return FDN_WAMR_CLOSED;
    }
    if (module->engine == NULL || module->engine->modules == 0) {
        fake_open_lock_release();
        return FDN_WAMR_HANDLER_ERROR;
    }
    --module->engine->modules;
    slot->module = NULL;
    if (fake_live_modules == 0) abort();
    --fake_live_modules;
    fake_open_broadcast();
    fake_open_lock_release();
    free(module);
    *value = 0;
    return FDN_WAMR_OK;
}

FDN_WAMR_PROVIDER_EXPORT const fdn_wamr_provider_v2 *foundation_wamr_provider_query(void) {
    static const fdn_wamr_provider_v2 provider = {
        FDN_WAMR_PROVIDER_ABI_MAJOR,
        FDN_WAMR_PROVIDER_ABI_MINOR,
        sizeof(fdn_wamr_provider_v2),
        fake_engine_open,
        fake_engine_close,
        fake_module_open,
        fake_allow_read,
        fake_allow_write,
        fake_set_environment,
        fake_add_argument,
        fake_set_host_dispatch,
        fake_prepare,
        fake_metadata,
        fake_module_start,
        fake_module_stop,
        fake_module_cancel,
        fake_module_call,
        fake_free_output,
        fake_module_close,
    };
    return &provider;
}
