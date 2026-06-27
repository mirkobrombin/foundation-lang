#include "foundation/wasm_plugin.h"

#include <stdint.h>

static const char metadata[] =
    "{\"name\":\"fixture\",\"version\":\"1.0.0\",\"methods\":[\"echo\"]}";
static uint8_t heap[65536];
static uint32_t heap_used;

FDN_WASM_EXPORT(FDN_WASM_EXPORT_ABI_VERSION)
uint64_t foundation_abi_version(void) {
    return fdn_wasm_pack_version(FDN_WASM_ABI_MAJOR, FDN_WASM_ABI_MINOR);
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_ALLOCATE)
uint32_t foundation_alloc(uint32_t length) {
    uint32_t pointer;
    if (length == 0 || length > (uint32_t)sizeof(heap) - heap_used) {
        return 0;
    }
    pointer = (uint32_t)(uintptr_t)&heap[heap_used];
    heap_used += length;
    return pointer;
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_FREE)
void foundation_free(uint32_t pointer, uint32_t length) {
    (void)pointer;
    (void)length;
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_METADATA)
uint64_t foundation_metadata(void) {
    return fdn_wasm_pack_buffer((uint32_t)(uintptr_t)metadata,
                                (uint32_t)(sizeof(metadata) - 1));
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_START)
uint32_t foundation_start(void) {
    return 0;
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_STOP)
uint32_t foundation_stop(void) {
    return 0;
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_CALL)
uint64_t foundation_call(uint32_t method_pointer, uint32_t method_length,
                         uint32_t input_pointer, uint32_t input_length) {
    (void)method_pointer;
    (void)method_length;
    return fdn_wasm_pack_buffer(input_pointer, input_length);
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_LAST_ERROR)
uint64_t foundation_last_error(void) {
    return 0;
}
