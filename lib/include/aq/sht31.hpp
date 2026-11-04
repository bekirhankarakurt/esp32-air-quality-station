/// @file sht31.hpp
/// @brief Sensirion SHT31 temperature / humidity driver (single-shot mode).
#pragma once

#include <array>
#include <cstdint>

#include "aq/i2c_bus.hpp"
#include "aq/sensirion.hpp"

namespace aq {

/// Temperature and relative humidity pair.
struct ThermoReading {
    float temperature_c{0.0F};
    float humidity_pct{0.0F};
};

namespace sht31 {
inline constexpr std::uint8_t kDefaultAddress = 0x44;
inline constexpr std::uint16_t kCmdMeasureHigh = 0x2400;  ///< Single shot, high repeatability, no clock stretching.
inline constexpr std::uint16_t kCmdSoftReset = 0x30A2;
inline constexpr std::uint16_t kCmdClearStatus = 0x3041;
inline constexpr std::uint32_t kMeasureTimeMs = 16;  ///< Datasheet max 15.5 ms at high repeatability.
inline constexpr std::uint32_t kResetTimeMs = 2;     ///< Datasheet max 1.5 ms.
inline constexpr std::size_t kFrameSize = 6;         ///< T word + RH word, each with CRC.
using Frame = std::array<std::uint8_t, kFrameSize>;

/// Validate CRCs and convert a raw measurement frame.
[[nodiscard]] Result<ThermoReading> parse(const Frame& frame) noexcept;
}  // namespace sht31

/// SHT31 driver. Holds a reference to the bus; the bus must outlive the driver.
class Sht31 {
public:
    explicit Sht31(I2cBus& bus, std::uint8_t address = sht31::kDefaultAddress) noexcept
        : bus_(bus), address_(address) {}

    /// Trigger a high-repeatability single-shot measurement and read the result.
    [[nodiscard]] Result<ThermoReading> measure() noexcept;
    /// Issue a soft reset (used for error recovery).
    [[nodiscard]] SensorError soft_reset() noexcept;

    [[nodiscard]] std::uint8_t address() const noexcept { return address_; }

private:
    I2cBus& bus_;
    std::uint8_t address_;
};

}  // namespace aq
