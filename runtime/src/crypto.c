#include "foundation/runtime.h"
#include "bytes_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
extern void arc4random_buf(void *buffer, size_t length);
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <pthread.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

typedef struct fdn_bytes_builder {
    uint8_t *data;
    size_t length;
    size_t capacity;
    size_t limit;
} fdn_bytes_builder;

typedef struct fdn_sha256 {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t block_length;
} fdn_sha256;

typedef struct fdn_secret_entry {
    struct fdn_secret_entry *next;
    char *key;
    size_t key_length;
    uint8_t *value;
    size_t value_length;
} fdn_secret_entry;

typedef struct fdn_secret_memory {
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
    uint64_t references;
    fdn_secret_entry *entries;
} fdn_secret_memory;

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

static const uint8_t fdn_aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7,
    0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf,
    0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5,
    0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e,
    0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef,
    0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
    0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
    0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee,
    0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
    0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e,
    0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
    0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55,
    0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t fdn_aes_xtime(uint8_t value) {
    return (uint8_t)((value << 1U) ^ ((value >> 7U) * UINT8_C(0x1b)));
}

static void fdn_aes_expand_256(const uint8_t key[32], uint8_t round_keys[240]) {
    uint8_t temporary[4];
    uint8_t rcon = 1;
    size_t generated = 32;
    size_t index;
    (void)memcpy(round_keys, key, 32);
    while (generated < 240) {
        (void)memcpy(temporary, round_keys + generated - 4, 4);
        if (generated % 32 == 0) {
            const uint8_t first = temporary[0];
            temporary[0] = fdn_aes_sbox[temporary[1]] ^ rcon;
            temporary[1] = fdn_aes_sbox[temporary[2]];
            temporary[2] = fdn_aes_sbox[temporary[3]];
            temporary[3] = fdn_aes_sbox[first];
            rcon = fdn_aes_xtime(rcon);
        } else if (generated % 32 == 16) {
            for (index = 0; index < 4; ++index) {
                temporary[index] = fdn_aes_sbox[temporary[index]];
            }
        }
        for (index = 0; index < 4 && generated < 240; ++index) {
            round_keys[generated] = round_keys[generated - 32] ^ temporary[index];
            ++generated;
        }
    }
    fdn_secure_zero(temporary, sizeof(temporary));
}

static void fdn_aes_add_round_key(uint8_t state[16], const uint8_t key[16]) {
    size_t index;
    for (index = 0; index < 16; ++index) {
        state[index] ^= key[index];
    }
}

static void fdn_aes_sub_bytes(uint8_t state[16]) {
    size_t index;
    for (index = 0; index < 16; ++index) {
        state[index] = fdn_aes_sbox[state[index]];
    }
}

static void fdn_aes_shift_rows(uint8_t state[16]) {
    uint8_t temporary;
    temporary = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temporary;
}

static void fdn_aes_mix_columns(uint8_t state[16]) {
    size_t column;
    for (column = 0; column < 4; ++column) {
        uint8_t *value = state + column * 4;
        const uint8_t total = value[0] ^ value[1] ^ value[2] ^ value[3];
        const uint8_t first = value[0];
        value[0] ^= total ^ fdn_aes_xtime(value[0] ^ value[1]);
        value[1] ^= total ^ fdn_aes_xtime(value[1] ^ value[2]);
        value[2] ^= total ^ fdn_aes_xtime(value[2] ^ value[3]);
        value[3] ^= total ^ fdn_aes_xtime(value[3] ^ first);
    }
}

static void fdn_aes_encrypt_block(const uint8_t input[16], uint8_t output[16],
                                  const uint8_t round_keys[240]) {
    uint8_t state[16];
    size_t round;
    (void)memcpy(state, input, sizeof(state));
    fdn_aes_add_round_key(state, round_keys);
    for (round = 1; round < 14; ++round) {
        fdn_aes_sub_bytes(state);
        fdn_aes_shift_rows(state);
        fdn_aes_mix_columns(state);
        fdn_aes_add_round_key(state, round_keys + round * 16);
    }
    fdn_aes_sub_bytes(state);
    fdn_aes_shift_rows(state);
    fdn_aes_add_round_key(state, round_keys + 224);
    (void)memcpy(output, state, sizeof(state));
    fdn_secure_zero(state, sizeof(state));
}

