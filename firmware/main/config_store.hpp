// NVS persistence for aq::StationConfig (versioned, CRC-protected blob).
#pragma once

#include "aq/config.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace fw {

/// RAII NVS namespace handle.
class NvsHandle {
public:
    NvsHandle(const char* ns, nvs_open_mode_t mode) noexcept : err_(nvs_open(ns, mode, &handle_)) {}
    ~NvsHandle() {
        if (err_ == ESP_OK) {
            nvs_close(handle_);
        }
    }
    NvsHandle(const NvsHandle&) = delete;
    NvsHandle& operator=(const NvsHandle&) = delete;
    NvsHandle(NvsHandle&&) = delete;
    NvsHandle& operator=(NvsHandle&&) = delete;

    [[nodiscard]] esp_err_t error() const noexcept { return err_; }
    [[nodiscard]] nvs_handle_t get() const noexcept { return handle_; }

private:
    nvs_handle_t handle_{0};
    esp_err_t err_;
};

class ConfigStore {
public:
    /// Initialise the NVS partition, erasing it if its layout is outdated.
    [[nodiscard]] static esp_err_t init_flash() noexcept;
    /// Stored configuration, or @p defaults if none / invalid.
    [[nodiscard]] static aq::StationConfig load(const aq::StationConfig& defaults) noexcept;
    [[nodiscard]] static esp_err_t save(const aq::StationConfig& config) noexcept;
};

}  // namespace fw
