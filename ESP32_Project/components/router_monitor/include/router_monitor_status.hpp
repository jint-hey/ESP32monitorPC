#pragma once

#include "models.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace router_monitor {

inline constexpr std::size_t STATUS_TARGET_LENGTH = 64;
inline constexpr std::size_t STATUS_DEVICE_NAME_LENGTH = 64;
inline constexpr std::size_t STATUS_IP_LENGTH = 45;

struct RouterMonitorSnapshot {
    Status status{Status::error};
    std::array<char, STATUS_TARGET_LENGTH + 1> target{};
    std::array<char, STATUS_DEVICE_NAME_LENGTH + 1> device_name{};
    std::array<char, 18> mac{};
    std::array<char, STATUS_IP_LENGTH + 1> ip{};
    std::uint32_t update_count{};
    bool has_result{};
};

class RouterMonitorStatusStore {
public:
    RouterMonitorStatusStore();
    RouterMonitorStatusStore(const RouterMonitorStatusStore&) = delete;
    RouterMonitorStatusStore& operator=(const RouterMonitorStatusStore&) = delete;

    void Update(const Config& config, const CheckResult& result);
    RouterMonitorSnapshot Copy() const;

private:
    mutable SemaphoreHandle_t mutex_{nullptr};
    RouterMonitorSnapshot state_{};
};

}  // namespace router_monitor
