#pragma once

#include <string>

#include "esp_err.h"

namespace router_monitor {

struct HttpResponse {
    esp_err_t transport_error{ESP_OK};
    int status_code{};
    std::string body;

    [[nodiscard]] bool transport_ok() const { return transport_error == ESP_OK; }
    [[nodiscard]] bool http_ok() const {
        return transport_ok() && status_code >= 200 && status_code < 300;
    }
};

class HttpClient {
public:
    explicit HttpClient(int timeout_seconds) : timeout_seconds_(timeout_seconds) {}
    HttpResponse post_json(const std::string& url, const std::string& json) const;

private:
    int timeout_seconds_;
};

}  // namespace router_monitor
