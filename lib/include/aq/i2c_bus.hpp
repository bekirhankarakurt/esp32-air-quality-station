/// @file i2c_bus.hpp
/// @brief Hardware-agnostic I2C bus interface used by the sensor drivers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace aq {

/// Outcome of a single I2C transaction.
enum class I2cStatus : std::uint8_t {
    Ok,        ///< Transfer completed and was acknowledged.
    Nack,      ///< Target did not acknowledge (absent, busy or no data ready).
    Timeout,   ///< Transfer did not complete in time (e.g. SCL held low).
    BusError,  ///< Arbitration loss or other controller error.
};

/// Abstract I2C controller. The firmware implements it on top of ESP-IDF's
/// i2c_master driver; tests and the host simulator use an in-memory bus.
class I2cBus {
public:
    virtual ~I2cBus() = default;
    I2cBus(const I2cBus&) = delete;
    I2cBus& operator=(const I2cBus&) = delete;
    I2cBus(I2cBus&&) = delete;
    I2cBus& operator=(I2cBus&&) = delete;

    /// Write @p len bytes to the 7-bit @p address.
    [[nodiscard]] virtual I2cStatus write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept = 0;
    /// Read @p len bytes from the 7-bit @p address.
    [[nodiscard]] virtual I2cStatus read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept = 0;
    /// Block for at least @p ms milliseconds (sensor conversion time).
    virtual void delay_ms(std::uint32_t ms) noexcept = 0;

protected:
    I2cBus() = default;
};

}  // namespace aq
