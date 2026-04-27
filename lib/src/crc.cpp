#include "aq/crc.hpp"

#include <array>

namespace aq {
namespace {

constexpr std::array<std::uint32_t, 256> make_crc32_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
        }
        table[i] = c;
    }
    return table;
}

constexpr auto kCrc32Table = make_crc32_table();

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        c = kCrc32Table[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

}  // namespace aq
