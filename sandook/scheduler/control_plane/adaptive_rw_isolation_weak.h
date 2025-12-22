#pragma once

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/control_plane/adaptive_rw_isolation_base.h"

namespace sandook::schedulers::control_plane {

class ProfileGuidedAdaptiveRWIsolation;

class AdaptiveRWIsolationWeak : public AdaptiveRWIsolationBase {
 public:
  AdaptiveRWIsolationWeak() = default;
  ~AdaptiveRWIsolationWeak() override = default;

  /* No copying. */
  AdaptiveRWIsolationWeak(const AdaptiveRWIsolationWeak &) = delete;
  AdaptiveRWIsolationWeak &operator=(const AdaptiveRWIsolationWeak &) = delete;

  /* No moving. */
  AdaptiveRWIsolationWeak(AdaptiveRWIsolationWeak &&other) = delete;
  AdaptiveRWIsolationWeak &operator=(AdaptiveRWIsolationWeak &&other) = delete;

  Status<ServerWeights> ComputeWeights(const ServerStatsList &stats, OpType op,
                                       const SystemLoad load) override {
    switch (op) {
      case OpType::kRead:
        return ComputeReadWeights(stats, load);

      case OpType::kWrite:
        return ComputeWriteWeights(stats, load);

      default:
        throw std::runtime_error("Invalid op for computing weights");
    }

    std::unreachable();
  }

 private:
  /* Reads can be performed from any server. */
  static Status<ServerWeights> ComputeReadWeights(
      const ServerStatsList &stats, [[maybe_unused]] const SystemLoad load) {
    const auto num_valid_servers = stats.size();
    const auto weight = 1.0 / static_cast<double>(num_valid_servers);

    ServerWeights weights;
    std::ranges::for_each(
        stats, [&](const auto &srv) { weights.at(srv.server_id) = weight; });

    return weights;
  }

  /* Writes only go to write mode servers. */
  static Status<ServerWeights> ComputeWriteWeights(
      const ServerStatsList &stats, [[maybe_unused]] const SystemLoad load) {
    auto num_valid_servers = 0;
    std::ranges::for_each(stats, [&](const auto &srv) {
      if (srv.mode != ServerMode::kRead) {
        num_valid_servers++;
      }
    });

    ServerWeights weights;
    const auto weight = 1.0 / static_cast<double>(num_valid_servers);
    std::ranges::for_each(stats, [&](const auto &srv) {
      if (srv.mode != ServerMode::kRead) {
        weights.at(srv.server_id) = weight;
      }
    });

    return weights;
  }

  friend class ProfileGuidedAdaptiveRWIsolation;
};

}  // namespace sandook::schedulers::control_plane
