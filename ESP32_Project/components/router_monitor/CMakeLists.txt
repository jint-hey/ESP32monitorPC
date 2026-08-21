idf_component_register(
    SRCS
        "config.cpp"
        "core.cpp"
        "http_client.cpp"
        "notifier.cpp"
        "router_client.cpp"
        "router_monitor_service.cpp"
        "router_monitor_status.cpp"
        "state_store.cpp"
        "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_event
        esp_http_client
        esp_netif
        esp_psram
        esp_wifi
        json
        lwip
        mbedtls
        nvs_flash
        spiffs
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
