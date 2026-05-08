// aq::I2cBus implementation on top of ESP-IDF's i2c_master driver.
#pragma once

#include <array>
#include <cstdint>

#include "aq/i2c_bus.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace fw {

class EspI2cBus final : public aq::I2cBus {
public:
    static constexpr std::size_t kMaxDevices = 4;
    static constexpr int kTimeoutMs = 50;

    EspI2cBus() = default;
    ~EspI2cBus() override;

    /// Create the controller. Internal pull-ups are enabled; use external
    /// 4.7 kOhm pull-ups for cables longer than ~20 cm.
    [[nodiscard]] esp_err_t init(int sda_gpio, int scl_gpio, std::uint32_t freq_hz) noexcept;
    /// Clock out a stuck slave (9 SCL pulses + STOP).
    [[nodiscard]] esp_err_t recover() noexcept;

    [[nodiscard]] aq::I2cStatus write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept override;
    [[nodiscard]] aq::I2cStatus read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept override;
    void delay_ms(std::uint32_t ms) noexcept override;

private:
    struct Device {
        std::uint8_t address{0};
        i2c_master_dev_handle_t handle{nullptr};
    };

    [[nodiscard]] i2c_master_dev_handle_t device(std::uint8_t address) noexcept;
    [[nodiscard]] static aq::I2cStatus map(esp_err_t err) noexcept;

    i2c_master_bus_handle_t bus_{nullptr};
    std::array<Device, kMaxDevices> devices_{};
    std::uint32_t freq_hz_{100000};
};

}  // namespace fw
