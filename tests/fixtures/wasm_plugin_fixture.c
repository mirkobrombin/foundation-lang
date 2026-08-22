#include "foundation/wasm_plugin.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char metadata[] =
#if defined(FDN_WASM_FIXTURE_INVALID_METADATA)
    "{\"name\":";
#else
    "{\"name\":\"fixture\",\"version\":\"1.0.0\","
    "\"description\":\"Foundation WAMR fixture\","
    "\"methods\":[\"echo\",\"capability\",\"bad-range\",\"error\","
    "\"undeclared\",\"last-error-probe\",\"alias-input\",\"alias-method\",\"hang\"],"
    "\"capabilities\":[\"test.echo\"],"
    "\"properties\":{\"language\":\"c\"}}";
#endif
static uint8_t heap[65536];
static uint32_t heap_used;
static volatile uint32_t spin;
static uint32_t last_error_reads;
static char last_error[256] = "no error";

static uint32_t text_length(const char *value) {
    uint32_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length) {
    uint32_t index;
    for (index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
}

static bool method_is(uint32_t pointer, uint32_t length,
                      const char *expected) {
    const uint8_t *method = (const uint8_t *)(uintptr_t)pointer;
    uint32_t index;
    if (length != text_length(expected)) {
        return false;
    }
    for (index = 0; index < length; ++index) {
        if (method[index] != (uint8_t)expected[index]) {
            return false;
        }
    }
    return true;
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_ABI_VERSION)
#if defined(FDN_WASM_FIXTURE_BAD_SIGNATURE)
uint64_t foundation_abi_version(uint32_t unexpected) {
    (void)unexpected;
#else
uint64_t foundation_abi_version(void) {
#endif
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
    uint32_t output;
    if (method_is(method_pointer, method_length, "hang")) {
        for (;;) {
            ++spin;
        }
    }
    if (method_is(method_pointer, method_length, "capability")) {
        const uint32_t status = foundation_host_call(
            (uint32_t)(uintptr_t)"test.echo", UINT32_C(9),
            (uint32_t)(uintptr_t)"invoke", UINT32_C(6), input_pointer,
            input_length);
        uint32_t length;
        if (status != FDN_WASM_HOST_OK) {
            length = foundation_host_error_len();
            if (length > (uint32_t)sizeof(last_error)) {
                length = (uint32_t)sizeof(last_error);
            }
            (void)foundation_host_error_read(
                (uint32_t)(uintptr_t)last_error, length);
            return FDN_WASM_ERROR_RESULT;
        }
        length = foundation_host_response_len();
        output = foundation_alloc(length);
        if (length > 0 && output == 0) {
            return FDN_WASM_ERROR_RESULT;
        }
        if (foundation_host_response_read(output, length) < 0) {
            return FDN_WASM_ERROR_RESULT;
        }
        return fdn_wasm_pack_buffer(output, length);
    }
    if (method_is(method_pointer, method_length, "undeclared")) {
        static const char value[] = "denied";
        const uint32_t status = foundation_host_call(
            (uint32_t)(uintptr_t)"test.admin", UINT32_C(10),
            (uint32_t)(uintptr_t)"invoke", UINT32_C(6), input_pointer,
            input_length);
        if (status != FDN_WASM_HOST_DENIED) {
            return FDN_WASM_ERROR_RESULT;
        }
        output = foundation_alloc((uint32_t)(sizeof(value) - 1));
        if (output == 0) {
            return FDN_WASM_ERROR_RESULT;
        }
        copy_bytes((uint8_t *)(uintptr_t)output, (const uint8_t *)value,
                   (uint32_t)(sizeof(value) - 1));
        return fdn_wasm_pack_buffer(output, (uint32_t)(sizeof(value) - 1));
    }
    if (method_is(method_pointer, method_length, "bad-range")) {
        return fdn_wasm_pack_buffer(UINT32_MAX - UINT32_C(8), UINT32_C(32));
    }
    if (method_is(method_pointer, method_length, "error")) {
        return FDN_WASM_ERROR_RESULT;
    }
    if (method_is(method_pointer, method_length, "last-error-probe")) {
        const char value[] = "read";
        if (last_error_reads == 0) {
            return FDN_WASM_ERROR_RESULT;
        }
        output = foundation_alloc((uint32_t)(sizeof(value) - 1));
        if (output == 0) {
            return FDN_WASM_ERROR_RESULT;
        }
        copy_bytes((uint8_t *)(uintptr_t)output, (const uint8_t *)value,
                   (uint32_t)(sizeof(value) - 1));
        return fdn_wasm_pack_buffer(output, (uint32_t)(sizeof(value) - 1));
    }
    if (method_is(method_pointer, method_length, "alias-input")) {
        return fdn_wasm_pack_buffer(input_pointer, input_length);
    }
    if (method_is(method_pointer, method_length, "alias-method")) {
        return fdn_wasm_pack_buffer(method_pointer, method_length);
    }
    if (!method_is(method_pointer, method_length, "echo")) {
        return FDN_WASM_ERROR_RESULT;
    }
    output = foundation_alloc(input_length);
    if (input_length > 0 && output == 0) {
        return FDN_WASM_ERROR_RESULT;
    }
    copy_bytes((uint8_t *)(uintptr_t)output,
               (const uint8_t *)(uintptr_t)input_pointer, input_length);
    return fdn_wasm_pack_buffer(output, input_length);
}

FDN_WASM_EXPORT(FDN_WASM_EXPORT_LAST_ERROR)
uint64_t foundation_last_error(void) {
    ++last_error_reads;
    return fdn_wasm_pack_buffer((uint32_t)(uintptr_t)last_error,
                                text_length(last_error));
}
