#include "foundation/sha256.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace foundation {

namespace {

constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t loadWord(const std::byte *bytes) {
    return (std::to_integer<std::uint32_t>(bytes[0]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[3]);
}

void storeWord(std::uint32_t word, std::byte *bytes) {
    bytes[0] = static_cast<std::byte>(word >> 24U);
    bytes[1] = static_cast<std::byte>(word >> 16U);
    bytes[2] = static_cast<std::byte>(word >> 8U);
    bytes[3] = static_cast<std::byte>(word);
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::update(std::span<const std::byte> bytes) {
    if (finished_) {
        throw std::logic_error("cannot update a finished SHA-256 digest");
    }
    constexpr auto maximumBytes = UINT64_MAX / 8U;
    if (totalBytes_ > maximumBytes || bytes.size() > maximumBytes - totalBytes_) {
        throw std::length_error("SHA-256 input is too large");
    }
    totalBytes_ += bytes.size();
    while (!bytes.empty()) {
        const auto count = std::min(block_.size() - blockSize_, bytes.size());
        std::copy_n(bytes.begin(), count, block_.begin() + blockSize_);
        blockSize_ += count;
        bytes = bytes.subspan(count);
        if (blockSize_ == block_.size()) {
            transform();
            blockSize_ = 0;
        }
    }
}

void Sha256::update(std::string_view text) {
    update(std::as_bytes(std::span{text.data(), text.size()}));
}

std::array<std::byte, 32> Sha256::finish() {
    if (finished_) {
        throw std::logic_error("SHA-256 digest was already finished");
    }
    const auto bitCount = totalBytes_ * 8U;
    block_[blockSize_++] = std::byte{0x80};
    if (blockSize_ > 56) {
        std::fill(block_.begin() + blockSize_, block_.end(), std::byte{});
        transform();
        blockSize_ = 0;
    }
    std::fill(block_.begin() + blockSize_, block_.begin() + 56, std::byte{});
    for (std::size_t index = 0; index < 8; ++index) {
        block_[63 - index] = static_cast<std::byte>(bitCount >> (index * 8U));
    }
    transform();
    finished_ = true;

    std::array<std::byte, 32> digest{};
    for (std::size_t index = 0; index < state_.size(); ++index) {
        storeWord(state_[index], digest.data() + index * 4);
    }
    return digest;
}

void Sha256::transform() {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = loadWord(block_.data() + index * 4);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto first = std::rotr(words[index - 15], 7) ^
                           std::rotr(words[index - 15], 18) ^
                           (words[index - 15] >> 3U);
        const auto second = std::rotr(words[index - 2], 17) ^
                            std::rotr(words[index - 2], 19) ^
                            (words[index - 2] >> 10U);
        words[index] = words[index - 16] + first + words[index - 7] + second;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto temporary1 = h + sum1 + choice + constants[index] + words[index];
        const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string sha256Hex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    Sha256 hash;
    hash.update(bytes);
    const auto digest = hash.finish();
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        const auto value = std::to_integer<unsigned int>(byte);
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

std::string sha256Hex(std::string_view text) {
    return sha256Hex(std::as_bytes(std::span{text.data(), text.size()}));
}

} // namespace foundation
