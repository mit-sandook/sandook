#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <tuple>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/disk_model/disk_model.h"
#include "sandook/scheduler/control_plane/base_scheduler.h"
#include "sandook/telemetry/controller_rw_isolation_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"

namespace sandook::schedulers::control_plane {

class RWIsolationBase : public BaseScheduler {
 public:
  explicit RWIsolationBase()
      : rand_generator_(rand_device_()), last_mode_switch_time_(MicroTime()) {}
  ~RWIsolationBase() override = default;

  /* No copying. */
  RWIsolationBase(const RWIsolationBase &) = delete;
  RWIsolationBase &operator=(const RWIsolationBase &) = delete;

  /* No moving. */
  RWIsolationBase(RWIsolationBase &&other) = delete;
  RWIsolationBase &operator=(RWIsolationBase &&other) = delete;

  Status<void> AddServer([[maybe_unused]] ServerID server_id,
                         [[maybe_unused]] const std::string &name,
                         [[maybe_unused]] const DiskModel *model) override {
    num_servers_++;
    last_write_server_idx_ = 0;

    return {};
  }

  Status<ServerModes> ComputeModes(const ServerStatsList &stats,
                                   const SystemLoad load) override {
    const size_t num_servers = stats.size();
    ServerModes modes;

    if (num_servers <= kNumReplicas) {
      /* There are not enough servers in the system for isolation, just keep all
       * servers in mixed mode to handle both reads and writes.
       */
      for (auto &mode : modes) {
        mode = ServerMode::kMix;
      }

      UpdateSystemLoad(load);

      return modes;
    }

    const auto allocation = GetAllocation(&stats, load);
    const auto [is_traffic, n_r_servers, n_w_servers] = allocation;
    if (!is_traffic) {
      /* There is no traffic in the system, do not change the allocation and
       * stick to what we have.
       */
      UpdateSystemLoad(load);

      return MakeError(EAGAIN);
    }

    if (n_r_servers == 0 && n_w_servers == 0) {
      /* There IS traffic in the system but the n_r_servers and n_w_servers both
       * being 0 indicates all servers should be in mix mode.
       */
      for (auto &mode : modes) {
        mode = ServerMode::kMix;
      }

      UpdateSystemLoad(load);

      return modes;
    }

    const auto is_update = AllocationNeedsUpdate(allocation, load);

    UpdateSystemLoad(load);

    if (!is_update && !IsTimeToModeSwitch()) {
      return MakeError(EAGAIN);
    }

    UpdateAllocation(allocation);

    /* Set the new server allocation. */
    auto server_id = last_write_server_idx_;

    /* Assign the write-mode servers. */
    for (size_t i = 0; i < n_w_servers; i++) {
      /* Add 1 to the server ID to account for kInvalidServerID. */
      modes.at(server_id + 1) = ServerMode::kWrite;
      server_id = (server_id + 1) % num_servers_;
    }
    /* Assign the read-mode servers. */
    for (size_t i = 0; i < n_r_servers; i++) {
      /* Add 1 to the server ID to account for kInvalidServerID. */
      modes.at(server_id + 1) = ServerMode::kRead;
      server_id = (server_id + 1) % num_servers_;
    }

    last_write_server_idx_ =
        (last_write_server_idx_ + n_w_servers) % num_servers_;

    telemetry_.TraceBuffered(
        ControllerRWIsolationTelemetry(allocation, num_servers_));

    UpdateModeSwitchTime();

    return modes;
  }

  virtual uint64_t GetPeakIOPS([[maybe_unused]] ServerID server_id,
                               ServerMode mode) {
    switch (mode) {
      case ServerMode::kRead:
        return kPeakReadIOPSPerSSD;

      default:
        return kPeakWriteIOPSPerSSD;
    }

    std::unreachable();
  }

 private:
  std::random_device rand_device_;
  std::mt19937 rand_generator_;
  TelemetryStream<ControllerRWIsolationTelemetry> telemetry_;

  ServerAllocation last_allocation_{false, 0, 0};
  SystemLoad prev_system_load_;
  uint64_t last_mode_switch_time_;
  size_t num_servers_{0};
  size_t last_write_server_idx_{0};

