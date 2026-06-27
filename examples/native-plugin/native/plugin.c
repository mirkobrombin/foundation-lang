#define FDN_PLUGIN_BUILD 1
#include "foundation/plugin.h"

#include <string.h>

typedef struct greeter_state {
    fdn_plugin_deallocate_v1 deallocate;
    int started;
} greeter_state;

static fdn_plugin_text_v1 greeter_text(const char *value) {
    const fdn_plugin_text_v1 result = {value, strlen(value)};
    return result;
}

static int32_t FDN_PLUGIN_CALL greeter_create(
    const fdn_plugin_host_v1 *host,
    void **context,
    fdn_plugin_text_v1 *error) {
    greeter_state *state;
    if (host == NULL || context == NULL || error == NULL ||
        host->allocate == NULL || host->deallocate == NULL) {
        return 1;
    }
    state = host->allocate(sizeof(*state));
    state->deallocate = host->deallocate;
    state->started = 0;
    *context = state;
    *error = greeter_text("");
    return 0;
}

static int32_t FDN_PLUGIN_CALL greeter_start(
    void *context,
    fdn_plugin_text_v1 *error) {
    greeter_state *state = context;
    state->started = 1;
    *error = greeter_text("");
    return 0;
}

static int32_t FDN_PLUGIN_CALL greeter_stop(
    void *context,
    fdn_plugin_text_v1 *error) {
    greeter_state *state = context;
    state->started = 0;
    *error = greeter_text("");
    return 0;
}

static void FDN_PLUGIN_CALL greeter_destroy(void *context) {
    greeter_state *state = context;
    state->deallocate(state);
}

FDN_PLUGIN_EXPORT int32_t FDN_PLUGIN_CALL foundation_plugin_query_v1(
    const fdn_plugin_host_v1 *host,
    fdn_plugin_descriptor_v1 *descriptor,
    fdn_plugin_text_v1 *error) {
    if (host == NULL || descriptor == NULL || error == NULL ||
        host->struct_size < sizeof(*host)) {
        return 1;
    }
    (void)memset(descriptor, 0, sizeof(*descriptor));
    descriptor->struct_size = (uint32_t)sizeof(*descriptor);
    descriptor->abi_major = FDN_PLUGIN_ABI_MAJOR;
    descriptor->abi_minor = FDN_PLUGIN_ABI_MINOR;
    descriptor->sdk_major = FDN_PLUGIN_SDK_MAJOR;
    descriptor->sdk_minor = FDN_PLUGIN_SDK_MINOR;
    descriptor->contract_hash = FDN_PLUGIN_LIFECYCLE_CONTRACT_HASH;
    descriptor->target_os = greeter_text(FDN_PLUGIN_TARGET_OS);
    descriptor->target_arch = greeter_text(FDN_PLUGIN_TARGET_ARCH);
    descriptor->name = greeter_text("greeter-native");
    descriptor->create = greeter_create;
    descriptor->start = greeter_start;
    descriptor->stop = greeter_stop;
    descriptor->destroy = greeter_destroy;
    *error = greeter_text("");
    return 0;
}
