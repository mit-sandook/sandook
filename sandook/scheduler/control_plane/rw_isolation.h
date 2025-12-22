#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <random>
#include <tuple>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/bindings/log.h"
#include "sandook/scheduler/control_plane/base_scheduler.h"

/* NumReadServers, NumWriteServers. */
using ServerAllocation = std::tuple<size_t, size_t>;

namespace sandook::schedulers::control_plane {

class ProfileGuidedRWIsolation;

constexpr auto kModeSwitchIntervalUs = 200 * kOneMilliSecond;

class RWIsolation : public BaseScheduler {
 public:
  explicit RWIsolation()
      : mode_switch_interval_(kModeSwitchIntervalUs),
        rand_generator_(rand_device_()),
        last_mode_switch_time_(Time::Now()) {}
  ~RWIsolation() override = default;

  /* No copying. */
  RWIsolation(const RWIsolation &) = delete;
  RWIsolation &operator=(const RWIsolation &) = delete;

  /* No moving. */
  RWIsolation(RWIsolation &&other) = delete;
  RWIsolation &operator=(RWIsolation &&other) = delete;

  Status<ServerModes> ComputeModes(const ServerStatsList &stats) override {
    ServerModes modes;

    const size_t num_servers = stats.size();

    if (num_servers <= kNumReplicas) {
      /* There are not enough servers in the system for isolation, just keep all
       * servers in mixed mode to handle both reads and writes.
       */
      for (auto &mode : modes) {
        mode = ServerMode::kMix;
      }
      return modes;
    }

    const auto allocation = GetAllocation(&stats);
    const auto is_allocation_changed = IsAllocationChanged(allocation);
    UpdateAllocation(allocation);

    const auto [n_r_servers, n_w_servers] = allocation;
    if (n_r_servers == 0 && n_w_servers == 0) {
      /* There is no traffic in the system, do not change the allocation and
       * stick to what we have.
       */
      return MakeError(EAGAIN);
    }

    if (!is_allocation_changed && !IsTimeToModeSwitch()) {
      return MakeError(EAGAIN);
    }

    /* Shuffle the servers before assigning read/write servers. */
    ServerStatsList stats_rand = stats;
    std::shuffle(stats_rand.begin(), stats_rand.end(), rand_generator_);

    /* Set the new allocation. */
    auto it = stats_rand.begin();
    /* Assign the read-mode servers. */
    for (size_t i = 0; i < n_r_servers; i++) {
      const auto server_id = (*it).server_id;
      modes.at(server_id) = ServerMode::kRead;
      it++;
    }
    /* Assign the write-mode servers. */
    while (it != stats_rand.end()) {
      const auto server_id = (*it).server_id;
      modes.at(server_id) = ServerMode::kWrite;
      it++;
    }

    UpdateModeSwitchTime();

    return modes;
  }

 private:
  const Duration mode_switch_interval_;
  std::random_device rand_device_;
  std::mt19937 rand_generator_;

  ServerAllocation last_allocation_{0, 0};
  Time last_mode_switch_time_;

  bool IsAllocationChanged(const ServerAllocation allocation) {
    return last_allocation_ != allocation;
  }

  void UpdateAllocation(const ServerAllocation allocation) {
    last_allocation_ = allocation;
  }

  bool IsTimeToModeSwitch() {
    return Duration::Since(last_mode_switch_time_) >= mode_switch_interval_;
  }

  void UpdateModeSwitchTime() { last_mode_switch_time_ = Time::Now(); }

  static ServerAllocation GetAllocation(const ServerStatsList *stats) {
    // NOLINTBEGIN
    const double total_writes = std::ranges::fold_left(
        *stats, 0.0, [](double mops, const auto &server) {
          return mops + server.write_mops + server.rejected_writes;
        });
    // NOLINTEND

    /* If there is at least one write, we need to reserve at least kNumReplicas
     * servers to handle the writes.
     */
    size_t min_w_servers = 0;
    if (total_writes > 0) {
      min_w_servers = kNumReplicas;
    }

    // NOLINTBEGIN
    const double total_reads = std::ranges::fold_left(
        *stats, 0.0, [](double mops, const auto &server) {
          return mops + server.read_mops + server.rejected_reads;
        });
    // NOLINTEND

    size_t n_r_servers = 0;
    size_t n_w_servers = 0;
    const double total_mops = total_reads + total_writes;
    if (total_mops != 0) {
      /* Find the number of write servers to allocate. */
      const auto w_ratio = total_writes / total_mops;
      const double w_size = static_cast<double>(stats->size()) * w_ratio;
      const auto needed_w_servers = static_cast<size_t>(std::ceil(w_size));
      n_w_servers =
          std::min(stats->size(), std::max(min_w_servers, needed_w_servers));

      /* Assign remaining servers as read servers. */
      n_r_servers = stats->size() - n_w_servers;

      if (total_reads != 0 && n_r_servers == 0) {
        LOG(WARN) << "Not enough servers for pure reads";
      }
    }

    return {n_r_servers, n_w_servers};
  }

  friend class ProfileGuidedRWIsolation;
};

}  // namespace sandook::schedulers::control_plane
