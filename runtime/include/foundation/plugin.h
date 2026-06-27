#ifndef FOUNDATION_PLUGIN_H
#define FOUNDATION_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#define FDN_PLUGIN_ABI_MAJOR UINT32_C(1)
#define FDN_PLUGIN_ABI_MINOR UINT32_C(0)
#define FDN_PLUGIN_SDK_MAJOR UINT32_C(0)
#define FDN_PLUGIN_SDK_MINOR UINT32_C(1)
#define FDN_PLUGIN_LIFECYCLE_CONTRACT_HASH UINT64_C(0x2b208733b3b44e02)
#define FDN_PLUGIN_QUERY_SYMBOL "foundation_plugin_query_v1"

#if defined(_WIN32)
#define FDN_PLUGIN_TARGET_OS "windows"
#elif defined(__APPLE__)
#define FDN_PLUGIN_TARGET_OS "macos"
#elif defined(__linux__)
#define FDN_PLUGIN_TARGET_OS "linux"
#else
#define FDN_PLUGIN_TARGET_OS "unknown"
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define FDN_PLUGIN_TARGET_ARCH "x86_64"
#elif defined(_M_ARM64) || defined(__aarch64__)
#define FDN_PLUGIN_TARGET_ARCH "aarch64"
#else
#define FDN_PLUGIN_TARGET_ARCH "unknown"
#endif

#if defined(_WIN32)
#define FDN_PLUGIN_CALL __cdecl
#if defined(FDN_PLUGIN_BUILD)
#define FDN_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FDN_PLUGIN_EXPORT
#endif
#else
#define FDN_PLUGIN_CALL
#if defined(FDN_PLUGIN_BUILD)
#define FDN_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FDN_PLUGIN_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fdn_plugin_text_v1 {
    const char *data;
    size_t length;
} fdn_plugin_text_v1;

typedef void *(FDN_PLUGIN_CALL *fdn_plugin_allocate_v1)(size_t size);
typedef void(FDN_PLUGIN_CALL *fdn_plugin_deallocate_v1)(void *value);
struct fdn_plugin_host_v1;
typedef int32_t(FDN_PLUGIN_CALL *fdn_plugin_create_v1)(
    const struct fdn_plugin_host_v1 *host,
    void **context,
    fdn_plugin_text_v1 *error);
typedef int32_t(FDN_PLUGIN_CALL *fdn_plugin_lifecycle_v1)(
    void *context, fdn_plugin_text_v1 *error);
typedef void(FDN_PLUGIN_CALL *fdn_plugin_destroy_v1)(void *context);

typedef struct fdn_plugin_host_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t sdk_major;
    uint32_t sdk_minor;
    fdn_plugin_text_v1 target_os;
    fdn_plugin_text_v1 target_arch;
    fdn_plugin_allocate_v1 allocate;
    fdn_plugin_deallocate_v1 deallocate;
} fdn_plugin_host_v1;

typedef struct fdn_plugin_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t sdk_major;
    uint32_t sdk_minor;
    uint64_t contract_hash;
    fdn_plugin_text_v1 target_os;
    fdn_plugin_text_v1 target_arch;
    fdn_plugin_text_v1 name;
    fdn_plugin_create_v1 create;
    fdn_plugin_lifecycle_v1 start;
    fdn_plugin_lifecycle_v1 stop;
    fdn_plugin_destroy_v1 destroy;
} fdn_plugin_descriptor_v1;

/*
 * Query fills a descriptor without retaining host or creating persistent state.
 * The host validates the complete descriptor before calling create. A successful
 * create returns one non-null context owned by the host until destroy. Error text
 * is borrowed only for the duration of the callback that produced it.
 */
typedef int32_t(FDN_PLUGIN_CALL *fdn_plugin_query_v1_fn)(
    const fdn_plugin_host_v1 *host,
    fdn_plugin_descriptor_v1 *descriptor,
    fdn_plugin_text_v1 *error);

#ifdef __cplusplus
}
#endif

#endif
