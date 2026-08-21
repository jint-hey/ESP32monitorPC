#pragma once

#include "models.hpp"

namespace router_monitor {

class WifiManager {
public:
    bool start(const Config& config, int timeout_seconds);
    bool wait_connected(int timeout_seconds) const;

    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    bool initialized_{false};
};

}  // namespace router_monitor
