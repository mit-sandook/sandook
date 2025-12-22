#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/config/config.h"
#include "sandook/disk_model/disk_model.h"
#include "sandook/scheduler/control_plane/adaptive_rw_isolation_weak.h"
#include "sandook/scheduler/control_plane/base_scheduler.h"
#include "sandook/scheduler/control_plane/no_op.h"
#include "sandook/scheduler/control_plane/profile_guided.h"
#include "sandook/scheduler/control_plane/profile_guided_rw_isolation.h"
#include "sandook/scheduler/control_plane/rw_isolation_strict.h"
#include "sandook/scheduler/control_plane/rw_isolation_weak.h"
#include "sandook/scheduler/control_plane/server_stats_manager.h"
#include "sandook/telemetry/disk_server_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"

namespace sandook::schedulers::control_plane {

using TelemetryMap =
    std::array<std::unique_ptr<TelemetryStream<DiskServerTelemetry>>,
               kNumMaxServers>;

class Scheduler {
 public:
  Scheduler() : Scheduler(Config::kControlPlaneSchedulerType) {}

  explicit Scheduler(Config::ControlPlaneSchedulerType sched_type) {
    switch (sched_type) {
      case Config::ControlPlaneSchedulerType::kNoOp:
        sched_ = std::make_unique<NoOp>();
        break;

      case Config::ControlPlaneSchedulerType::kProfileGuided:
        sched_ = std::make_unique<ProfileGuided>();
        break;

      case Config::ControlPlaneSchedulerType::kRWIsolationStrict:
        sched_ = std::make_unique<RWIsolationStrict>();
        break;

      case Config::ControlPlaneSchedulerType::kRWIsolationWeak:
        sched_ = std::make_unique<RWIsolationWeak>();
        break;

      case Config::ControlPlaneSchedulerType::kAdaptiveRWIsolationWeak:
        sched_ = std::make_unique<AdaptiveRWIsolationWeak>();
        break;

      case Config::ControlPlaneSchedulerType::kProfileGuidedRWIsolation:
        sched_ = std::make_unique<ProfileGuidedRWIsolation>();
        break;

      default:
        throw std::runtime_error("Unknown control plane scheduler");
    }
    th_updater_ = rt::Thread([this]() -> void { Run(); });
  }

  ~Scheduler() {
    Stop();
    th_updater_.Join();
  }

  /* No copying. */
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  /* No moving. */
  Scheduler(Scheduler &&other) = delete;
  Scheduler &operator=(Scheduler &&other) = delete;

  void HandleSignal(int sig) {
    stats_mgr_.HandleSignal(sig);
    sched_->HandleSignal(sig);
  }

  Status<void> AddServer(ServerID server_id, const std::string &name,
                         const DiskModel *model = nullptr) {
    const auto ret = sched_->AddServer(server_id, name, model);
    if (!ret) {
      return MakeError(ret);
    }

    stats_mgr_.AddServer(server_id, name);

    /* Create a telemetry stream for the server. */
    const auto telemetry_tag = std::to_string(server_id) + "_" + name;
    telemetry_map_.at(server_id) =
        std::make_unique<TelemetryStream<DiskServerTelemetry>>(telemetry_tag);

    num_servers_++;

    return {};
  }

  Status<DiskPeakIOPS> GetDiskPeakIOPS(ServerID server_id) const {
    return sched_->GetDiskPeakIOPS(server_id);
  }

  Status<ServerStatsList> GetServerStats() {
    return stats_mgr_.GetServerStats();
  }

  Status<SystemLoad> GetSystemLoad() const {
    return stats_mgr_.GetSystemLoad();
  }

  Status<DataPlaneServerStats> GetDataPlaneServerStats(ServerID server_id) {
    return stats_mgr_.GetDataPlaneServerStats(server_id);
  }

  Status<void> UpdateServerStats(ServerID server_id, ServerStats stats,
                                 bool is_override = false,
                                 bool is_update_system_load = true) {
    stats_mgr_.UpdateServerStats(server_id, stats, is_override,
                                 is_update_system_load);
    telemetry_map_.at(server_id)->TraceBuffered(DiskServerTelemetry(stats));

    return {};
  }

  Status<void> CommitServerMode(ServerID server_id, ServerMode mode) {
    stats_mgr_.CommitServerMode(server_id, mode);
    return {};
  }

  void Stop() {
    stats_mgr_.Stop();
    stop_ = true;
  }

  void FreezeWeights() { freeze_weights_ = true; }

  void FreezeModes() { freeze_modes_ = true; }

  void FreezeLoad() { stats_mgr_.FreezeLoad(); }

  void Update() {
    auto stats_ret = GetServerStats();
    if (!stats_ret) {
      LOG(WARN) << "Cannot get server stats";
      return;
    }

    auto load_ret = GetSystemLoad();
    if (!load_ret) {
      LOG(WARN) << "Cannot get system load";
      return;
    }

    auto &stats = *stats_ret;
    const auto load = *load_ret;

    /* First compute the modes; some schedulers may use them for computing
     * weights.
     */
    Status<ServerModes> modes = MakeError(EINVAL);
    if (!freeze_modes_) {
      modes = ComputeModes(stats, load);

      if (modes) {
        /* Update the mode in ServerStatsList to be used in subsequent weight
         * calculation.
         */
        for (auto &srv_stats : stats) {
          srv_stats.mode = (*modes).at(srv_stats.server_id);
        }
      }
    }

    /* Now compute the weights. */
    Status<ServerWeights> r_weights = MakeError(EINVAL);
    Status<ServerWeights> w_weights = MakeError(EINVAL);
    if (!freeze_weights_) {
      r_weights = sched_->ComputeWeights(stats, OpType::kRead, load);
      w_weights = sched_->ComputeWeights(stats, OpType::kWrite, load);
    }

    for (size_t i = 0; i < num_servers_; i++) {
      const auto server_id = i + kInvalidServerID + 1;
      auto srv_stats = stats_mgr_.GetServerStats(server_id);

      if (r_weights) {
        srv_stats.read_weight = (*r_weights).at(server_id);
      }

      if (w_weights) {
        srv_stats.write_weight = (*w_weights).at(server_id);
      }

      if (modes) {
        srv_stats.mode = (*modes).at(server_id);
      }

      if (r_weights || w_weights || modes) {
        static const auto is_override = true;
        static const auto is_update_load = false;
        stats_mgr_.UpdateServerStats(server_id, srv_stats, is_override,
                                     is_update_load);
      }
    }
  }

 private:
  size_t num_servers_{0};
  ServerStatsManager stats_mgr_;
  std::unique_ptr<BaseScheduler> sched_;
  TelemetryMap telemetry_map_;

  bool stop_{false};
  bool freeze_weights_{false};
  bool freeze_modes_{false};
  rt::Thread th_updater_;

  Status<ServerModes> ComputeModes(const ServerStatsList &stats,
                                   const SystemLoad load) {
    return sched_->ComputeModes(stats, load);
  }

  void Run() {
    const Duration interval(kControlPlaneUpdateIntervalUs);

    while (!stop_) {
      Update();
      rt::Sleep(interval);
    }
  }
};

}  // namespace sandook::schedulers::control_plane
