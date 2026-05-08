#include "config_store.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

namespace fw {
namespace {

constexpr const char* kTag = "config";
constexpr const char* kNamespace = "aq";
constexpr const char* kKey = "station";

}  // namespace

esp_err_t ConfigStore::init_flash() noexcept {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "erasing NVS partition (%s)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    return err;
}

aq::StationConfig ConfigStore::load(const aq::StationConfig& defaults) noexcept {
    NvsHandle nvs(kNamespace, NVS_READONLY);
    if (nvs.error() != ESP_OK) {
        ESP_LOGI(kTag, "no stored configuration, using menuconfig defaults");
        return defaults;
    }
    aq::ConfigBlob blob{};
    std::size_t len = blob.size();
    if (nvs_get_blob(nvs.get(), kKey, blob.data(), &len) != ESP_OK) {
        return defaults;
    }
    const auto cfg = aq::deserialize(blob.data(), len);
    if (!cfg) {
        ESP_LOGW(kTag, "stored configuration is corrupt, using defaults");
        return defaults;
    }
    return *cfg;
}

esp_err_t ConfigStore::save(const aq::StationConfig& config) noexcept {
    NvsHandle nvs(kNamespace, NVS_READWRITE);
    if (nvs.error() != ESP_OK) {
        return nvs.error();
    }
    const aq::ConfigBlob blob = aq::serialize(config);
    const esp_err_t err = nvs_set_blob(nvs.get(), kKey, blob.data(), blob.size());
    return err == ESP_OK ? nvs_commit(nvs.get()) : err;
}

}  // namespace fw
