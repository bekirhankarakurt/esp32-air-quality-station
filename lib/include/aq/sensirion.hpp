/// @file sensirion.hpp
/// @brief Shared result types and word framing for Sensirion I2C sensors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "aq/crc.hpp"
#include "aq/i2c_bus.hpp"

namespace aq {

/// Error categories reported by the sensor drivers.
enum class SensorError : std::uint8_t {
    None,        ///< Success.
    Bus,         ///< I2C transaction failed (NACK, timeout, bus error).
    Crc,         ///< A received word failed its CRC-8 check.
    NotReady,    ///< Sensor has no new measurement yet.
    OutOfRange,  ///< Value outside the sensor's physical range.
};

/// Human-readable name of a sensor error.
[[nodiscard]] const char* to_string(SensorError e) noexcept;

/// Value-or-error result returned by driver calls (no exceptions, no heap).
template <typename T>
struct Result {
    T value{};
    SensorError error{SensorError::None};
    [[nodiscard]] constexpr bool ok() const noexcept { return error == SensorError::None; }
};

namespace sensirion {

/// Bytes per framed word on the wire: MSB, LSB, CRC-8.
inline constexpr std::size_t kWordSize = 3;

/// Decode a framed word; std::nullopt if its CRC does not match.
[[nodiscard]] constexpr std::optional<std::uint16_t> decode_word(const std::uint8_t* framed) noexcept {
    if (crc8_sensirion(framed, 2) != framed[2]) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(framed[0]) << 8U) | framed[1]);
}

/// Encode @p word as MSB, LSB, CRC-8 into @p out (3 bytes).
constexpr void encode_word(std::uint16_t word, std::uint8_t* out) noexcept {
    out[0] = static_cast<std::uint8_t>(word >> 8U);
    out[1] = static_cast<std::uint8_t>(word & 0xFFU);
    out[2] = crc8_sensirion(out, 2);
}

/// Convert a raw temperature word to degrees Celsius (SHT3x / SCD4x formula).
[[nodiscard]] constexpr float ticks_to_celsius(std::uint16_t raw) noexcept {
    return -45.0F + 175.0F * static_cast<float>(raw) / 65535.0F;
}

/// Convert a raw humidity word to percent relative humidity.
[[nodiscard]] constexpr float ticks_to_rh(std::uint16_t raw) noexcept {
    return 100.0F * static_cast<float>(raw) / 65535.0F;
}

/// Inverse of ticks_to_celsius(), clamped to the 16-bit range (used by the simulator).
[[nodiscard]] std::uint16_t celsius_to_ticks(float celsius) noexcept;
/// Inverse of ticks_to_rh(), clamped to the 16-bit range (used by the simulator).
[[nodiscard]] std::uint16_t rh_to_ticks(float rh) noexcept;

/// Send a 16-bit command, optionally followed by one framed argument word.
[[nodiscard]] SensorError send_command(I2cBus& bus, std::uint8_t address, std::uint16_t command) noexcept;
[[nodiscard]] SensorError send_command(I2cBus& bus, std::uint8_t address, std::uint16_t command,
                                       std::uint16_t argument) noexcept;

/// Map a bus status to a sensor error.
[[nodiscard]] constexpr SensorError from_bus(I2cStatus s) noexcept {
    return s == I2cStatus::Ok ? SensorError::None : SensorError::Bus;
}

}  // namespace sensirion
}  // namespace aq