static void fdn_gcm_shift_right(uint8_t value[16]) {
    uint8_t carry = 0;
    size_t index;
    for (index = 0; index < 16; ++index) {
        const uint8_t next = value[index] & 1U;
        value[index] = (uint8_t)((value[index] >> 1U) | (carry << 7U));
        carry = next;
    }
}

static void fdn_gcm_multiply(uint8_t value[16], const uint8_t hash[16]) {
    uint8_t product[16] = {0};
    uint8_t factor[16];
    size_t bit;
    size_t index;
    (void)memcpy(factor, hash, sizeof(factor));
    for (bit = 0; bit < 128; ++bit) {
        if ((value[bit / 8] & (uint8_t)(UINT8_C(0x80) >> (bit % 8))) != 0) {
            for (index = 0; index < 16; ++index) {
                product[index] ^= factor[index];
            }
        }
        {
            const uint8_t reduced = factor[15] & 1U;
            fdn_gcm_shift_right(factor);
            if (reduced != 0) {
                factor[0] ^= UINT8_C(0xe1);
            }
        }
    }
    (void)memcpy(value, product, sizeof(product));
    fdn_secure_zero(product, sizeof(product));
    fdn_secure_zero(factor, sizeof(factor));
}

static void fdn_gcm_block(uint8_t state[16], const uint8_t block[16],
                          const uint8_t hash[16]) {
    size_t index;
    for (index = 0; index < 16; ++index) {
        state[index] ^= block[index];
    }
    fdn_gcm_multiply(state, hash);
}

static void fdn_gcm_data(uint8_t state[16], const uint8_t *data, size_t length,
                         const uint8_t hash[16]) {
    uint8_t block[16] = {0};
    while (length >= 16) {
        fdn_gcm_block(state, data, hash);
        data += 16;
        length -= 16;
    }
    if (length != 0) {
        (void)memcpy(block, data, length);
        fdn_gcm_block(state, block, hash);
    }
    fdn_secure_zero(block, sizeof(block));
}

static void fdn_store_u64_be(uint8_t output[8], uint64_t value) {
    size_t index;
    for (index = 0; index < 8; ++index) {
        output[7 - index] = (uint8_t)(value >> (index * 8U));
    }
}

static void fdn_gcm_tag(const uint8_t *associated_data, size_t associated_length,
                        const uint8_t *ciphertext, size_t ciphertext_length,
                        const uint8_t hash[16], const uint8_t mask[16], uint8_t tag[16]) {
    uint8_t state[16] = {0};
    uint8_t lengths[16];
    size_t index;
    fdn_gcm_data(state, associated_data, associated_length, hash);
    fdn_gcm_data(state, ciphertext, ciphertext_length, hash);
    fdn_store_u64_be(lengths, (uint64_t)associated_length * 8U);
    fdn_store_u64_be(lengths + 8, (uint64_t)ciphertext_length * 8U);
    fdn_gcm_block(state, lengths, hash);
    for (index = 0; index < 16; ++index) {
        tag[index] = state[index] ^ mask[index];
    }
    fdn_secure_zero(state, sizeof(state));
    fdn_secure_zero(lengths, sizeof(lengths));
}

static void fdn_gcm_increment(uint8_t counter[16]) {
    size_t index = 16;
    while (index > 12) {
        --index;
        ++counter[index];
        if (counter[index] != 0) {
            break;
        }
    }
}

static void fdn_gcm_crypt(const uint8_t *input, uint8_t *output, size_t length,
                          uint8_t counter[16], const uint8_t round_keys[240]) {
    uint8_t stream[16];
    size_t index;
    while (length != 0) {
        const size_t count = length < 16 ? length : 16;
        fdn_gcm_increment(counter);
        fdn_aes_encrypt_block(counter, stream, round_keys);
        for (index = 0; index < count; ++index) {
            output[index] = input[index] ^ stream[index];
        }
        input += count;
        output += count;
        length -= count;
    }
    fdn_secure_zero(stream, sizeof(stream));
}

