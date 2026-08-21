#pragma once

#include "models.hpp"

#include <string>

namespace router_monitor {

bool mount_config_filesystem(std::string& error);
bool load_config(const char* path, Config& config, std::string& error);

}  // namespace router_monitor
