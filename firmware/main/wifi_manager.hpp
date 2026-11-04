// Wi-Fi station with automatic reconnect using capped exponential backoff.
#pragma once

#include <atomic>
#include <cstdint>

#include "aq/backoff.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"

namespace fw {

class WifiManager {
public:
    static constexpr std::uint32_t kInitialBackoffMs = 500;
    static constexpr std::uint32_t kMaxBackoffMs = 60'000;

    WifiManager() = default;
    ~WifiManager();
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;
    WifiManager(WifiManager&&) = delete;
    WifiManager& operator=(WifiManager&&) = delete;

    /// Bring up netif + Wi-Fi in station mode and start connecting.
    [[nodiscard]] esp_err_t start(const char* ssid, const char* password) noexcept;

    [[nodiscard]] bool connected() const noexcept { return connected_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint32_t reconnects() const noexcept { return reconnects_.load(std::memory_order_relaxed); }
    /// RSSI of the current AP in dBm, or 0 when disconnected.
    [[nodiscard]] int rssi() const noexcept;

private:
    static void on_event(void* arg, esp_event_base_t base, std::int32_t id, void* data) noexcept;
    static void on_retry(void* arg) noexcept;

    aq::Backoff backoff_{kInitialBackoffMs, kMaxBackoffMs};
    esp_timer_handle_t retry_timer_{nullptr};
    esp_event_handler_instance_t wifi_handler_{nullptr};
    esp_event_handler_instance_t ip_handler_{nullptr};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint32_t> reconnects_{0};
};

}  // namespace fw