static bool fdn_crypto_random(uint8_t *output, size_t length) {
#if defined(_WIN32)
    return length <= ULONG_MAX &&
           BCryptGenRandom(NULL, output, (ULONG)length,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    arc4random_buf(output, length);
    return true;
#else
    size_t offset = 0;
#if defined(__linux__)
    while (offset < length) {
        const ssize_t count = getrandom(output + offset, length - offset, 0);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (offset == length) {
        return true;
    }
#endif
    {
        const int source = open("/dev/urandom", O_RDONLY);
        if (source < 0) {
            return false;
        }
        while (offset < length) {
            const ssize_t count = read(source, output + offset, length - offset);
            if (count > 0) {
                offset += (size_t)count;
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                (void)close(source);
                return false;
            }
        }
        if (close(source) != 0) {
            return false;
        }
    }
    return true;
#endif
}

static fdn_bytes *fdn_bytes_value(uint64_t handle) {
    return (fdn_bytes *)(uintptr_t)handle;
}

static uint64_t fdn_bytes_create(const uint8_t *value, size_t length) {
    fdn_bytes *result = fdn_alloc(sizeof(*result));
    result->data = NULL;
    result->length = length;
    result->capacity = length;
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
        fdn_secure_zero(value->data, value->capacity);
        fdn_dealloc(value->data);
    }
    fdn_secure_zero(value, sizeof(*value));
    fdn_dealloc(value);
}

static fdn_bytes_builder *fdn_bytes_builder_value(uint64_t handle) {
    return (fdn_bytes_builder *)(uintptr_t)handle;
}

static bool fdn_bytes_builder_reserve(fdn_bytes_builder *builder, size_t count) {
    size_t required;
    size_t capacity;
    uint8_t *data;
    if (count > builder->limit - builder->length || count > SIZE_MAX - builder->length) {
        return false;
    }
    required = builder->length + count;
    if (required <= builder->capacity) {
        return true;
    }
    capacity = builder->capacity == 0 ? 64 : builder->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if (capacity > builder->limit) {
        capacity = builder->limit;
    }
    if (capacity < required) {
        return false;
    }
    data = fdn_alloc(capacity);
    if (builder->length != 0) {
        (void)memcpy(data, builder->data, builder->length);
    }
    if (builder->data != NULL) {
        fdn_secure_zero(builder->data, builder->capacity);
        fdn_dealloc(builder->data);
    }
    builder->data = data;
    builder->capacity = capacity;
    return true;
}

static void fdn_bytes_builder_release(fdn_bytes_builder *builder) {
    if (builder == NULL) {
        return;
    }
    if (builder->data != NULL) {
        fdn_secure_zero(builder->data, builder->capacity);
        fdn_dealloc(builder->data);
    }
    fdn_secure_zero(builder, sizeof(*builder));
    fdn_dealloc(builder);
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

static bool fdn_sha256_accepts(const fdn_sha256 *context, size_t length) {
    return length <= (UINT64_MAX - context->length) / 8U;
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

int32_t foundation_runtime_bytes_slice(uint64_t handle, uint64_t start, uint64_t end,
                                       uint64_t *result) {
    const fdn_bytes *value = fdn_bytes_value(handle);
    const uint8_t *source = NULL;
    if (result == NULL || value == NULL) {
        return 1;
    }
    *result = 0;
    if (start > end || end > value->length) {
        return 2;
    }
    if (start != end) {
        source = value->data + (size_t)start;
    }
    *result = fdn_bytes_create(source, (size_t)(end - start));
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

int32_t foundation_runtime_bytes_builder_open(uint64_t limit, uint64_t *result) {
    fdn_bytes_builder *builder;
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (limit > SIZE_MAX) {
        return 2;
    }
    builder = fdn_alloc(sizeof(*builder));
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
    builder->limit = (size_t)limit;
    *result = (uint64_t)(uintptr_t)builder;
    return 0;
}

int32_t foundation_runtime_bytes_builder_length(uint64_t handle, uint64_t *result) {
    const fdn_bytes_builder *builder = fdn_bytes_builder_value(handle);
    if (result == NULL || builder == NULL) {
        return 1;
    }
    *result = (uint64_t)builder->length;
    return 0;
}

int32_t foundation_runtime_bytes_builder_append_byte(uint64_t handle, uint64_t value) {
    fdn_bytes_builder *builder = fdn_bytes_builder_value(handle);
    if (builder == NULL) {
        return 1;
    }
    if (value > UINT8_MAX) {
        return 3;
    }
    if (!fdn_bytes_builder_reserve(builder, 1)) {
        return 2;
    }
    builder->data[builder->length++] = (uint8_t)value;
    return 0;
}

int32_t foundation_runtime_bytes_builder_append(uint64_t handle, uint64_t value_handle) {
    fdn_bytes_builder *builder = fdn_bytes_builder_value(handle);
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    if (builder == NULL || value == NULL) {
        return 1;
    }
    if (!fdn_bytes_builder_reserve(builder, value->length)) {
        return 2;
    }
    if (value->length != 0) {
        (void)memcpy(builder->data + builder->length, value->data, value->length);
        builder->length += value->length;
    }
    return 0;
}

int32_t foundation_runtime_bytes_builder_finish(uint64_t *handle, uint64_t *result) {
    fdn_bytes_builder *builder;
    fdn_bytes *value;
    if (handle == NULL || result == NULL) {
        return 1;
    }
    *result = 0;
    builder = fdn_bytes_builder_value(*handle);
    if (builder == NULL) {
        return 1;
    }
    value = fdn_alloc(sizeof(*value));
    value->data = builder->data;
    value->length = builder->length;
    value->capacity = builder->capacity;
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
    *handle = 0;
    fdn_bytes_builder_release(builder);
    *result = (uint64_t)(uintptr_t)value;
    return 0;
}

void foundation_runtime_bytes_builder_close(uint64_t *handle) {
    fdn_bytes_builder *builder;
    if (handle == NULL) {
        return;
    }
    builder = fdn_bytes_builder_value(*handle);
    *handle = 0;
    fdn_bytes_builder_release(builder);
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
        if (output == NULL || target > output_length - 3) {
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
        bytes->capacity = output_length;
        *result = (uint64_t)(uintptr_t)bytes;
    }
    return 0;
}

int32_t foundation_runtime_base64_encode(uint64_t handle, fdn_string *result) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
    if (value->length > (SIZE_MAX - 2) / 3) {
        return 2;
    }
    output_length = (value->length + 2) / 3 * 4;
    if (output_length == 0) {
        return 0;
    }
    output = fdn_alloc(output_length);
    while (source < value->length) {
        const size_t remaining = value->length - source;
        const uint32_t first = value->data[source++];
        const uint32_t second = remaining > 1 ? value->data[source++] : 0;
        const uint32_t third = remaining > 2 ? value->data[source++] : 0;
        const uint32_t group = (first << 16U) | (second << 8U) | third;
        output[target++] = alphabet[(group >> 18U) & 63U];
        output[target++] = alphabet[(group >> 12U) & 63U];
        output[target++] = remaining > 1 ? alphabet[(group >> 6U) & 63U] : '=';
        output[target++] = remaining > 2 ? alphabet[group & 63U] : '=';
    }
    result->data = output;
    result->length = target;
    result->owned = 1;
    return 0;
}

static int32_t fdn_base64_digit(uint8_t value) {
    if (value >= 'A' && value <= 'Z') {
        return (int32_t)(value - 'A');
    }
    if (value >= 'a' && value <= 'z') {
        return (int32_t)(value - 'a') + 26;
    }
    if (value >= '0' && value <= '9') {
        return (int32_t)(value - '0') + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

int32_t foundation_runtime_base64_decode(const fdn_string *value, uint64_t *result) {
    size_t padding = 0;
    size_t output_length;
    uint8_t *output;
    size_t source;
    size_t target = 0;
    if (result == NULL || value == NULL ||
        (value->length != 0 && value->data == NULL)) {
        return 1;
    }
    *result = 0;
    if (value->length % 4 != 0) {
        return 2;
    }
    if (value->length != 0 && value->data[value->length - 1] == '=') {
        padding = 1;
        if (value->data[value->length - 2] == '=') {
            padding = 2;
        }
    }
    output_length = value->length / 4 * 3 - padding;
    output = output_length == 0 ? NULL : fdn_alloc(output_length);
    for (source = 0; source < value->length; source += 4) {
        const bool last = source + 4 == value->length;
        const bool third_padding = value->data[source + 2] == '=';
        const bool fourth_padding = value->data[source + 3] == '=';
        const int32_t a = fdn_base64_digit((uint8_t)value->data[source]);
        const int32_t b = fdn_base64_digit((uint8_t)value->data[source + 1]);
        const int32_t c = third_padding ? 0 :
            fdn_base64_digit((uint8_t)value->data[source + 2]);
        const int32_t d = fourth_padding ? 0 :
            fdn_base64_digit((uint8_t)value->data[source + 3]);
        uint32_t group;
        if (a < 0 || b < 0 || c < 0 || d < 0 || (!last && (third_padding || fourth_padding)) ||
            (third_padding && !fourth_padding) || (third_padding && (b & 15) != 0) ||
            (!third_padding && fourth_padding && (c & 3) != 0)) {
            if (output != NULL) {
                fdn_secure_zero(output, output_length);
                fdn_dealloc(output);
            }
            return 2;
        }
        group = ((uint32_t)a << 18U) | ((uint32_t)b << 12U) |
                ((uint32_t)c << 6U) | (uint32_t)d;
        if (target < output_length) {
            output[target++] = (uint8_t)(group >> 16U);
        }
        if (target < output_length) {
            output[target++] = (uint8_t)(group >> 8U);
        }
        if (target < output_length) {
            output[target++] = (uint8_t)group;
        }
    }
    if (output_length == 0) {
        *result = fdn_bytes_create(NULL, 0);
    } else {
        fdn_bytes *bytes = fdn_alloc(sizeof(*bytes));
        bytes->data = output;
        bytes->length = target;
        bytes->capacity = output_length;
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

uint64_t foundation_runtime_sha256_open(void) {
    fdn_sha256 *context = fdn_alloc(sizeof(*context));
    fdn_sha256_init(context);
    return (uint64_t)(uintptr_t)context;
}

int32_t foundation_runtime_sha256_update_text(uint64_t handle,
                                              const fdn_string *value) {
    fdn_sha256 *context = (fdn_sha256 *)(uintptr_t)handle;
    if (context == NULL || value == NULL ||
        (value->data == NULL && value->length != 0)) {
        return 1;
    }
    if (!fdn_sha256_accepts(context, value->length)) {
        return 2;
    }
    fdn_sha256_update(context, (const uint8_t *)value->data, value->length);
    return 0;
}

int32_t foundation_runtime_sha256_update_bytes(uint64_t handle,
                                               uint64_t value_handle) {
    fdn_sha256 *context = (fdn_sha256 *)(uintptr_t)handle;
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    if (context == NULL || value == NULL) {
        return 1;
    }
    if (!fdn_sha256_accepts(context, value->length)) {
        return 2;
    }
    fdn_sha256_update(context, value->data, value->length);
    return 0;
}

int32_t foundation_runtime_sha256_finish(uint64_t *handle, uint64_t *result) {
    fdn_sha256 *context;
    uint8_t digest[32];
    if (handle == NULL || result == NULL) {
        return 1;
    }
    *result = 0;
    context = (fdn_sha256 *)(uintptr_t)*handle;
    if (context == NULL) {
        return 1;
    }
    *handle = 0;
    fdn_sha256_finish(context, digest);
    fdn_dealloc(context);
    *result = fdn_bytes_create(digest, sizeof(digest));
    fdn_secure_zero(digest, sizeof(digest));
    return 0;
}

void foundation_runtime_sha256_close(uint64_t *handle) {
    fdn_sha256 *context;
    if (handle == NULL) {
        return;
    }
    context = (fdn_sha256 *)(uintptr_t)*handle;
    *handle = 0;
    if (context != NULL) {
        fdn_secure_zero(context, sizeof(*context));
        fdn_dealloc(context);
    }
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

int32_t foundation_runtime_aes256_gcm_encrypt(uint64_t key_handle,
                                              uint64_t value_handle,
                                              const fdn_string *associated_data,
                                              uint64_t *result) {
    const fdn_bytes *key = fdn_bytes_value(key_handle);
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    uint8_t round_keys[240];
    uint8_t zero[16] = {0};
    uint8_t hash[16];
    uint8_t counter[16] = {0};
    uint8_t mask[16];
    uint8_t tag[16];
    uint8_t *output;
    size_t output_length;
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (key == NULL || value == NULL || associated_data == NULL ||
        (associated_data->length != 0 && associated_data->data == NULL)) {
        return 1;
    }
    if (key->length != 32) {
        return 2;
    }
    if (value->length > SIZE_MAX - 28 || value->length > UINT64_MAX / 8U ||
        associated_data->length > UINT64_MAX / 8U) {
        return 3;
    }
    output_length = value->length + 28;
    output = fdn_alloc(output_length);
    if (!fdn_crypto_random(output, 12)) {
        fdn_secure_zero(output, output_length);
        fdn_dealloc(output);
        return 5;
    }
    fdn_aes_expand_256(key->data, round_keys);
    fdn_aes_encrypt_block(zero, hash, round_keys);
    (void)memcpy(counter, output, 12);
    counter[15] = 1;
    fdn_aes_encrypt_block(counter, mask, round_keys);
    fdn_gcm_crypt(value->data, output + 12, value->length, counter, round_keys);
    fdn_gcm_tag((const uint8_t *)associated_data->data, associated_data->length,
                output + 12, value->length, hash, mask, tag);
    (void)memcpy(output + 12 + value->length, tag, sizeof(tag));
    {
        fdn_bytes *bytes = fdn_alloc(sizeof(*bytes));
        bytes->data = output;
        bytes->length = output_length;
        bytes->capacity = output_length;
        *result = (uint64_t)(uintptr_t)bytes;
    }
    fdn_secure_zero(round_keys, sizeof(round_keys));
    fdn_secure_zero(hash, sizeof(hash));
    fdn_secure_zero(counter, sizeof(counter));
    fdn_secure_zero(mask, sizeof(mask));
    fdn_secure_zero(tag, sizeof(tag));
    return 0;
}

int32_t foundation_runtime_aes256_gcm_decrypt(uint64_t key_handle,
                                              uint64_t value_handle,
                                              const fdn_string *associated_data,
                                              uint64_t *result) {
    const fdn_bytes *key = fdn_bytes_value(key_handle);
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    uint8_t round_keys[240];
    uint8_t zero[16] = {0};
    uint8_t hash[16];
    uint8_t counter[16] = {0};
    uint8_t mask[16];
    uint8_t tag[16];
    uint8_t difference = 0;
    uint8_t *output = NULL;
    size_t plaintext_length;
    size_t index;
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (key == NULL || value == NULL || associated_data == NULL ||
        (associated_data->length != 0 && associated_data->data == NULL)) {
        return 1;
    }
    if (key->length != 32) {
        return 2;
    }
    if (value->length < 28 || value->length - 28 > UINT64_MAX / 8U ||
        associated_data->length > UINT64_MAX / 8U) {
        return 3;
    }
    plaintext_length = value->length - 28;
    fdn_aes_expand_256(key->data, round_keys);
    fdn_aes_encrypt_block(zero, hash, round_keys);
    (void)memcpy(counter, value->data, 12);
    counter[15] = 1;
    fdn_aes_encrypt_block(counter, mask, round_keys);
    fdn_gcm_tag((const uint8_t *)associated_data->data, associated_data->length,
                value->data + 12, plaintext_length, hash, mask, tag);
    for (index = 0; index < sizeof(tag); ++index) {
        difference |= tag[index] ^ value->data[12 + plaintext_length + index];
    }
    if (difference != 0) {
        fdn_secure_zero(round_keys, sizeof(round_keys));
        fdn_secure_zero(hash, sizeof(hash));
        fdn_secure_zero(counter, sizeof(counter));
        fdn_secure_zero(mask, sizeof(mask));
        fdn_secure_zero(tag, sizeof(tag));
        return 4;
    }
    if (plaintext_length != 0) {
        output = fdn_alloc(plaintext_length);
    }
    fdn_gcm_crypt(value->data + 12, output, plaintext_length, counter, round_keys);
    {
        fdn_bytes *bytes = fdn_alloc(sizeof(*bytes));
        bytes->data = output;
        bytes->length = plaintext_length;
        bytes->capacity = plaintext_length;
        *result = (uint64_t)(uintptr_t)bytes;
    }
    fdn_secure_zero(round_keys, sizeof(round_keys));
    fdn_secure_zero(hash, sizeof(hash));
    fdn_secure_zero(counter, sizeof(counter));
    fdn_secure_zero(mask, sizeof(mask));
    fdn_secure_zero(tag, sizeof(tag));
    return 0;
}

static void fdn_secret_memory_lock(fdn_secret_memory *store) {
#if defined(_WIN32)
    EnterCriticalSection(&store->lock);
#else
    if (pthread_mutex_lock(&store->lock) != 0) {
        fdn_panic_cstr("secret memory lock failed");
    }
#endif
}

static void fdn_secret_memory_unlock(fdn_secret_memory *store) {
#if defined(_WIN32)
    LeaveCriticalSection(&store->lock);
#else
    if (pthread_mutex_unlock(&store->lock) != 0) {
        fdn_panic_cstr("secret memory unlock failed");
    }
#endif
}

static bool fdn_secret_key_valid(const fdn_string *key) {
    size_t index;
    if (key == NULL || key->length == 0 || key->data == NULL) {
        return false;
    }
    for (index = 0; index < key->length; ++index) {
        if (key->data[index] == '\0') {
            return false;
        }
    }
    return true;
}

static fdn_secret_entry *fdn_secret_find(fdn_secret_memory *store,
                                         const fdn_string *key) {
    fdn_secret_entry *entry = store->entries;
    while (entry != NULL) {
        if (entry->key_length == key->length &&
            memcmp(entry->key, key->data, key->length) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static void fdn_secret_entry_release(fdn_secret_entry *entry) {
    if (entry == NULL) {
        return;
    }
    if (entry->key != NULL) {
        fdn_secure_zero(entry->key, entry->key_length);
        fdn_dealloc(entry->key);
    }
    if (entry->value != NULL) {
        fdn_secure_zero(entry->value, entry->value_length);
        fdn_dealloc(entry->value);
    }
    fdn_secure_zero(entry, sizeof(*entry));
    fdn_dealloc(entry);
}

uint64_t foundation_runtime_secret_memory_open(void) {
    fdn_secret_memory *store = fdn_alloc(sizeof(*store));
    store->references = 1;
    store->entries = NULL;
#if defined(_WIN32)
    InitializeCriticalSection(&store->lock);
#else
    if (pthread_mutex_init(&store->lock, NULL) != 0) {
        fdn_dealloc(store);
        return 0;
    }
#endif
    return (uint64_t)(uintptr_t)store;
}

uint64_t foundation_runtime_secret_memory_retain(uint64_t handle) {
    fdn_secret_memory *store = (fdn_secret_memory *)(uintptr_t)handle;
    if (store == NULL) {
        return 0;
    }
    fdn_secret_memory_lock(store);
    if (store->references == UINT64_MAX) {
        fdn_secret_memory_unlock(store);
        fdn_panic_cstr("secret memory reference overflow");
    }
    ++store->references;
    fdn_secret_memory_unlock(store);
    return handle;
}

int32_t foundation_runtime_secret_memory_set(uint64_t handle,
                                             const fdn_string *key,
                                             uint64_t value_handle) {
    fdn_secret_memory *store = (fdn_secret_memory *)(uintptr_t)handle;
    const fdn_bytes *value = fdn_bytes_value(value_handle);
    fdn_secret_entry *entry;
    uint8_t *copy;
    if (store == NULL || value == NULL) {
        return 1;
    }
    if (!fdn_secret_key_valid(key)) {
        return 3;
    }
    copy = value->length == 0 ? NULL : fdn_alloc(value->length);
    if (value->length != 0) {
        (void)memcpy(copy, value->data, value->length);
    }
    fdn_secret_memory_lock(store);
    entry = fdn_secret_find(store, key);
    if (entry == NULL) {
        entry = fdn_alloc(sizeof(*entry));
        entry->key = fdn_alloc(key->length);
        (void)memcpy(entry->key, key->data, key->length);
        entry->key_length = key->length;
        entry->value = NULL;
        entry->value_length = 0;
        entry->next = store->entries;
        store->entries = entry;
    }
    if (entry->value != NULL) {
        fdn_secure_zero(entry->value, entry->value_length);
        fdn_dealloc(entry->value);
    }
    entry->value = copy;
    entry->value_length = value->length;
    fdn_secret_memory_unlock(store);
    return 0;
}

int32_t foundation_runtime_secret_memory_get(uint64_t handle,
                                             const fdn_string *key,
                                             uint64_t *result) {
    fdn_secret_memory *store = (fdn_secret_memory *)(uintptr_t)handle;
    fdn_secret_entry *entry;
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (store == NULL) {
        return 1;
    }
    if (!fdn_secret_key_valid(key)) {
        return 3;
    }
    fdn_secret_memory_lock(store);
    entry = fdn_secret_find(store, key);
    if (entry == NULL) {
        fdn_secret_memory_unlock(store);
        return 2;
    }
    *result = fdn_bytes_create(entry->value, entry->value_length);
    fdn_secret_memory_unlock(store);
    return 0;
}

int32_t foundation_runtime_secret_memory_delete(uint64_t handle,
                                                const fdn_string *key) {
    fdn_secret_memory *store = (fdn_secret_memory *)(uintptr_t)handle;
    fdn_secret_entry **current;
    fdn_secret_entry *removed;
    if (store == NULL) {
        return 1;
    }
    if (!fdn_secret_key_valid(key)) {
        return 3;
    }
    fdn_secret_memory_lock(store);
    current = &store->entries;
    while (*current != NULL &&
           ((*current)->key_length != key->length ||
            memcmp((*current)->key, key->data, key->length) != 0)) {
        current = &(*current)->next;
    }
    if (*current == NULL) {
        fdn_secret_memory_unlock(store);
        return 0;
    }
    removed = *current;
    *current = removed->next;
    fdn_secret_memory_unlock(store);
    fdn_secret_entry_release(removed);
    return 0;
}

void foundation_runtime_secret_memory_close(uint64_t *handle) {
    fdn_secret_memory *store;
    fdn_secret_entry *entries;
    if (handle == NULL) {
        return;
    }
    store = (fdn_secret_memory *)(uintptr_t)*handle;
    *handle = 0;
    if (store == NULL) {
        return;
    }
    fdn_secret_memory_lock(store);
    --store->references;
    if (store->references != 0) {
        fdn_secret_memory_unlock(store);
        return;
    }
    entries = store->entries;
    store->entries = NULL;
    fdn_secret_memory_unlock(store);
#if defined(_WIN32)
    DeleteCriticalSection(&store->lock);
#else
    if (pthread_mutex_destroy(&store->lock) != 0) {
        fdn_panic_cstr("secret memory lock destroy failed");
    }
#endif
    while (entries != NULL) {
        fdn_secret_entry *next = entries->next;
        fdn_secret_entry_release(entries);
        entries = next;
    }
    fdn_secure_zero(store, sizeof(*store));
    fdn_dealloc(store);
}
