#include "foundation/sha256.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures{};

void expectEqual(std::string_view value, std::string_view expected,
                 std::string_view message) {
    if (value == expected) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

} // namespace

int runSha256Tests() {
    expectEqual(foundation::sha256Hex(""),
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                "empty SHA-256 vector");
    expectEqual(foundation::sha256Hex("abc"),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "short SHA-256 vector");
    expectEqual(foundation::sha256Hex(
                    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                "multi-block SHA-256 vector");

    foundation::Sha256 incremental;
    incremental.update("a");
    incremental.update("b");
    incremental.update("c");
    const auto digest = incremental.finish();
    expectEqual(foundation::sha256Hex(std::span{digest}),
                "4f8b42c22dd3729b519ba6f68d2da7cc5b2d606d05daed5ad5128cc03e6c6358",
                "incremental digest bytes are stable");
    return failures;
}
