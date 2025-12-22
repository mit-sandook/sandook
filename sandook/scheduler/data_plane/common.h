#pragma once

#include <algorithm>
#include <ranges>

#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"

namespace sandook::schedulers::data_plane {

inline ServerSet GetValidServers(const ServerWeights &weights) {
  ServerSet valid_servers;
  for (size_t i = kInvalidServerID + 1; i < weights.size(); i++) {
    if (weights.at(i) != kInvalidServerWeight) {
      valid_servers.insert(i);
    }
  }
  return valid_servers;
}

}  // namespace sandook::schedulers::data_plane
