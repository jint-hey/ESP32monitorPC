#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace router_monitor {

enum class Status : std::int8_t {
    online = 0,
    offline = 1,
    error = 2,
};

struct Config {
    std::string wifi_ssid;
    std::string wifi_password;
    std::string router_url;
    std::string router_password;
    std::string target_type;
    std::string target_value;
    int query_interval_seconds{60};
    std::vector<std::string> pushplus_tokens;
    int request_timeout_seconds{10};
    std::string timezone{"CST-8"};

    [[nodiscard]] std::string target_key() const;
};

struct Device {
    std::string mac;
    std::string name;
    std::string hostname;
    std::string ip;
};

struct CheckResult {
    Status status{Status::error};
    std::string detail;
    std::time_t checked_at{};
    std::vector<Device> matched_devices;
    std::string notification_error;
    std::string state_error;
};

struct StoredState {
    std::string target_key;
    Status status{Status::error};
    std::time_t updated_at{};
};

}  // namespace router_monitor
