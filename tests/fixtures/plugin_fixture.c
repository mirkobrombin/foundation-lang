#define FDN_PLUGIN_BUILD 1
#include "foundation/plugin.h"

#include <string.h>

typedef struct fixture_state {
    fdn_plugin_deallocate_v1 deallocate;
    int started;
} fixture_state;

static fdn_plugin_text_v1 fixture_text(const char *value) {
    const fdn_plugin_text_v1 result = {value, strlen(value)};
    return result;
}

static int32_t FDN_PLUGIN_CALL fixture_create(
    const fdn_plugin_host_v1 *host,
    void **context,
    fdn_plugin_text_v1 *error) {
#if defined(FDN_PLUGIN_FIXTURE_CREATE_FAIL)
    (void)host;
    (void)context;
    *error = fixture_text("fixture creation rejected");
    return 1;
#else
    fixture_state *state;
    if (host == NULL || context == NULL || error == NULL ||
        host->struct_size < sizeof(*host) || host->allocate == NULL ||
        host->deallocate == NULL) {
        return 1;
    }
    state = host->allocate(sizeof(*state));
    state->deallocate = host->deallocate;
    state->started = 0;
    *context = state;
    *error = fixture_text("");
    return 0;
#endif
}

static int32_t FDN_PLUGIN_CALL fixture_start(void *context,
                                             fdn_plugin_text_v1 *error) {
#if defined(FDN_PLUGIN_FIXTURE_START_FAIL)
    (void)context;
    *error = fixture_text("fixture start rejected");
    return 1;
#else
    fixture_state *state = context;
    state->started = 1;
    *error = fixture_text("");
    return 0;
#endif
}

static int32_t FDN_PLUGIN_CALL fixture_stop(void *context,
                                            fdn_plugin_text_v1 *error) {
    fixture_state *state = context;
    state->started = 0;
    *error = fixture_text("");
    return 0;
}

static void FDN_PLUGIN_CALL fixture_destroy(void *context) {
    fixture_state *state = context;
    if (state == NULL) {
        return;
    }
    state->deallocate(state);
}

FDN_PLUGIN_EXPORT int32_t FDN_PLUGIN_CALL foundation_plugin_query_v1(
    const fdn_plugin_host_v1 *host,
    fdn_plugin_descriptor_v1 *descriptor,
    fdn_plugin_text_v1 *error) {
    if (host == NULL || descriptor == NULL || error == NULL ||
        host->struct_size < sizeof(*host) || host->allocate == NULL ||
        host->deallocate == NULL) {
        return 1;
    }
#if defined(FDN_PLUGIN_FIXTURE_QUERY_FAIL)
    *error = fixture_text("fixture query rejected");
    return 1;
#endif
    (void)memset(descriptor, 0, sizeof(*descriptor));
    descriptor->struct_size = (uint32_t)sizeof(*descriptor);
#if defined(FDN_PLUGIN_FIXTURE_ABI_MISMATCH)
    descriptor->abi_major = FDN_PLUGIN_ABI_MAJOR + 1;
#else
    descriptor->abi_major = FDN_PLUGIN_ABI_MAJOR;
#endif
    descriptor->abi_minor = FDN_PLUGIN_ABI_MINOR;
    descriptor->sdk_major = FDN_PLUGIN_SDK_MAJOR;
    descriptor->sdk_minor = FDN_PLUGIN_SDK_MINOR;
#if defined(FDN_PLUGIN_FIXTURE_SDK_MISMATCH)
    descriptor->sdk_minor += 1;
#endif
    descriptor->contract_hash = FDN_PLUGIN_LIFECYCLE_CONTRACT_HASH;
#if defined(FDN_PLUGIN_FIXTURE_CONTRACT_MISMATCH)
    descriptor->contract_hash += 1;
#endif
#if defined(FDN_PLUGIN_FIXTURE_TARGET_MISMATCH)
    descriptor->target_os = fixture_text("other");
#else
    descriptor->target_os = fixture_text(FDN_PLUGIN_TARGET_OS);
#endif
    descriptor->target_arch = fixture_text(FDN_PLUGIN_TARGET_ARCH);
#if defined(FDN_PLUGIN_FIXTURE_START_FAIL)
    descriptor->name = fixture_text("failing-native");
#else
    descriptor->name = fixture_text("sample-native");
#endif
    descriptor->create = fixture_create;
#if defined(FDN_PLUGIN_FIXTURE_INVALID_DESCRIPTOR)
    descriptor->create = NULL;
#endif
    descriptor->start = fixture_start;
    descriptor->stop = fixture_stop;
    descriptor->destroy = fixture_destroy;
    *error = fixture_text("");
    return 0;
}
