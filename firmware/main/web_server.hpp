// Built-in HTTP server: dashboard page, live JSON and configuration API.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "shared_state.hpp"

namespace fw {

class WebServer {
public:
    explicit WebServer(SharedState& state) noexcept : state_(state) {}
    ~WebServer();
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    WebServer(WebServer&&) = delete;
    WebServer& operator=(WebServer&&) = delete;

    /// Start on port 80 with GET /, GET /api/live, GET|POST /api/config.
    [[nodiscard]] esp_err_t start() noexcept;

private:
    static esp_err_t handle_index(httpd_req_t* req) noexcept;
    static esp_err_t handle_live(httpd_req_t* req) noexcept;
    static esp_err_t handle_get_config(httpd_req_t* req) noexcept;
    static esp_err_t handle_post_config(httpd_req_t* req) noexcept;

    SharedState& state_;
    httpd_handle_t server_{nullptr};
};

}  // namespace fw
