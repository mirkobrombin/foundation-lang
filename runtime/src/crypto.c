#include "foundation/runtime.h"

#include <limits.h>
#include <string.h>

typedef struct fdn_bytes {
    uint8_t *data;
    size_t length;
} fdn_bytes;

typedef struct fdn_sha256 {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t block_length;
} fdn_sha256;

static const uint32_t fdn_sha256_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};

static uint32_t fdn_rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

static uint32_t fdn_load_u32_be(const uint8_t *value) {
    return ((uint32_t)value[0] << 24U) | ((uint32_t)value[1] << 16U) |
           ((uint32_t)value[2] << 8U) | (uint32_t)value[3];
}

static void fdn_store_u32_be(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void fdn_secure_zero(void *value, size_t length) {
    volatile uint8_t *bytes = value;
    while (length != 0) {
        *bytes = 0;
        ++bytes;
        --length;
    }
}

static fdn_bytes *fdn_bytes_value(uint64_t handle) {
    return (fdn_bytes *)(uintptr_t)handle;
}

static uint64_t fdn_bytes_create(const uint8_t *value, size_t length) {
    fdn_bytes *result = fdn_alloc(sizeof(*result));
    result->data = NULL;
    result->length = length;
    if (length != 0) {
        result->data = fdn_alloc(length);
        (void)memcpy(result->data, value, length);
    }
    return (uint64_t)(uintptr_t)result;
}

static void fdn_bytes_release(fdn_bytes *value) {
    if (value == NULL) {
        return;
    }
    if (value->data != NULL) {
        fdn_secure_zero(value->data, value->length);
        fdn_dealloc(value->data);
    }
    fdn_secure_zero(value, sizeof(*value));
    fdn_dealloc(value);
}

static void fdn_sha256_transform(fdn_sha256 *context, const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0; index < 16; ++index) {
        words[index] = fdn_load_u32_be(block + index * 4);
    }
    for (index = 16; index < 64; ++index) {
        const uint32_t left = fdn_rotate_right(words[index - 15], 7) ^
                              fdn_rotate_right(words[index - 15], 18) ^
                              (words[index - 15] >> 3U);
        const uint32_t right = fdn_rotate_right(words[index - 2], 17) ^
                               fdn_rotate_right(words[index - 2], 19) ^
                               (words[index - 2] >> 10U);
        words[index] = words[index - 16] + left + words[index - 7] + right;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0; index < 64; ++index) {
        const uint32_t sum1 = fdn_rotate_right(e, 6) ^ fdn_rotate_right(e, 11) ^
                              fdn_rotate_right(e, 25);
        const uint32_t choice = (e & f) ^ ((~e) & g);
        const uint32_t first = h + sum1 + choice + fdn_sha256_constants[index] + words[index];
        const uint32_t sum0 = fdn_rotate_right(a, 2) ^ fdn_rotate_right(a, 13) ^
                              fdn_rotate_right(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
    fdn_secure_zero(words, sizeof(words));
}

static void fdn_sha256_init(fdn_sha256 *context) {
    static const uint32_t initial[8] = {
        UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
        UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
        UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
    };
    (void)memcpy(context->state, initial, sizeof(initial));
    context->length = 0;
    context->block_length = 0;
}

static void fdn_sha256_update(fdn_sha256 *context, const uint8_t *value, size_t length) {
    size_t offset = 0;
    if (length > (UINT64_MAX - context->length) / 8U) {
        fdn_panic_cstr("SHA-256 input is too large");
    }
    context->length += (uint64_t)length * 8U;
    while (offset < length) {
        size_t available = sizeof(context->block) - context->block_length;
        size_t count = length - offset;
        if (count > available) {
            count = available;
        }
        (void)memcpy(context->block + context->block_length, value + offset, count);
        context->block_length += count;
        offset += count;
        if (context->block_length == sizeof(context->block)) {
            fdn_sha256_transform(context, context->block);
            context->block_length = 0;
        }
    }
}

static void fdn_sha256_finish(fdn_sha256 *context, uint8_t output[32]) {
    size_t index;
    context->block[context->block_length++] = UINT8_C(0x80);
    if (context->block_length > 56) {
        (void)memset(context->block + context->block_length, 0,
                     sizeof(context->block) - context->block_length);
        fdn_sha256_transform(context, context->block);
        context->block_length = 0;
    }
    (void)memset(context->block + context->block_length, 0, 56 - context->block_length);
    for (index = 0; index < 8; ++index) {
        context->block[63 - index] = (uint8_t)(context->length >> (index * 8U));
    }
    fdn_sha256_transform(context, context->block);
    for (index = 0; index < 8; ++index) {
        fdn_store_u32_be(output + index * 4, context->state[index]);
    }
    fdn_secure_zero(context, sizeof(*context));
}

static void fdn_sha256_digest(const uint8_t *value, size_t length, uint8_t output[32]) {
    fdn_sha256 context;
    fdn_sha256_init(&context);
    fdn_sha256_update(&context, value, length);
    fdn_sha256_finish(&context, output);
}

static void fdn_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *value,
                            size_t length, uint8_t output[32]) {
    uint8_t normalized[64] = {0};
    uint8_t inner_pad[64];
    uint8_t outer_pad[64];
    uint8_t inner_digest[32];
    fdn_sha256 context;
    size_t index;

    if (key_length > sizeof(normalized)) {
        fdn_sha256_digest(key, key_length, normalized);
    } else if (key_length != 0) {
        (void)memcpy(normalized, key, key_length);
    }
    for (index = 0; index < sizeof(normalized); ++index) {
        inner_pad[index] = normalized[index] ^ UINT8_C(0x36);
        outer_pad[index] = normalized[index] ^ UINT8_C(0x5c);
    }
    fdn_sha256_init(&context);
    fdn_sha256_update(&context, inner_pad, sizeof(inner_pad));
    fdn_sha256_update(&context, value, length);
    fdn_sha256_finish(&context, inner_digest);
    fdn_sha256_init(&context);
    fdn_sha256_update(&context, outer_pad, sizeof(outer_pad));
    fdn_sha256_update(&context, inner_digest, sizeof(inner_digest));
    fdn_sha256_finish(&context, output);
    fdn_secure_zero(normalized, sizeof(normalized));
    fdn_secure_zero(inner_pad, sizeof(inner_pad));
    fdn_secure_zero(outer_pad, sizeof(outer_pad));
    fdn_secure_zero(inner_digest, sizeof(inner_digest));
}

uint64_t foundation_runtime_bytes_from_text(const fdn_string *value) {
    if (value == NULL || (value->length != 0 && value->data == NULL)) {
        return 0;
    }
    return fdn_bytes_create((const uint8_t *)value->data, value->length);
}

int32_t foundation_runtime_bytes_copy(uint64_t handle, uint64_t *result) {
    const fdn_bytes *value = fdn_bytes_value(handle);
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (value == NULL) {
        return 1;
    }
    *result = fdn_bytes_create(value->data, value->length);
    return 0;
}

int32_t foundation_runtime_bytes_length(uint64_t handle, uint64_t *result) {
    const fdn_bytes *value = fdn_bytes_value(handle);
    if (result == NULL || value == NULL) {
        return 1;
    }
    *result = (uint64_t)value->length;
    return 0;
}

int32_t foundation_runtime_bytes_at(uint64_t handle, uint64_t index, uint64_t *result) {
    const fdn_bytes *value = fdn_bytes_value(handle);
    if (result == NULL || value == NULL) {
        return 1;
    }
    if (index >= value->length) {
        return 2;
    }
    *result = value->data[index];
    return 0;
}

int32_t foundation_runtime_bytes_to_text(uint64_t handle, fdn_string *result) {
    const fdn_bytes *value = fdn_bytes_value(handle);
    if (result == NULL || value == NULL) {
        return 1;
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    if (!fdn_utf8_valid((const char *)value->data, value->length)) {
        return 2;
    }
    if (value->length != 0) {
        char *copy = fdn_alloc(value->length);
        (void)memcpy(copy, value->data, value->length);
        result->data = copy;
        result->length = value->length;
        result->owned = 1;
    }
    return 0;
}

void foundation_runtime_bytes_close(uint64_t *handle) {
    fdn_bytes *value;
    if (handle == NULL) {
        return;
    }
    value = fdn_bytes_value(*handle);
    *handle = 0;
    fdn_bytes_release(value);
}

int32_t foundation_runtime_base64url_encode(uint64_t handle, fdn_string *result) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const fdn_bytes *value = fdn_bytes_value(handle);
    size_t output_length;
    size_t source = 0;
    size_t target = 0;
    char *output;
    if (result == NULL || value == NULL) {
        return 1;
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    if (value->length / 3 > (SIZE_MAX - 3) / 4) {
        return 2;
    }
    output_length = value->length / 3 * 4;
    if (value->length % 3 == 1) {
        output_length += 2;
    } else if (value->length % 3 == 2) {
        output_length += 3;
    }
    if (output_length == 0) {
        return 0;
    }
    output = fdn_alloc(output_length);
    while (source + 3 <= value->length) {
        const uint32_t group = ((uint32_t)value->data[source] << 16U) |
                               ((uint32_t)value->data[source + 1] << 8U) |
                               (uint32_t)value->data[source + 2];
        output[target++] = alphabet[(group >> 18U) & 63U];
        output[target++] = alphabet[(group >> 12U) & 63U];
        output[target++] = alphabet[(group >> 6U) & 63U];
        output[target++] = alphabet[group & 63U];
        source += 3;
    }
    if (value->length - source == 1) {
        const uint32_t group = (uint32_t)value->data[source] << 16U;
        output[target++] = alphabet[(group >> 18U) & 63U];
        output[target++] = alphabet[(group >> 12U) & 63U];
    } else if (value->length - source == 2) {
        const uint32_t group = ((uint32_t)value->data[source] << 16U) |
                               ((uint32_t)value->data[source + 1] << 8U);
        output[target++] = alphabet[(group >> 18U) & 63U];
        output[target++] = alphabet[(group >> 12U) & 63U];
        output[target++] = alphabet[(group >> 6U) & 63U];
    }
    result->data = output;
    result->length = target;
    result->owned = 1;
    return 0;
}

static int32_t fdn_base64url_digit(uint8_t value) {
    if (value >= 'A' && value <= 'Z') {
        return (int32_t)(value - 'A');
    }
    if (value >= 'a' && value <= 'z') {
        return (int32_t)(value - 'a') + 26;
    }
    if (value >= '0' && value <= '9') {
        return (int32_t)(value - '0') + 52;
    }
    if (value == '-') {
        return 62;
    }
    if (value == '_') {
        return 63;
    }
    return -1;
}

int32_t foundation_runtime_base64url_decode(const fdn_string *value, uint64_t *result) {
    size_t output_length;
    uint8_t *output;
    size_t source = 0;
    size_t target = 0;
    if (result == NULL || value == NULL || (value->length != 0 && value->data == NULL)) {
        return 1;
    }
    *result = 0;
    if (value->length % 4 == 1) {
        return 2;
    }
    output_length = value->length / 4 * 3;
    if (value->length % 4 == 2) {
        output_length += 1;
    } else if (value->length % 4 == 3) {
        output_length += 2;
    }
    output = output_length == 0 ? NULL : fdn_alloc(output_length);
    while (source + 4 <= value->length) {
        const int32_t a = fdn_base64url_digit((uint8_t)value->data[source]);
        const int32_t b = fdn_base64url_digit((uint8_t)value->data[source + 1]);
        const int32_t c = fdn_base64url_digit((uint8_t)value->data[source + 2]);
        const int32_t d = fdn_base64url_digit((uint8_t)value->data[source + 3]);
        uint32_t group;
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            if (output != NULL) {
                fdn_dealloc(output);
            }
            return 2;
        }
        group = ((uint32_t)a << 18U) | ((uint32_t)b << 12U) |
                ((uint32_t)c << 6U) | (uint32_t)d;
        output[target++] = (uint8_t)(group >> 16U);
        output[target++] = (uint8_t)(group >> 8U);
        output[target++] = (uint8_t)group;
        source += 4;
    }
    if (value->length - source == 2) {
        const int32_t a = fdn_base64url_digit((uint8_t)value->data[source]);
        const int32_t b = fdn_base64url_digit((uint8_t)value->data[source + 1]);
        if (a < 0 || b < 0 || (b & 15) != 0) {
            if (output != NULL) {
                fdn_dealloc(output);
            }
            return 2;
        }
        output[target++] = (uint8_t)(((uint32_t)a << 2U) | ((uint32_t)b >> 4U));
    } else if (value->length - source == 3) {
        const int32_t a = fdn_base64url_digit((uint8_t)value->data[source]);
        const int32_t b = fdn_base64url_digit((uint8_t)value->data[source + 1]);
        const int32_t c = fdn_base64url_digit((uint8_t)value->data[source + 2]);
        if (a < 0 || b < 0 || c < 0 || (c & 3) != 0) {
            if (output != NULL) {
                fdn_dealloc(output);
            }
            return 2;
        }
        output[target++] = (uint8_t)(((uint32_t)a << 2U) | ((uint32_t)b >> 4U));
        output[target++] = (uint8_t)(((uint32_t)b << 4U) | ((uint32_t)c >> 2U));
    }
    if (output_length == 0) {
        *result = fdn_bytes_create(NULL, 0);
    } else {
        fdn_bytes *bytes = fdn_alloc(sizeof(*bytes));
        bytes->data = output;
        bytes->length = target;
        *result = (uint64_t)(uintptr_t)bytes;
    }
    return 0;
}

int32_t foundation_runtime_hmac_sha256(uint64_t key_handle, uint64_t value_handle,
                                       uint64_t *result) {
    const fdn_bytes *key = fdn_bytes_value(key_handle);
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    uint8_t digest[32];
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (key == NULL || value == NULL) {
        return 1;
    }
    fdn_hmac_sha256(key->data, key->length, value->data, value->length, digest);
    *result = fdn_bytes_create(digest, sizeof(digest));
    fdn_secure_zero(digest, sizeof(digest));
    return 0;
}

bool foundation_runtime_bytes_constant_time_equal(uint64_t left_handle, uint64_t right_handle) {
    const fdn_bytes *left = fdn_bytes_value(left_handle);
    const fdn_bytes *right = fdn_bytes_value(right_handle);
    size_t index;
    uint8_t difference = 0;
    if (left == NULL || right == NULL || left->length != right->length) {
        return false;
    }
    for (index = 0; index < left->length; ++index) {
        difference |= left->data[index] ^ right->data[index];
    }
    return difference == 0;
}
