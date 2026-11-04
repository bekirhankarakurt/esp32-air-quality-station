#include "web_server.hpp"

#include <array>
#include <cstdio>

#include "config_store.hpp"
#include "esp_log.h"
#include "web_page.hpp"

namespace fw {
namespace {

constexpr const char* kTag = "web";
constexpr std::size_t kMaxBody = 256;

SharedState& state_of(httpd_req_t* req) noexcept { return *static_cast<SharedState*>(req->user_ctx); }

}  // namespace

WebServer::~WebServer() {
    if (server_ != nullptr) {
        httpd_stop(server_);
    }
}

esp_err_t WebServer::start() noexcept {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    cfg.lru_purge_enable = true;
    if (const esp_err_t err = httpd_start(&server_, &cfg); err != ESP_OK) {
        return err;
    }
    std::array<httpd_uri_t, 4> uris{};
    uris[0].uri = "/";
    uris[0].method = HTTP_GET;
    uris[0].handler = &WebServer::handle_index;
    uris[1].uri = "/api/live";
    uris[1].method = HTTP_GET;
    uris[1].handler = &WebServer::handle_live;
    uris[2].uri = "/api/config";
    uris[2].method = HTTP_GET;
    uris[2].handler = &WebServer::handle_get_config;
    uris[3].uri = "/api/config";
    uris[3].method = HTTP_POST;
    uris[3].handler = &WebServer::handle_post_config;
    for (auto& u : uris) {
        u.user_ctx = &state_;
        if (const esp_err_t err = httpd_register_uri_handler(server_, &u); err != ESP_OK) {
            return err;
        }
    }
    ESP_LOGI(kTag, "dashboard on port %u", static_cast<unsigned>(cfg.server_port));
    return ESP_OK;
}

esp_err_t WebServer::handle_index(httpd_req_t* req) noexcept {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t WebServer::handle_live(httpd_req_t* req) noexcept {
    aq::Reading r{};
    Diagnostics d{};
    if (!state_of(req).latest(r, d)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "no sample yet");
    }
    // Reading JSON with the closing brace replaced by diagnostics fields.
    std::array<char, aq::kJsonBufferSize + 128> buf{};
    std::size_t n = aq::encode_json(r, buf.data(), aq::kJsonBufferSize);
    if (n < 2) {
        return httpd_resp_send_500(req);
    }
    n -= 2;  // drop "}\n"
    const int extra = std::snprintf(buf.data() + n, buf.size() - n,
                                    ",\"rssi_dbm\":%d,\"wifi\":%s,\"udp_sent\":%u,\"udp_failed\":%u,"
                                    "\"wifi_reconnects\":%u,\"bus_recoveries\":%u}",
                                    d.rssi_dbm, d.wifi_connected ? "true" : "false", static_cast<unsigned>(d.udp_sent),
                                    static_cast<unsigned>(d.udp_failed), static_cast<unsigned>(d.wifi_reconnects),
                                    static_cast<unsigned>(d.bus_recoveries));
    if (extra < 0 || static_cast<std::size_t>(extra) >= buf.size() - n) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf.data(), static_cast<ssize_t>(n + static_cast<std::size_t>(extra)));
}

esp_err_t WebServer::handle_get_config(httpd_req_t* req) noexcept {
    const aq::StationConfig c = state_of(req).config();
    std::array<char, 16> ip{};
    static_cast<void>(aq::format_ipv4(c.target_ip, ip.data(), ip.size()));
    std::array<char, 192> buf{};
    const int n = std::snprintf(buf.data(), buf.size(),
                                "{\"ip\":\"%s\",\"port\":%u,\"period_ms\":%u,\"format\":\"%s\",\"altitude_m\":%u,"
                                "\"window\":%u}",
                                ip.data(), static_cast<unsigned>(c.target_port),
                                static_cast<unsigned>(c.sample_period_ms), aq::to_string(c.format),
                                static_cast<unsigned>(c.altitude_m), static_cast<unsigned>(c.filter_window));
    if (n < 0 || static_cast<std::size_t>(n) >= buf.size()) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf.data(), n);
}

esp_err_t WebServer::handle_post_config(httpd_req_t* req) noexcept {
    if (req->content_len > kMaxBody) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "body too large");
    }
    std::array<char, kMaxBody> body{};
    std::size_t received = 0;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (n <= 0) {
            return n == HTTPD_SOCK_ERR_TIMEOUT ? httpd_resp_send_408(req) : ESP_FAIL;
        }
        received += static_cast<std::size_t>(n);
    }
    SharedState& state = state_of(req);
    aq::StationConfig cfg = state.config();
    const aq::ConfigError err = aq::apply_form(std::string_view(body.data(), received), cfg);
    if (err != aq::ConfigError::None) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, aq::to_string(err));
    }
    if (const esp_err_t e = ConfigStore::save(cfg); e != ESP_OK) {
        ESP_LOGE(kTag, "saving configuration failed: %s", esp_err_to_name(e));
        return httpd_resp_send_500(req);
    }
    state.set_config(cfg);
    ESP_LOGI(kTag, "configuration updated");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

}  // namespace fw
