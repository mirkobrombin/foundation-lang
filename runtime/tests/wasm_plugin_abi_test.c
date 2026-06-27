#include "foundation/wasm_plugin.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    const uint64_t version = fdn_wasm_pack_version(FDN_WASM_ABI_MAJOR,
                                                   FDN_WASM_ABI_MINOR);
    const uint64_t buffer = fdn_wasm_pack_buffer(UINT32_C(0x12345678),
                                                 UINT32_C(0x9abcdef0));

    if (strcmp(FDN_WASM_ABI_NAME, "foundation:plugin") != 0 ||
        version != UINT64_C(0x0000000100000000)) {
        return 1;
    }
    if (fdn_wasm_buffer_pointer(buffer) != UINT32_C(0x12345678) ||
        fdn_wasm_buffer_length(buffer) != UINT32_C(0x9abcdef0)) {
        return 2;
    }
    if (FDN_WASM_HOST_OK != 0 || FDN_WASM_HOST_DENIED != 1 ||
        FDN_WASM_HOST_INVALID_REQUEST != 2 ||
        FDN_WASM_HOST_HANDLER_ERROR != 3 ||
        FDN_WASM_HOST_PAYLOAD_TOO_LARGE != 4) {
        return 3;
    }
    return 0;
}
