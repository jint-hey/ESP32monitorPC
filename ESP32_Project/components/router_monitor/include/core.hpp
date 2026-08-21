#pragma once

#include "models.hpp"

#include <string>
#include <vector>

namespace router_monitor {

std::string trim_copy(const std::string& value);
std::string ascii_lower_copy(const std::string& value);
bool ascii_case_equal(const std::string& left, const std::string& right);
bool normalize_mac(const std::string& input, std::string& output);
bool device_matches(const Device& device, const Config& config);
std::string encode_router_password(const std::string& password);
std::string percent_decode(const std::string& value);
std::string percent_encode_path(const std::string& value);
std::vector<std::string> split_tokens(const std::string& value);
const char* status_name(Status status);
std::string format_timestamp(std::time_t value);

}  // namespace router_monitor
