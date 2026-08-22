#ifndef FOUNDATION_WAMR_PROVIDER_H
#define FOUNDATION_WAMR_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#include "foundation/runtime.h"

#if defined(FOUNDATION_WAMR_PROVIDER_BUILD) && defined(_WIN32)
#define FDN_WAMR_PROVIDER_EXPORT __declspec(dllexport)
#elif defined(FOUNDATION_WAMR_PROVIDER_BUILD) && \
    (defined(__GNUC__) || defined(__clang__))
#define FDN_WAMR_PROVIDER_EXPORT __attribute__((visibility("default")))
#else
#define FDN_WAMR_PROVIDER_EXPORT
#endif

#define FDN_WAMR_PROVIDER_ABI_MAJOR UINT32_C(2)
#define FDN_WAMR_PROVIDER_ABI_MINOR UINT32_C(0)
#define FDN_WAMR_PROVIDER_QUERY "foundation_wamr_provider_query"

enum fdn_wamr_status {
    FDN_WAMR_OK = 0,
    FDN_WAMR_DENIED = 1,
    FDN_WAMR_INVALID_REQUEST = 2,
    FDN_WAMR_HANDLER_ERROR = 3,
    FDN_WAMR_PAYLOAD_TOO_LARGE = 4,
    FDN_WAMR_CLOSED = 5,
    FDN_WAMR_READ_ONLY_UNSUPPORTED = 6,
};

typedef struct fdn_wamr_provider_v2 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    int32_t (*engine_open)(uint64_t *engine);
    int32_t (*engine_close)(uint64_t *engine);
    int32_t (*module_open)(uint64_t engine, const fdn_string *path,
                           uint64_t payload_limit, uint64_t *module);
    int32_t (*module_allow_read)(uint64_t module, const fdn_string *path);
    int32_t (*module_allow_write)(uint64_t module, const fdn_string *path);
    int32_t (*module_set_environment)(uint64_t module, const fdn_string *key,
                                      const fdn_string *value);
    int32_t (*module_add_argument)(uint64_t module, const fdn_string *value);
    int32_t (*module_set_host_dispatch)(uint64_t module, void *context,
                                        fdn_wamr_host_dispatch_fn dispatch);
    int32_t (*module_prepare)(uint64_t module);
    int32_t (*module_metadata)(uint64_t module, uint8_t **metadata,
                               size_t *metadata_length);
    int32_t (*module_start)(uint64_t module);
    int32_t (*module_stop)(uint64_t module);
    int32_t (*module_cancel)(uint64_t module);
    int32_t (*module_call)(uint64_t module, const fdn_string *method,
                           const uint8_t *input, size_t input_length,
                           uint8_t **output, size_t *output_length);
    void (*free_output)(uint64_t module, uint8_t *output, size_t output_length);
    int32_t (*module_close)(uint64_t *module);
} fdn_wamr_provider_v2;

/*
 * Close is a consuming, infallible operation after the caller has quiesced the
 * handle and closed its dependants. A conforming provider returns FDN_WAMR_OK
 * and writes zero. Returning another status or retaining the handle is an ABI
 * contract violation because Foundation may finalize a timed-out operation
 * after the source handle is no longer reachable.
 */

#define FDN_WAMR_PROVIDER_V2_SIZE ((uint32_t)sizeof(fdn_wamr_provider_v2))

typedef const fdn_wamr_provider_v2 *(*fdn_wamr_provider_query_fn)(void);

FDN_WAMR_PROVIDER_EXPORT const fdn_wamr_provider_v2 *foundation_wamr_provider_query(void);

#endif
