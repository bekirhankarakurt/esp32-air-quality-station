#include "wifi_manager.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"

namespace fw {
namespace {

constexpr const char* kTag = "wifi";

void copy_field(std::uint8_t* dst, std::size_t cap, const char* src) noexcept {
    const std::size_t n = std::strlen(src);
    std::memcpy(dst, src, n < cap ? n : cap);
}

}  // namespace

WifiManager::~WifiManager() {
    if (wifi_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
    }
    if (ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_);
    }
    if (retry_timer_ != nullptr) {
        esp_timer_stop(retry_timer_);
        esp_timer_delete(retry_timer_);
    }
}

esp_err_t WifiManager::start(const char* ssid, const char* password) noexcept {
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return ESP_FAIL;
    }

    esp_timer_create_args_t timer_args{};
    timer_args.callback = &WifiManager::on_retry;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "wifi_retry";
    if ((err = esp_timer_create(&timer_args, &retry_timer_)) != ESP_OK) {
        return err;
    }

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) {
        return err;
    }
    if ((err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::on_event, this,
                                                   &wifi_handler_)) != ESP_OK) {
        return err;
    }
    if ((err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::on_event, this,
                                                   &ip_handler_)) != ESP_OK) {
        return err;
    }

    wifi_config_t cfg{};
    copy_field(cfg.sta.ssid, sizeof(cfg.sta.ssid), ssid);
    copy_field(cfg.sta.password, sizeof(cfg.sta.password), password);
    cfg.sta.threshold.authmode = std::strlen(password) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;

    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        return err;
    }
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &cfg)) != ESP_OK) {
        return err;
    }
    // Modem sleep between DTIM beacons: large saving, ~100 ms extra latency.
    if ((err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM)) != ESP_OK) {
        return err;
    }
    ESP_LOGI(kTag, "connecting to \"%s\"", ssid);
    return esp_wifi_start();
}

int WifiManager::rssi() const noexcept {
    wifi_ap_record_t ap{};
    return connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

void WifiManager::on_event(void* arg, esp_event_base_t base, std::int32_t id, void* data) noexcept {
    auto* self = static_cast<WifiManager*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->connected_.store(false, std::memory_order_release);
        const auto* ev = static_cast<const wifi_event_sta_disconnected_t*>(data);
        const std::uint32_t delay = self->backoff_.next(esp_random());
        ESP_LOGW(kTag, "disconnected (reason %d), retry %u in %u ms", ev != nullptr ? ev->reason : -1,
                 static_cast<unsigned>(self->backoff_.attempts()), static_cast<unsigned>(delay));
        esp_timer_stop(self->retry_timer_);
        esp_timer_start_once(self->retry_timer_, static_cast<std::uint64_t>(delay) * 1000U);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* ev = static_cast<const ip_event_got_ip_t*>(data);
        ESP_LOGI(kTag, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        self->backoff_.reset();
        self->connected_.store(true, std::memory_order_release);
    }
}

void WifiManager::on_retry(void* arg) noexcept {
    auto* self = static_cast<WifiManager*>(arg);
    self->reconnects_.fetch_add(1, std::memory_order_relaxed);
    esp_wifi_connect();
}

}  // namespace fw
