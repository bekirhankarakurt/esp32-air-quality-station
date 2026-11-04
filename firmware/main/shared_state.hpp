// State shared between the sampling loop and the HTTP server task.
#pragma once

#include <cstdint>
#include <mutex>

#include "aq/config.hpp"
#include "aq/telemetry.hpp"

namespace fw {

struct Diagnostics {
    std::uint32_t udp_sent{0};
    std::uint32_t udp_failed{0};
    std::uint32_t wifi_reconnects{0};
    std::uint32_t bus_recoveries{0};
    int rssi_dbm{0};
    bool wifi_connected{false};
};

class SharedState {
public:
    void publish(const aq::Reading& r, const Diagnostics& d) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        latest_ = r;
        diag_ = d;
        have_reading_ = true;
    }

    /// Copy out the latest sample; false before the first one.
    [[nodiscard]] bool latest(aq::Reading& r, Diagnostics& d) const noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        r = latest_;
        d = diag_;
        return have_reading_;
    }

    [[nodiscard]] aq::StationConfig config() const noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    /// Store a new configuration and flag it for the sampling loop.
    void set_config(const aq::StationConfig& c) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        config_ = c;
        config_changed_ = true;
    }

    /// Fetch the configuration if it changed since the last call.
    [[nodiscard]] bool take_config_change(aq::StationConfig& out) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!config_changed_) {
            return false;
        }
        config_changed_ = false;
        out = config_;
        return true;
    }

private:
    mutable std::mutex mutex_;
    aq::Reading latest_{};
    Diagnostics diag_{};
    aq::StationConfig config_{};
    bool have_reading_{false};
    bool config_changed_{false};
};

}  // namespace fw
