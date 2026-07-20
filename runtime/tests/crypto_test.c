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
    static const char long_hmac_message[] =
        "Test Using Larger Than Block-Size Key - Hash Key First";
    const size_t initial_allocations = fdn_live_allocations();
    char long_hmac_key[131];
    uint64_t abc = bytes_from_data("abc", 3);
    uint64_t decoded = 0;
    uint64_t copied = 0;
    uint64_t invalid_utf8 = 0;
    uint64_t key = bytes_from_data(hmac_key, sizeof(hmac_key));
    uint64_t message = bytes_from_data("Hi There", 8);
    uint64_t digest = 0;
    uint64_t expected_digest = bytes_from_data((const char *)hmac_expected, sizeof(hmac_expected));
    uint64_t long_key;
    uint64_t long_message;
    uint64_t long_digest = 0;
    uint64_t long_expected_digest;
    uint64_t length = 0;
    uint64_t byte = 0;
    fdn_string encoded = fdn_string_static("", 0);
    fdn_string text = fdn_string_static("", 0);
    fdn_string invalid = fdn_string_static("Zh", 2);
    fdn_string raw_invalid_utf8 = fdn_string_static("_w", 2);

    (void)memset(long_hmac_key, 0xaa, sizeof(long_hmac_key));
    long_key = bytes_from_data(long_hmac_key, sizeof(long_hmac_key));
    long_message = bytes_from_data(long_hmac_message, sizeof(long_hmac_message) - 1);
    long_expected_digest =
        bytes_from_data((const char *)long_hmac_expected, sizeof(long_hmac_expected));

    assert(foundation_runtime_bytes_length(abc, &length) == 0);
    assert(length == 3);
    assert(foundation_runtime_bytes_at(abc, 1, &byte) == 0);
    assert(byte == 'b');
    assert(foundation_runtime_bytes_at(abc, 3, &byte) == 2);
    assert(foundation_runtime_bytes_copy(abc, &copied) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(abc, copied));

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

    assert(foundation_runtime_hmac_sha256(key, message, &digest) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(digest, expected_digest));
    assert(!foundation_runtime_bytes_constant_time_equal(digest, abc));
    assert(foundation_runtime_hmac_sha256(long_key, long_message, &long_digest) == 0);
    assert(foundation_runtime_bytes_constant_time_equal(long_digest, long_expected_digest));

    fdn_string_drop(&encoded);
    fdn_string_drop(&text);
    foundation_runtime_bytes_close(&abc);
    foundation_runtime_bytes_close(&copied);
    foundation_runtime_bytes_close(&decoded);
    foundation_runtime_bytes_close(&invalid_utf8);
    foundation_runtime_bytes_close(&key);
    foundation_runtime_bytes_close(&message);
    foundation_runtime_bytes_close(&digest);
    foundation_runtime_bytes_close(&expected_digest);
    foundation_runtime_bytes_close(&long_key);
    foundation_runtime_bytes_close(&long_message);
    foundation_runtime_bytes_close(&long_digest);
    foundation_runtime_bytes_close(&long_expected_digest);
    assert(fdn_live_allocations() == initial_allocations);
    return 0;
}
