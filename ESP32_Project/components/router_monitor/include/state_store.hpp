#pragma once

#include "models.hpp"

#include <string>

namespace router_monitor {

class StateStore {
public:
    bool load(const std::string& target_key, StoredState& state) const;
    bool save(const std::string& target_key, Status status,
              std::time_t updated_at, std::string& error) const;
};

}  // namespace router_monitor
