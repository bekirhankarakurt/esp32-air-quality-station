/// @file scd41.hpp
/// @brief Sensirion SCD41 photoacoustic CO2 sensor driver (periodic mode).
#pragma once

#include <array>
#include <cstdint>

#include "aq/i2c_bus.hpp"
#include "aq/sensirion.hpp"

namespace aq {

/// One SCD41 measurement. Temperature/RH are from the on-board SHT4x and
/// include module self-heating, so they serve only as a fallback.
struct Co2Reading {
    std::uint16_t co2_ppm{0};
    float temperature_c{0.0F};
    float humidity_pct{0.0F};
};

namespace scd41 {
inline constexpr std::uint8_t kDefaultAddress = 0x62;
inline constexpr std::uint16_t kCmdStartPeriodic = 0x21B1;
inline constexpr std::uint16_t kCmdReadMeasurement = 0xEC05;
inline constexpr std::uint16_t kCmdStopPeriodic = 0x3F86;
inline constexpr std::uint16_t kCmdGetDataReady = 0xE4B8;
inline constexpr std::uint16_t kCmdSetAltitude = 0x2427;
inline constexpr std::uint16_t kCmdReinit = 0x3646;
inline constexpr std::uint32_t kCommandTimeMs = 1;
inline constexpr std::uint32_t kStopTimeMs = 500;
inline constexpr std::uint32_t kReinitTimeMs = 30;
inline constexpr std::uint32_t kMeasurementIntervalMs = 5000;  ///< Periodic mode update rate.
inline constexpr std::uint16_t kMaxCo2Ppm = 40000;             ///< Output range upper bound.
inline constexpr std::size_t kFrameSize = 9;                   ///< CO2, T, RH words with CRC.
using Frame = std::array<std::uint8_t, kFrameSize>;

/// Validate CRCs and convert a raw read_measurement frame.
[[nodiscard]] Result<Co2Reading> parse(const Frame& frame) noexcept;
/// Interpret the get_data_ready_status word: ready if the low 11 bits are non-zero.
[[nodiscard]] constexpr bool is_data_ready(std::uint16_t status) noexcept { return (status & 0x07FFU) != 0U; }
}  // namespace scd41

/// SCD41 driver. Holds a reference to the bus; the bus must outlive the driver.
class Scd41 {
public:
    explicit Scd41(I2cBus& bus, std::uint8_t address = scd41::kDefaultAddress) noexcept
        : bus_(bus), address_(address) {}

    [[nodiscard]] SensorError start_periodic() noexcept;
    /// Stop periodic measurement; blocks for 500 ms as required by the datasheet.
    [[nodiscard]] SensorError stop_periodic() noexcept;
    /// Reload settings from EEPROM (sensor must be idle).
    [[nodiscard]] SensorError reinit() noexcept;
    /// Set altitude compensation in metres (sensor must be idle).
    [[nodiscard]] SensorError set_altitude(std::uint16_t metres) noexcept;
    /// Query whether a new measurement is available.
    [[nodiscard]] Result<bool> data_ready() noexcept;
    /// Read the latest measurement (fails with Bus if none is available).
    [[nodiscard]] Result<Co2Reading> read_measurement() noexcept;

    [[nodiscard]] std::uint8_t address() const noexcept { return address_; }

private:
    I2cBus& bus_;
    std::uint8_t address_;
};

}  // namespace aq
