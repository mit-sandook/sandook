#pragma once

#include <algorithm>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/control_plane/rw_isolation_base.h"

namespace sandook::schedulers::control_plane {

class RWIsolationStrict : public RWIsolationBase {
 public:
  RWIsolationStrict() = default;
  ~RWIsolationStrict() override = default;

  /* No copying. */
  RWIsolationStrict(const RWIsolationStrict &) = delete;
  RWIsolationStrict &operator=(const RWIsolationStrict &) = delete;

  /* No moving. */
  RWIsolationStrict(RWIsolationStrict &&other) = delete;
  RWIsolationStrict &operator=(RWIsolationStrict &&other) = delete;

  /* Reads go to read/mix mode servers, writes go to write/mix mode servers. */
  Status<ServerWeights> ComputeWeights(
      const ServerStatsList &stats, OpType op,
      [[maybe_unused]] const SystemLoad load) override {
    auto num_valid_servers = 0;
    ServerMode filter_mode =
        op == OpType::kRead ? ServerMode::kWrite : ServerMode::kRead;
    std::ranges::for_each(stats, [&](const auto &srv) {
      if (srv.mode != filter_mode) {
        num_valid_servers++;
      }
    });

    ServerWeights weights;
    const auto weight = 1.0 / static_cast<double>(num_valid_servers);
    std::ranges::for_each(stats, [&](const auto &srv) {
      if (srv.mode != filter_mode) {
        weights.at(srv.server_id) = weight;
      }
    });

    return weights;
  }
};

}  // namespace sandook::schedulers::control_plane
