// ESP32 air-quality station: SHT31 + SCD41 over I2C -> filters/AQI -> UDP + web page.

#include <cstdint>

#include "aq/config.hpp"
#include "aq/station.hpp"
#include "config_store.hpp"
#include "esp_i2c_bus.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "shared_state.hpp"
#include "udp_publisher.hpp"
#include "web_server.hpp"
#include "wifi_manager.hpp"

namespace fw {
namespace {

constexpr const char* kTag = "station";
constexpr std::uint32_t kLogEvery = 12;  // log one sample per minute at the default 5 s period

aq::StationConfig menuconfig_defaults() noexcept {
    aq::StationConfig c{};
    if (const auto ip = aq::parse_ipv4(CONFIG_AQ_TARGET_IP)) {
        c.target_ip = *ip;
    }
    c.target_port = CONFIG_AQ_TARGET_PORT;
    c.sample_period_ms = CONFIG_AQ_SAMPLE_PERIOD_MS;
#if defined(CONFIG_AQ_FORMAT_JSON)
    c.format = aq::TelemetryFormat::Json;
#elif defined(CONFIG_AQ_FORMAT_BOTH)
    c.format = aq::TelemetryFormat::Both;
#else
    c.format = aq::TelemetryFormat::Binary;
#endif
    c.altitude_m = CONFIG_AQ_ALTITUDE_M;
    c.filter_window = CONFIG_AQ_FILTER_WINDOW;
    return c;
}

std::uint32_t device_id() noexcept {
    std::uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return 0;
    }
    return (static_cast<std::uint32_t>(mac[2]) << 24U) | (static_cast<std::uint32_t>(mac[3]) << 16U) |
           (static_cast<std::uint32_t>(mac[4]) << 8U) | mac[5];
}

aq::StationSettings settings_for(const aq::StationConfig& c, std::uint32_t id) noexcept {
    aq::StationSettings s{};
    s.device_id = id;
    s.filter_window = c.filter_window;
    s.altitude_m = c.altitude_m;
    return s;
}

/// Owns every subsystem; constructed once with static storage duration.
class App {
public:
    App() noexcept : station_(bus_, aq::StationSettings{}) {}

    void run() noexcept {
        ESP_ERROR_CHECK(ConfigStore::init_flash());
        config_ = ConfigStore::load(menuconfig_defaults());
        state_.set_config(config_);
        static_cast<void>(state_.take_config_change(config_));
        id_ = device_id();

        ESP_ERROR_CHECK(bus_.init(CONFIG_AQ_I2C_SDA_GPIO, CONFIG_AQ_I2C_SCL_GPIO, CONFIG_AQ_I2C_FREQ_HZ));
        if (const auto err = station_.reconfigure(settings_for(config_, id_)); err != aq::SensorError::None) {
            ESP_LOGW(kTag, "SCD41 start failed (%s); recovery will retry", aq::to_string(err));
        }

        ESP_ERROR_CHECK(wifi_.start(CONFIG_AQ_WIFI_SSID, CONFIG_AQ_WIFI_PASSWORD));
        if (!udp_.open()) {
            ESP_LOGE(kTag, "UDP socket unavailable; telemetry disabled");
        }
        udp_.set_target(config_.target_ip, config_.target_port);
#if CONFIG_AQ_WEB_SERVER
        if (const esp_err_t err = web_.start(); err != ESP_OK) {
            ESP_LOGE(kTag, "web server failed: %s", esp_err_to_name(err));
        }
#endif
        loop();
    }

private:
    void apply_config_change() noexcept {
        aq::StationConfig next{};
        if (!state_.take_config_change(next)) {
            return;
        }
        const bool sensor_change =
            next.altitude_m != config_.altitude_m || next.filter_window != config_.filter_window;
        config_ = next;
        udp_.set_target(config_.target_ip, config_.target_port);
        if (sensor_change) {
            static_cast<void>(station_.reconfigure(settings_for(config_, id_)));
        }
        ESP_LOGI(kTag, "applied new configuration (period %u ms, format %s)",
                 static_cast<unsigned>(config_.sample_period_ms), aq::to_string(config_.format));
    }

    void recover_bus_if_stuck() noexcept {
        // Both sensors failing together points at the bus, not a sensor.
        if (station_.sht31_health().consecutive >= aq::Station::kRecoveryThreshold &&
            station_.scd41_health().consecutive >= aq::Station::kRecoveryThreshold &&
            station_.sht31_health().consecutive % aq::Station::kRecoveryThreshold == 0) {
            ESP_LOGW(kTag, "both sensors failing, resetting I2C bus");
            if (bus_.recover() == ESP_OK) {
                ++diag_.bus_recoveries;
            }
        }
    }

    [[noreturn]] void loop() noexcept {
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            apply_config_change();
            const auto now_ns = static_cast<std::uint64_t>(esp_timer_get_time()) * 1000U;
            const aq::Reading r = station_.sample(now_ns);
            recover_bus_if_stuck();

            diag_.wifi_connected = wifi_.connected();
            diag_.wifi_reconnects = wifi_.reconnects();
            diag_.rssi_dbm = wifi_.rssi();
            if (diag_.wifi_connected) {
                const std::uint32_t failed = udp_.publish(r, config_.format);
                const std::uint32_t datagrams = config_.format == aq::TelemetryFormat::Both ? 2U : 1U;
                diag_.udp_sent += datagrams - failed;
                diag_.udp_failed += failed;
            }
            state_.publish(r, diag_);

            if (r.seq % kLogEvery == 0) {
                ESP_LOGI(kTag, "#%llu T=%.2fC RH=%.1f%% CO2=%u AQI=%u (%s) flags=0x%02x errors=%u",
                         static_cast<unsigned long long>(r.seq), static_cast<double>(r.temperature_c),
                         static_cast<double>(r.humidity_pct), static_cast<unsigned>(r.co2_ppm),
                         static_cast<unsigned>(r.aqi), aq::to_string(r.level), static_cast<unsigned>(r.flags),
                         static_cast<unsigned>(r.error_count));
            }
            xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(config_.sample_period_ms));
        }
    }

    EspI2cBus bus_;
    aq::Station station_;
    WifiManager wifi_;
    UdpPublisher udp_;
    SharedState state_;
    WebServer web_{state_};
    aq::StationConfig config_{};
    Diagnostics diag_{};
    std::uint32_t id_{0};
};

App g_app;

}  // namespace
}  // namespace fw

extern "C" void app_main(void) { fw::g_app.run(); }
