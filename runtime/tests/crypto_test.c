#include "foundation/runtime.h"

#include <assert.h>
#include <string.h>

static uint64_t bytes_from_data(const char *value, size_t length) {
    const fdn_string text = fdn_string_static(value, length);
    const uint64_t result = foundation_runtime_bytes_from_text(&text);
    assert(result != 0);
    return result;
}

static void assert_text(fdn_string value, const char *expected) {
    assert(value.length == strlen(expected));
    assert(memcmp(value.data, expected, value.length) == 0);
}

int main(void) {
    static const char hmac_key[20] = {
        '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b',
        '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b', '\x0b',
    };
    static const uint8_t hmac_expected[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
    };
    static const uint8_t long_hmac_expected[32] = {
        0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f,
        0x0d, 0x8a, 0x26, 0xaa, 0xcb, 0xf5, 0xb7, 0x7f,
        0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28, 0xc5, 0x14,
        0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54,
    };
    static const uint8_t sha256_expected[32] = {
        0xbe, 0xf5, 0x7e, 0xc7, 0xf5, 0x3a, 0x6d, 0x40,
        0xbe, 0xb6, 0x40, 0xa7, 0x80, 0xa6, 0x39, 0xc8,
        0x3b, 0xc2, 0x9a, 0xc8, 0xa9, 0x81, 0x6f, 0x1f,
        0xc6, 0xc5, 0xc6, 0xdc, 0xd9, 0x3c, 0x47, 0x21,
    };
    static const char long_hmac_message[] =
        "Test Using Larger Than Block-Size Key - Hash Key First";
    static const uint8_t aes_known[44] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e,
        0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18,
        0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0,
        0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19,
    };
    static const uint8_t aes_zero_key[32] = {0};
    static const uint8_t aes_zero_plaintext[16] = {0};
    const size_t initial_allocations = fdn_live_allocations();
    char long_hmac_key[131];
    uint64_t abc = bytes_from_data("abc", 3);
    uint64_t decoded = 0;
    uint64_t decoded_standard = 0;
    uint64_t copied = 0;
    uint64_t builder = 0;
    uint64_t built = 0;
    uint64_t sliced = 0;
    uint64_t closed_builder = 0;
    uint64_t invalid_utf8 = 0;
    uint64_t key = bytes_from_data(hmac_key, sizeof(hmac_key));
    uint64_t message = bytes_from_data("Hi There", 8);
    uint64_t digest = 0;
    uint64_t expected_digest = bytes_from_data((const char *)hmac_expected, sizeof(hmac_expected));
    uint64_t sha256 = 0;
    uint64_t sha256_digest = 0;
    uint64_t sha256_expected_digest =
        bytes_from_data((const char *)sha256_expected, sizeof(sha256_expected));
    uint64_t sha256_suffix = bytes_from_data("def", 3);
    uint64_t closed_sha256 = 0;
    uint64_t long_key;
    uint64_t long_message;
    uint64_t long_digest = 0;
    uint64_t long_expected_digest;
    uint64_t aes_key;
    uint64_t aes_plaintext;
    uint64_t aes_ciphertext = 0;
    uint64_t aes_decrypted = 0;
    uint64_t aes_vector;
    uint64_t memory;
    uint64_t memory_copy;
    uint64_t stored = 0;
    uint64_t length = 0;
    uint64_t byte = 0;
    fdn_string encoded = fdn_string_static("", 0);
    fdn_string encoded_standard = fdn_string_static("", 0);
    fdn_string text = fdn_string_static("", 0);
    fdn_string invalid = fdn_string_static("Zh", 2);
    fdn_string raw_invalid_utf8 = fdn_string_static("_w", 2);
    fdn_string aad = fdn_string_static("binding", 7);
    fdn_string wrong_aad = fdn_string_static("wrong", 5);
    fdn_string empty = fdn_string_static("", 0);
    fdn_string secret_key = fdn_string_static("token", 5);
    fdn_string sha256_prefix = fdn_string_static("abc", 3);

    (void)memset(long_hmac_key, 0xaa, sizeof(long_hmac_key));
    long_key = bytes_from_data(long_hmac_key, sizeof(long_hmac_key));
    long_message = bytes_from_data(long_hmac_message, sizeof(long_hmac_message) - 1);
    long_expected_digest =
        bytes_from_data((const char *)long_hmac_expected, sizeof(long_hmac_expected));
    aes_key = bytes_from_data((const char *)aes_zero_key, sizeof(aes_zero_key));
    aes_plaintext = bytes_from_data((const char *)aes_zero_plaintext, sizeof(aes_zero_plaintext));
    aes_vector = bytes_from_data((const char *)aes_known, sizeof(aes_known));

    assert(foundation_runtime_bytes_length(abc, &length) == 0);
    assert(length == 3);
    assert(foundation_runtime_bytes_at(abc, 1, &byte) == 0);
    assert(byte == 'b');
    assert(foundation_runtime_bytes_at(abc, 3, &byte) == 2);
    assert(foundation_runtime_bytes_copy(abc, &copied) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(abc, copied));
    assert(foundation_runtime_bytes_builder_open(4, &builder) == 0);
    assert(foundation_runtime_bytes_builder_length(builder, &length) == 0);
    assert(length == 0);
    assert(foundation_runtime_bytes_builder_append_byte(builder, '!') == 0);
    assert(foundation_runtime_bytes_builder_append(builder, abc) == 0);
    assert(foundation_runtime_bytes_builder_append_byte(builder, 'x') == 2);
    assert(foundation_runtime_bytes_builder_append_byte(builder, 256) == 3);
    assert(foundation_runtime_bytes_builder_length(builder, &length) == 0);
    assert(length == 4);
    assert(foundation_runtime_bytes_builder_finish(&builder, &built) == 0);
    assert(builder == 0);
    assert(foundation_runtime_bytes_slice(built, 1, 4, &sliced) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(abc, sliced));
    assert(foundation_runtime_bytes_slice(built, 4, 3, &stored) == 2);
    assert(foundation_runtime_bytes_slice(built, 0, 5, &stored) == 2);
    assert(foundation_runtime_bytes_builder_open(1, &closed_builder) == 0);
    foundation_runtime_bytes_builder_close(&closed_builder);
    assert(closed_builder == 0);

    assert(foundation_runtime_base64url_encode(abc, &encoded) == 0);
    assert_text(encoded, "YWJj");
    assert(foundation_runtime_base64url_decode(&encoded, &decoded) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(abc, decoded));
    assert(foundation_runtime_bytes_to_text(decoded, &text) == 0);
    assert_text(text, "abc");

    assert(foundation_runtime_base64url_decode(&invalid, &invalid_utf8) == 2);
    assert(invalid_utf8 == 0);
    assert(foundation_runtime_base64url_decode(&raw_invalid_utf8, &invalid_utf8) == 0);
    assert(foundation_runtime_bytes_to_text(invalid_utf8, &text) == 2);

    assert(foundation_runtime_base64_encode(abc, &encoded_standard) == 0);
    assert_text(encoded_standard, "YWJj");
    assert(foundation_runtime_base64_decode(&encoded_standard, &decoded_standard) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(abc, decoded_standard));
    fdn_string_drop(&encoded_standard);
    encoded_standard = fdn_string_static("YWJjZA==", 8);
    assert(foundation_runtime_base64_decode(&encoded_standard, &stored) == 0);
    assert(foundation_runtime_bytes_to_text(stored, &text) == 0);
    assert_text(text, "abcd");
    foundation_runtime_bytes_close(&stored);
    encoded_standard = fdn_string_static("YWJjZA=", 7);
    assert(foundation_runtime_base64_decode(&encoded_standard, &stored) == 2);

    assert(foundation_runtime_hmac_sha256(key, message, &digest) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(digest, expected_digest));
    assert(!foundation_runtime_bytes_constant_time_equal(digest, abc));
    assert(foundation_runtime_hmac_sha256(long_key, long_message, &long_digest) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(long_digest, long_expected_digest));

    sha256 = foundation_runtime_sha256_open();
    assert(sha256 != 0);
    assert(foundation_runtime_sha256_update_text(sha256, &sha256_prefix) == 0);
    assert(foundation_runtime_sha256_update_bytes(sha256, sha256_suffix) == 0);
    assert(foundation_runtime_sha256_finish(&sha256, &sha256_digest) == 0);
    assert(sha256 == 0);
    assert(foundation_runtime_bytes_constant_time_equal(
        sha256_digest, sha256_expected_digest));
    assert(foundation_runtime_sha256_finish(&sha256, &stored) == 1);
    closed_sha256 = foundation_runtime_sha256_open();
    assert(foundation_runtime_sha256_update_bytes(closed_sha256, 0) == 1);
    foundation_runtime_sha256_close(&closed_sha256);
    foundation_runtime_sha256_close(&closed_sha256);
    assert(closed_sha256 == 0);
    assert(foundation_runtime_sha256_update_text(closed_sha256,
                                                 &sha256_prefix) == 1);

    assert(foundation_runtime_aes256_gcm_decrypt(aes_key, aes_vector, &empty,
                                                 &aes_decrypted) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(aes_decrypted, aes_plaintext));
    foundation_runtime_bytes_close(&aes_decrypted);
    assert(foundation_runtime_aes256_gcm_decrypt(aes_key, aes_vector, &wrong_aad,
                                                 &aes_decrypted) == 4);
    assert(aes_decrypted == 0);
    assert(foundation_runtime_aes256_gcm_encrypt(aes_key, abc, &aad,
                                                 &aes_ciphertext) == 0);
    assert(!foundation_runtime_bytes_constant_time_equal(aes_ciphertext, abc));
    assert(foundation_runtime_aes256_gcm_decrypt(aes_key, aes_ciphertext, &aad,
                                                 &aes_decrypted) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(aes_decrypted, abc));
    foundation_runtime_bytes_close(&aes_decrypted);
    assert(foundation_runtime_aes256_gcm_decrypt(aes_key, aes_ciphertext, &wrong_aad,
                                                 &aes_decrypted) == 4);

    memory = foundation_runtime_secret_memory_open();
    assert(memory != 0);
    memory_copy = foundation_runtime_secret_memory_retain(memory);
    assert(memory_copy == memory);
    assert(foundation_runtime_secret_memory_set(memory, &secret_key, abc) == 0);
    foundation_runtime_bytes_close(&abc);
    assert(foundation_runtime_secret_memory_get(memory_copy, &secret_key, &stored) == 0);
    assert(foundation_runtime_bytes_to_text(stored, &text) == 0);
    assert_text(text, "abc");
    foundation_runtime_bytes_close(&stored);
    foundation_runtime_secret_memory_close(&memory);
    assert(foundation_runtime_secret_memory_get(memory_copy, &secret_key, &stored) == 0);
    foundation_runtime_bytes_close(&stored);
    assert(foundation_runtime_secret_memory_delete(memory_copy, &secret_key) == 0);
    assert(foundation_runtime_secret_memory_get(memory_copy, &secret_key, &stored) == 2);
    assert(foundation_runtime_secret_memory_delete(memory_copy, &secret_key) == 0);
    assert(foundation_runtime_secret_memory_set(memory_copy, &empty, decoded) == 3);
    foundation_runtime_secret_memory_close(&memory_copy);

    fdn_string_drop(&encoded);
    fdn_string_drop(&text);
    foundation_runtime_bytes_close(&copied);
    foundation_runtime_bytes_close(&built);
    foundation_runtime_bytes_close(&sliced);
    foundation_runtime_bytes_close(&decoded);
    foundation_runtime_bytes_close(&decoded_standard);
    foundation_runtime_bytes_close(&invalid_utf8);
    foundation_runtime_bytes_close(&key);
    foundation_runtime_bytes_close(&message);
    foundation_runtime_bytes_close(&digest);
    foundation_runtime_bytes_close(&expected_digest);
    foundation_runtime_bytes_close(&sha256_digest);
    foundation_runtime_bytes_close(&sha256_expected_digest);
    foundation_runtime_bytes_close(&sha256_suffix);
    foundation_runtime_bytes_close(&long_key);
    foundation_runtime_bytes_close(&long_message);
    foundation_runtime_bytes_close(&long_digest);
    foundation_runtime_bytes_close(&long_expected_digest);
    foundation_runtime_bytes_close(&aes_key);
    foundation_runtime_bytes_close(&aes_plaintext);
    foundation_runtime_bytes_close(&aes_ciphertext);
    foundation_runtime_bytes_close(&aes_decrypted);
    foundation_runtime_bytes_close(&aes_vector);
    assert(fdn_live_allocations() == initial_allocations);
    return 0;
}
