#include "esp_i2c_bus.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace fw {

EspI2cBus::~EspI2cBus() {
    for (auto& d : devices_) {
        if (d.handle != nullptr) {
            i2c_master_bus_rm_device(d.handle);
        }
    }
    if (bus_ != nullptr) {
        i2c_del_master_bus(bus_);
    }
}

esp_err_t EspI2cBus::init(int sda_gpio, int scl_gpio, std::uint32_t freq_hz) noexcept {
    i2c_master_bus_config_t cfg{};
    cfg.i2c_port = -1;  // auto-select a free controller
    cfg.sda_io_num = static_cast<gpio_num_t>(sda_gpio);
    cfg.scl_io_num = static_cast<gpio_num_t>(scl_gpio);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    freq_hz_ = freq_hz;
    return i2c_new_master_bus(&cfg, &bus_);
}

esp_err_t EspI2cBus::recover() noexcept { return bus_ == nullptr ? ESP_ERR_INVALID_STATE : i2c_master_bus_reset(bus_); }

i2c_master_dev_handle_t EspI2cBus::device(std::uint8_t address) noexcept {
    if (bus_ == nullptr) {
        return nullptr;
    }
    for (auto& d : devices_) {
        if (d.handle != nullptr && d.address == address) {
            return d.handle;
        }
    }
    for (auto& d : devices_) {
        if (d.handle == nullptr) {
            i2c_device_config_t cfg{};
            cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            cfg.device_address = address;
            cfg.scl_speed_hz = freq_hz_;
            if (i2c_master_bus_add_device(bus_, &cfg, &d.handle) != ESP_OK) {
                d.handle = nullptr;
                return nullptr;
            }
            d.address = address;
            return d.handle;
        }
    }
    return nullptr;
}

aq::I2cStatus EspI2cBus::map(esp_err_t err) noexcept {
    switch (err) {
        case ESP_OK: return aq::I2cStatus::Ok;
        case ESP_ERR_TIMEOUT: return aq::I2cStatus::Timeout;
        case ESP_FAIL:
        case ESP_ERR_INVALID_STATE:
        case ESP_ERR_INVALID_RESPONSE: return aq::I2cStatus::Nack;
        default: return aq::I2cStatus::BusError;
    }
}

aq::I2cStatus EspI2cBus::write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(address);
    if (dev == nullptr) {
        return aq::I2cStatus::BusError;
    }
    return map(i2c_master_transmit(dev, data, len, kTimeoutMs));
}

aq::I2cStatus EspI2cBus::read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(address);
    if (dev == nullptr) {
        return aq::I2cStatus::BusError;
    }
    return map(i2c_master_receive(dev, data, len, kTimeoutMs));
}

void EspI2cBus::delay_ms(std::uint32_t ms) noexcept {
    // +1 tick: a delay of N ticks may expire up to one tick early.
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

}  // namespace fw
