#include "state_store.hpp"

#include "nvs.h"

namespace router_monitor {

bool StateStore::load(const std::string& target_key, StoredState& state) const {
    nvs_handle_t handle{};
    if (nvs_open("router_mon", NVS_READONLY, &handle) != ESP_OK) return false;
    std::size_t length = 0;
    if (nvs_get_str(handle, "target", nullptr, &length) != ESP_OK || length == 0) {
        nvs_close(handle);
        return false;
    }
    std::string stored_target(length, '\0');
    std::int8_t raw_status{};
    std::int64_t updated_at{};
    const bool ok = nvs_get_str(handle, "target", stored_target.data(), &length) == ESP_OK &&
                    nvs_get_i8(handle, "status", &raw_status) == ESP_OK &&
                    nvs_get_i64(handle, "updated", &updated_at) == ESP_OK;
    nvs_close(handle);
    if (!ok) return false;
    if (!stored_target.empty() && stored_target.back() == '\0') stored_target.pop_back();
    if (stored_target != target_key || raw_status < 0 || raw_status > 2) return false;
    state.target_key = stored_target;
    state.status = static_cast<Status>(raw_status);
    state.updated_at = static_cast<std::time_t>(updated_at);
    return true;
}

bool StateStore::save(const std::string& target_key, Status status,
                      std::time_t updated_at, std::string& error) const {
    nvs_handle_t handle{};
    esp_err_t result = nvs_open("router_mon", NVS_READWRITE, &handle);
    if (result == ESP_OK) result = nvs_set_str(handle, "target", target_key.c_str());
    if (result == ESP_OK) result = nvs_set_i8(handle, "status", static_cast<std::int8_t>(status));
    if (result == ESP_OK) result = nvs_set_i64(handle, "updated", static_cast<std::int64_t>(updated_at));
    if (result == ESP_OK) result = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (result != ESP_OK) {
        error = "保存 NVS 状态失败：" + std::string(esp_err_to_name(result));
        return false;
    }
    return true;
}

}  // namespace router_monitor
