#pragma once

#include "http_client.hpp"
#include "models.hpp"

#include <string>

namespace router_monitor {

class Notifier {
public:
    explicit Notifier(const Config& config);

    bool send_status(Status status, const std::string& detail,
                     std::time_t checked_at, std::string& error) const;
    bool send_lifecycle(const std::string& message, const CheckResult* result,
                        std::time_t occurred_at, std::string& error) const;

private:
    bool send_to_all(const std::string& title, const std::string& content,
                     std::string& error) const;
    bool send_one(const std::string& token, const std::string& title,
                  const std::string& content, std::string& error) const;

    const Config& config_;
    HttpClient http_;
};

}  // namespace router_monitor
