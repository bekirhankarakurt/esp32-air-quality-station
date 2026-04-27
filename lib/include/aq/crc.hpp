/// @file crc.hpp
/// @brief CRC-8 (Sensirion) and CRC-32 (IEEE 802.3) checksums.
#pragma once

#include <cstddef>
#include <cstdint>

namespace aq {

/// CRC-8 used by Sensirion sensors: polynomial 0x31, init 0xFF, no reflection, no final XOR.
/// Reference: CRC of {0xBE, 0xEF} is 0x92.
[[nodiscard]] constexpr std::uint8_t crc8_sensirion(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint8_t crc = 0xFFU;
    for (std::size_t i = 0; i < len; ++i) {
        crc = static_cast<std::uint8_t>(crc ^ data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0U ? static_cast<std::uint8_t>((crc << 1U) ^ 0x31U)
                                      : static_cast<std::uint8_t>(crc << 1U);
        }
    }
    return crc;
}

/// CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320, init and final XOR 0xFFFFFFFF).
/// Reference: CRC of "123456789" is 0xCBF43926.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace aq
