#pragma once

#include "http_client.hpp"
#include "models.hpp"

#include <string>
#include <vector>

namespace router_monitor {

class RouterClient {
public:
    explicit RouterClient(const Config& config);

    bool get_online_devices(std::vector<Device>& devices, std::string& error);
    void logout();

private:
    bool login(std::string& error);
    bool query_online_hosts(std::string& response_body, std::string& error);

    std::string router_url_;
    std::string encoded_password_;
    std::string stok_;
    HttpClient http_;
};

}  // namespace router_monitor
