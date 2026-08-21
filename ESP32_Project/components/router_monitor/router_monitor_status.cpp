#include "router_monitor_status.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace router_monitor {
namespace {

template <std::size_t Size>
void CopyText(std::array<char, Size>& destination, const std::string& source) {
    const std::size_t length = std::min(source.size(), Size - 1);
    std::memcpy(destination.data(), source.data(), length);
    destination[length] = '\0';
}

bool IsOledFriendly(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char value) {
        return value >= 32U && value <= 126U;
    });
}

}  // namespace

RouterMonitorStatusStore::RouterMonitorStatusStore() {
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

void RouterMonitorStatusStore::Update(const Config& config, const CheckResult& result) {
    RouterMonitorSnapshot next;
    next.status = result.status;
    next.has_result = true;
    CopyText(next.target, config.target_value);
    if (!result.matched_devices.empty()) {
        const Device& device = result.matched_devices.front();
        const std::string display_name = IsOledFriendly(device.name)
                                             ? device.name
                                             : (!device.hostname.empty()
                                                    ? device.hostname
                                                    : (!device.name.empty()
                                                           ? device.name : config.target_value));
        CopyText(next.device_name, display_name);
        CopyText(next.mac, device.mac);
        CopyText(next.ip, device.ip);
    } else if (result.status == Status::offline) {
        CopyText(next.device_name, "NOT FOUND");
    } else {
        CopyText(next.device_name, result.detail);
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    next.update_count = state_.update_count + 1U;
    state_ = next;
    xSemaphoreGive(mutex_);
}

void RouterMonitorStatusStore::UpdateError(const std::string& detail,
                                           const std::string& target) {
    RouterMonitorSnapshot next;
    next.status = Status::error;
    next.has_result = true;
    CopyText(next.target, target);
    CopyText(next.device_name, detail);

    xSemaphoreTake(mutex_, portMAX_DELAY);
    next.update_count = state_.update_count + 1U;
    state_ = next;
    xSemaphoreGive(mutex_);
}

RouterMonitorSnapshot RouterMonitorStatusStore::Copy() const {
    RouterMonitorSnapshot snapshot;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot = state_;
    xSemaphoreGive(mutex_);
    return snapshot;
}

}  // namespace router_monitor
