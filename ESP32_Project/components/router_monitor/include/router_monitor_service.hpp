#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "router_monitor_status.hpp"

namespace router_monitor {

class RouterMonitorService {
public:
    explicit RouterMonitorService(RouterMonitorStatusStore& status_store)
        : status_store_(status_store) {}

    esp_err_t Start();

private:
    static void TaskEntry(void* context);
    void Run();

    RouterMonitorStatusStore& status_store_;
    TaskHandle_t task_{nullptr};
};

}  // namespace router_monitor
