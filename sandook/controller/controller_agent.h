#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sandook/base/controller_stats.h"
#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/controller/block_allocator.h"
#include "sandook/controller/server_desc.h"
#include "sandook/controller/volume_desc.h"
#include "sandook/scheduler/control_plane/scheduler.h"

namespace sandook {

namespace controller {

struct RuntimeInfo {
  std::atomic_uint32_t inflight_resolve_ops{0};

  sandook::ControllerStats serialize() {
    return sandook::ControllerStats{.inflight_resolve_ops =
                                        inflight_resolve_ops};
  }
};

}  // namespace controller

class ControllerAgent {
 public:
  ControllerAgent();
  ~ControllerAgent() = default;

  /* No copying. */
  ControllerAgent(const ControllerAgent &) = delete;
  ControllerAgent &operator=(const ControllerAgent &) = delete;

  /* No moving. */
  ControllerAgent(ControllerAgent &&) noexcept;
  ControllerAgent &operator=(ControllerAgent &&) noexcept;

  void HandleSignal(int sig);

  [[nodiscard]] Status<ServerID> RegisterServer(const std::string &ip, int port,
                                                const std::string &name,
                                                uint64_t n_sectors);
  [[nodiscard]] Status<VolumeID> RegisterVolume(const std::string &ip, int port,
                                                uint64_t n_sectors);

  Status<ServerAllocationBlockInfoList> AllocateBlocks(ServerID server_id);

  Status<void> UpdateServerStats(ServerID server_id, ServerStats stats);
  Status<void> CommitServerMode(ServerID server_id, ServerMode mode);
  Status<ServerStatsList> GetServerStats();
  Status<DataPlaneServerStats> GetDataPlaneServerStats(ServerID server_id);
  Status<DiskPeakIOPS> GetDiskPeakIOPS(ServerID server_id) const;

  /* This method is used for tests.
   * It allows "pausing" the dynamic scheduler and testing the behavior of the
   * controller agent at a given moment in time.
   */
  void StopScheduler() { sched_.Stop(); }

 private:
  std::atomic<ServerID> next_server_id_{kInvalidServerID + 1};
  std::unordered_map<ServerID, ServerDesc> servers_;

  std::atomic<VolumeID> vol_id_{kInvalidVolumeID + 1};
  std::unordered_map<VolumeID, VolumeDesc> vols_;

  controller::BlockAllocator blk_alloc_;

  controller::RuntimeInfo stats_;

  schedulers::control_plane::Scheduler sched_;

 public:
  auto get_server_ids() const -> std::vector<ServerID> {
    std::vector<ServerID> ids;
    ids.reserve(servers_.size());
    for (const auto &kv : servers_) {
      ids.push_back(kv.first);
    }
    return ids;
  }

  auto get_servers() const -> const decltype(servers_) & { return servers_; }
  auto get_volumes() const -> const decltype(vols_) & { return vols_; }
};

}  // namespace sandook
