#ifndef FOUNDATION_WASM_PLUGIN_H
#define FOUNDATION_WASM_PLUGIN_H

#include <stdint.h>

#define FDN_WASM_ABI_NAME "foundation:plugin"
#define FDN_WASM_ABI_MAJOR UINT32_C(1)
#define FDN_WASM_ABI_MINOR UINT32_C(0)
#define FDN_WASM_HOST_MODULE "foundation"
#define FDN_WASM_ERROR_RESULT UINT64_MAX

#define FDN_WASM_EXPORT_ABI_VERSION "foundation_abi_version"
#define FDN_WASM_EXPORT_ALLOCATE "foundation_alloc"
#define FDN_WASM_EXPORT_FREE "foundation_free"
#define FDN_WASM_EXPORT_METADATA "foundation_metadata"
#define FDN_WASM_EXPORT_START "foundation_start"
#define FDN_WASM_EXPORT_STOP "foundation_stop"
#define FDN_WASM_EXPORT_CALL "foundation_call"
#define FDN_WASM_EXPORT_LAST_ERROR "foundation_last_error"
#define FDN_WASM_EXPORT_MEMORY "memory"

#define FDN_WASM_IMPORT_HOST_CALL "host_call"
#define FDN_WASM_IMPORT_RESPONSE_LENGTH "host_response_len"
#define FDN_WASM_IMPORT_RESPONSE_READ "host_response_read"
#define FDN_WASM_IMPORT_ERROR_LENGTH "host_error_len"
#define FDN_WASM_IMPORT_ERROR_READ "host_error_read"

#if defined(__clang__) && (defined(__wasm__) || defined(__wasm32__))
#define FDN_WASM_EXPORT(name) __attribute__((export_name(name)))
#define FDN_WASM_IMPORT(module, name)                                      \
    __attribute__((import_module(module), import_name(name)))
#else
#define FDN_WASM_EXPORT(name)
#define FDN_WASM_IMPORT(module, name)
#endif

typedef enum fdn_wasm_host_status {
    FDN_WASM_HOST_OK = 0,
    FDN_WASM_HOST_DENIED = 1,
    FDN_WASM_HOST_INVALID_REQUEST = 2,
    FDN_WASM_HOST_HANDLER_ERROR = 3,
    FDN_WASM_HOST_PAYLOAD_TOO_LARGE = 4,
} fdn_wasm_host_status;

#if defined(__clang__) && (defined(__wasm__) || defined(__wasm32__))
FDN_WASM_IMPORT(FDN_WASM_HOST_MODULE, FDN_WASM_IMPORT_HOST_CALL)
uint32_t foundation_host_call(uint32_t capability_pointer,
                              uint32_t capability_length,
                              uint32_t operation_pointer,
                              uint32_t operation_length,
                              uint32_t input_pointer,
                              uint32_t input_length);

FDN_WASM_IMPORT(FDN_WASM_HOST_MODULE, FDN_WASM_IMPORT_RESPONSE_LENGTH)
uint32_t foundation_host_response_len(void);

FDN_WASM_IMPORT(FDN_WASM_HOST_MODULE, FDN_WASM_IMPORT_RESPONSE_READ)
int32_t foundation_host_response_read(uint32_t pointer, uint32_t capacity);

FDN_WASM_IMPORT(FDN_WASM_HOST_MODULE, FDN_WASM_IMPORT_ERROR_LENGTH)
uint32_t foundation_host_error_len(void);

FDN_WASM_IMPORT(FDN_WASM_HOST_MODULE, FDN_WASM_IMPORT_ERROR_READ)
int32_t foundation_host_error_read(uint32_t pointer, uint32_t capacity);
#endif

static inline uint64_t fdn_wasm_pack_version(uint32_t major, uint32_t minor) {
    return ((uint64_t)major << 32) | (uint64_t)minor;
}

static inline uint64_t fdn_wasm_pack_buffer(uint32_t pointer, uint32_t length) {
    return ((uint64_t)pointer << 32) | (uint64_t)length;
}

static inline uint32_t fdn_wasm_buffer_pointer(uint64_t value) {
    return (uint32_t)(value >> 32);
}

static inline uint32_t fdn_wasm_buffer_length(uint64_t value) {
    return (uint32_t)value;
}

/*
 * Guests export linear memory with FDN_WASM_EXPORT_MEMORY and use the function
 * names above. Metadata, call responses, and errors return packed
 * pointer-length pairs. Metadata and last-error ranges are borrowed until the
 * next guest call. A successful call response is owned and the host releases it
 * with foundation_free. Host imports borrow guest memory only for the duration
 * of a call. Responses are copied through the length/read imports. A host read
 * returns the number of bytes copied, or a negative required size when capacity
 * is insufficient. Neither side retains a pointer owned by the other.
 */

#endif
