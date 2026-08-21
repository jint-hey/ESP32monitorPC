#include "wifi_manager.hpp"

#include <algorithm>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace router_monitor {

static constexpr EventBits_t connected_bit = BIT0;
static EventGroupHandle_t wifi_events;
static const char* tag = "wifi";

static void wifi_event_handler(void*, esp_event_base_t event_base,
                               std::int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, connected_bit);
        ESP_LOGW(tag, "Wi-Fi disconnected, reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(tag, "IPv4 acquired: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, connected_bit);
    }
}

bool WifiManager::start(const Config& config, int timeout_seconds) {
    wifi_events = xEventGroupCreate();
    if (wifi_events == nullptr) return false;
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK ||
        esp_netif_create_default_wifi_sta() == nullptr) return false;
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&initialization) != ESP_OK) return false;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            wifi_event_handler, nullptr, nullptr) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            wifi_event_handler, nullptr, nullptr) != ESP_OK) return false;

    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
                 config.wifi_ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
                 config.wifi_password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = config.wifi_password.empty()
                                             ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK ||
        esp_wifi_start() != ESP_OK) return false;
    initialized_ = true;
    const esp_err_t power_save_result = esp_wifi_set_ps(WIFI_PS_NONE);
    if (power_save_result != ESP_OK) {
        ESP_LOGW(tag, "Failed to disable Wi-Fi power saving: %s",
                 esp_err_to_name(power_save_result));
    }
    ESP_LOGI(tag, "Connecting to Wi-Fi SSID: %s", config.wifi_ssid.c_str());
    return wait_connected(timeout_seconds);
}

bool WifiManager::wait_connected(int timeout_seconds) const {
    if (wifi_events == nullptr) return false;
    const EventBits_t bits = xEventGroupWaitBits(
        wifi_events, connected_bit, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(std::max(timeout_seconds, 1) * 1000));
    return (bits & connected_bit) != 0;
}

}  // namespace router_monitor
