#ifndef FOUNDATION_SHA256_HPP
#define FOUNDATION_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace foundation {

class Sha256 final {
  public:
    Sha256();

    void update(std::span<const std::byte> bytes);
    void update(std::string_view text);
    [[nodiscard]] std::array<std::byte, 32> finish();

  private:
    void transform();

    std::array<std::uint32_t, 8> state_;
    std::array<std::byte, 64> block_{};
    std::size_t blockSize_{};
    std::uint64_t totalBytes_{};
    bool finished_{};
};

[[nodiscard]] std::string sha256Hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256Hex(std::string_view text);

} // namespace foundation

#endif
