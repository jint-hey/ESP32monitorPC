#include "router_monitor_service.hpp"

#include "config.hpp"
#include "core.hpp"
#include "notifier.hpp"
#include "router_client.hpp"
#include "state_store.hpp"
#include "wifi_manager.hpp"

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

namespace router_monitor {
namespace {

constexpr char TAG[] = "router-monitor";
constexpr char CONFIG_PATH[] = "/config/config.properties";
constexpr std::uint32_t TASK_STACK_SIZE = 16384;
constexpr UBaseType_t TASK_PRIORITY = 3;

bool InitializeNvs() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) result = nvs_flash_init();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

void SynchronizeTime(const std::string& timezone) {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char*>("pool.ntp.org"));
    esp_sntp_init();
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (std::time(nullptr) > 1609459200) return;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

CheckResult CheckOnce(const Config& config, RouterClient& router) {
    CheckResult result;
    result.checked_at = std::time(nullptr);
    std::vector<Device> devices;
    if (!router.get_online_devices(devices, result.detail)) {
        result.status = Status::error;
        return result;
    }
    for (const Device& device : devices) {
        if (device_matches(device, config)) result.matched_devices.push_back(device);
    }
    if (result.matched_devices.empty()) {
        result.status = Status::offline;
        result.detail = "在线设备列表中未找到目标设备";
    } else {
        result.status = Status::online;
        result.detail = "匹配到 " + std::to_string(result.matched_devices.size()) + " 台在线设备";
    }
    return result;
}

void UpdateTransition(const Config& config, const CheckResult& result,
                      bool initial_check, const Notifier& notifier,
                      const StateStore& state_store) {
    StoredState previous;
    const bool has_previous = state_store.load(config.target_key(), previous);
    const bool changed = !has_previous || previous.status != result.status;
    std::string error;
    if (!initial_check && changed &&
        !notifier.send_status(result.status, result.detail, result.checked_at, error)) {
        ESP_LOGE(TAG, "PushPlus status notification failed: %s", error.c_str());
        return;
    }
    if (!state_store.save(config.target_key(), result.status, result.checked_at, error)) {
        ESP_LOGE(TAG, "%s", error.c_str());
    }
}

}  // namespace

esp_err_t RouterMonitorService::Start() {
    if (task_ != nullptr) return ESP_OK;
    const BaseType_t result = xTaskCreate(
        TaskEntry, "router_monitor", TASK_STACK_SIZE, this, TASK_PRIORITY, &task_);
    if (result != pdPASS) {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void RouterMonitorService::TaskEntry(void* context) {
    auto* service = static_cast<RouterMonitorService*>(context);
    service->Run();
    service->task_ = nullptr;
    vTaskDelete(nullptr);
}

void RouterMonitorService::Run() {
    if (!InitializeNvs()) return;
    std::string error;
    if (!mount_config_filesystem(error)) {
        ESP_LOGE(TAG, "%s", error.c_str());
        return;
    }
    Config config;
    if (!load_config(CONFIG_PATH, config, error)) {
        ESP_LOGE(TAG, "Configuration error: %s", error.c_str());
        return;
    }

    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Started on ESP32-S3 cores=%d, PSRAM=%u, free PSRAM=%u",
             chip.cores, static_cast<unsigned>(esp_psram_get_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    WifiManager wifi;
    if (!wifi.start(config, 60)) {
        if (!wifi.initialized()) return;
        while (!wifi.wait_connected(30)) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    SynchronizeTime(config.timezone);

    RouterClient router(config);
    Notifier notifier(config);
    StateStore state_store;

    {
        CheckResult initial_result = CheckOnce(config, router);
        status_store_.Update(config, initial_result);
        UpdateTransition(config, initial_result, true, notifier, state_store);
        if (!notifier.send_lifecycle("ESP32 PC监控与路由监控程序现在开始运行",
                                     &initial_result, std::time(nullptr), error)) {
            ESP_LOGE(TAG, "Lifecycle notification failed: %s", error.c_str());
        }
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(config.query_interval_seconds * 1000));
        CheckResult current;
        if (!wifi.wait_connected(config.request_timeout_seconds)) {
            current.status = Status::error;
            current.checked_at = std::time(nullptr);
            current.detail = "Wi-Fi 未连接";
        } else {
            current = CheckOnce(config, router);
        }
        status_store_.Update(config, current);
        UpdateTransition(config, current, false, notifier, state_store);
    }
}

}  // namespace router_monitor
