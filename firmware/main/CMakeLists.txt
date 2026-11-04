idf_component_register(
  SRCS "main.cpp"
       "config_store.cpp"
       "esp_i2c_bus.cpp"
       "udp_publisher.cpp"
       "web_server.cpp"
       "wifi_manager.cpp"
  INCLUDE_DIRS "."
  REQUIRES aqcore driver esp_event esp_http_server esp_netif esp_timer esp_wifi lwip nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Wextra -Werror)
