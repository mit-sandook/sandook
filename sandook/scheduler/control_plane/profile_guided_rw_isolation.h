#pragma once

#include <algorithm>  // NOLINT
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/control_plane/adaptive_rw_isolation_base.h"
#include "sandook/scheduler/control_plane/base_scheduler.h"
#include "sandook/scheduler/control_plane/profile_guided.h"

namespace sandook::schedulers::control_plane {

class ProfileGuidedRWIsolation : public BaseScheduler {
 public:
  ProfileGuidedRWIsolation() = default;
  ~ProfileGuidedRWIsolation() override = default;

  /* No copying. */
  ProfileGuidedRWIsolation(const ProfileGuidedRWIsolation &) = delete;
  ProfileGuidedRWIsolation &operator=(const ProfileGuidedRWIsolation &) =
      delete;

  /* No moving. */
  ProfileGuidedRWIsolation(ProfileGuidedRWIsolation &&other) = delete;
  ProfileGuidedRWIsolation &operator=(ProfileGuidedRWIsolation &&other) =
      delete;

  Status<void> AddServer(ServerID server_id, const std::string &name,
                         const DiskModel *model) override {
    const auto pg_add = pg_.AddServer(server_id, name, model);
    if (!pg_add) {
      return MakeError(pg_add);
    }

    const auto rw_add = rw_.AddServer(server_id, name, model);
    if (!rw_add) {
      return MakeError(rw_add);
    }

    return {};
  }

  Status<DiskPeakIOPS> GetDiskPeakIOPS(ServerID server_id) const override {
    return pg_.GetDiskPeakIOPS(server_id);
  }

  Status<ServerModes> ComputeModes(const ServerStatsList &stats,
                                   const SystemLoad load) override {
    return rw_.ComputeModes(stats, load);
  }

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
  AdaptiveRWIsolationBase rw_;
  ProfileGuided pg_;

  /* Reads can be performed from any server. */
  Status<ServerWeights> ComputeReadWeights(const ServerStatsList &stats,
                                           const SystemLoad load) {
    return pg_.ComputeWeights(stats, OpType::kRead, load);
  }

  /* Writes go to servers not in read mode. */
  Status<ServerWeights> ComputeWriteWeights(const ServerStatsList &stats,
                                            const SystemLoad load) {
    auto stats_v = stats | std::views::filter([&](const auto &srv_stats) {
                     return srv_stats.mode != ServerMode::kRead;
                   });
    const ServerStatsList stats_filtered({stats_v.cbegin(), stats_v.cend()});
    return pg_.ComputeWeights(stats_filtered, OpType::kWrite, load);
  }
};

}  // namespace sandook::schedulers::control_plane