  bool IsLoadIncreased(const SystemLoad load) const {
    const auto [cur_read_load, cur_write_load] = load;
    const auto [prev_read_load, prev_write_load] = prev_system_load_;
    if (cur_read_load > prev_read_load) {
      return true;
    }
    if (cur_write_load > prev_write_load) {
      return true;
    }
    return false;
  }

  void UpdateSystemLoad(const SystemLoad load) { prev_system_load_ = load; }

  bool IsAllocationChanged(const ServerAllocation allocation) const {
    const auto [cur_is_traffic, cur_nr, cur_nw] = allocation;
    const auto [last_is_traffic, last_nr, last_nw] = last_allocation_;
    if (cur_nr != 0 && last_nr == 0) {
      return true;
    }
    if (cur_nw != 0 && last_nw == 0) {
      return true;
    }
    if (cur_nr > last_nr || cur_nw > last_nw) {
      return true;
    }
    return false;
  }

  void UpdateAllocation(const ServerAllocation allocation) {
    last_allocation_ = allocation;
  }

  bool AllocationNeedsUpdate(const ServerAllocation allocation,
                             const SystemLoad load) {
    return IsAllocationChanged(allocation);
  }

  bool IsTimeToModeSwitch() const {
    const auto now = MicroTime();
    return (now - last_mode_switch_time_) >= kModeSwitchIntervalUs;
  }

  void UpdateModeSwitchTime() { last_mode_switch_time_ = MicroTime(); }

  ServerAllocation GetAllocation(const ServerStatsList *stats,
                                 const SystemLoad load) {
    const auto n_servers = stats->size();

    /* Here, we check *all* write operations, including rejected and inflight
     * ones. This is because we want to do a fast allocation for at least the
     * minimum number of write servers to be available to accept these write
     * operations.
     */
    auto all_writes = 0.0;
    for (const auto &server : *stats) {
      all_writes += server.inflight_writes + server.completed_writes +
                    server.rejected_writes;
    }

    /* For the rest of the allocation decisions, we look at the stable load. */
    const auto [read_ops, write_ops] = load;
    const auto total_ops = read_ops + write_ops;

    /* If there is at least one write operation, we need to reserve at least
     * kNumReplicas servers to handle the writes.
     */
    size_t min_w_servers = 0;
    if (all_writes > 0 || write_ops > 0) {
      min_w_servers = kNumReplicas;
    }

    size_t n_r_servers = 0;
    size_t n_w_servers = 0;

    /* If either number of reads or number of writes is zero, put all the
     * servers in mix mode; this is indicated by returning both n_r_servers and
     * n_w_servers as zero.
     */
    const auto kExtraWriteSSDs = 1;
    if (read_ops != 0 && write_ops != 0) {
      /* Find the actual demand of write servers based on the write IOPS
       * currently in the system and the profiles of the SSDs.
       */
      uint64_t handled_writes = 0;
      auto server_id = last_write_server_idx_;
      size_t dem_w_servers = 0;
      while (true) {
        if (handled_writes >= write_ops || dem_w_servers == n_servers) {
          break;
        }

        /* Add 1 to the server ID to account for kInvalidServerID. */
        const auto server_w_tput = GetPeakIOPS(server_id + 1, ServerMode::kMix);
        handled_writes += server_w_tput;
        dem_w_servers += 1;
        server_id = (server_id + 1) % num_servers_;
      }
      /* Allocate some additional write SSDs to be safe. */
      dem_w_servers += kExtraWriteSSDs;

      /* Compute the number of write servers to allocate considering the demand
       * of write IOPS and the minimum number of servers required for block
       * replication.
       */
      n_w_servers = std::min(n_servers, std::max(min_w_servers, dem_w_servers));

      /* Assign the remaining servers as read servers. */
      n_r_servers = n_servers - n_w_servers;
    } else if (read_ops != 0 && write_ops == 0) {
      n_r_servers = n_servers;
    } else if (write_ops != 0 && read_ops == 0) {
      n_w_servers = n_servers;
    }

    return {total_ops > 0, n_r_servers, n_w_servers};
  }
};

}  // namespace sandook::schedulers::control_plane
