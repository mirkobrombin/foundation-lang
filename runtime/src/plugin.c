#include "foundation/plugin.h"
#include "foundation/runtime.h"

#include <limits.h>
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
#include <stdatomic.h>
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
    if (descriptor->sdk_major != host->sdk_major ||
        descriptor->sdk_minor != host->sdk_minor) {
        fdn_plugin_set_detail(detail, fdn_plugin_text("plugin SDK mismatch"),
                              "plugin SDK mismatch");
        return FDN_PLUGIN_SDK_MISMATCH;
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
