#pragma once

#include <memory>
#include <stdexcept>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/config/config.h"
#include "sandook/scheduler/data_plane/base_scheduler.h"
#include "sandook/scheduler/data_plane/random_read_hash_write.h"
#include "sandook/scheduler/data_plane/random_read_write.h"
#include "sandook/scheduler/data_plane/server_stats_manager.h"
#include "sandook/scheduler/data_plane/weighted_read_hash_write.h"
#include "sandook/scheduler/data_plane/weighted_read_write.h"

namespace sandook::schedulers::data_plane {

class Scheduler {
 public:
  Scheduler() : Scheduler(Config::kDataPlaneSchedulerType) {}
  ~Scheduler() = default;

  explicit Scheduler(Config::DataPlaneSchedulerType sched_type)
      : Scheduler(sched_type, kInvalidVolumeID) {}

  explicit Scheduler(Config::DataPlaneSchedulerType sched_type, VolumeID vol_id)
      : vol_id_(vol_id) {
    stats_mgr_ = std::make_unique<ServerStatsManager>(vol_id_);

    switch (sched_type) {
      case Config::DataPlaneSchedulerType::kWeightedReadWrite:
        sched_ = std::make_unique<WeightedReadWrite>();
        break;

      case Config::DataPlaneSchedulerType::kRandomReadWrite:
        sched_ = std::make_unique<RandomReadWrite>();
        break;

      case Config::DataPlaneSchedulerType::kWeightedReadHashWrite:
        sched_ = std::make_unique<WeightedReadHashWrite>();
        break;

      case Config::DataPlaneSchedulerType::kRandomReadHashWrite:
        sched_ = std::make_unique<RandomReadHashWrite>();
        break;

      default:
        throw std::runtime_error("Unknown data plane scheduler");
    }
  }

  /* No copying. */
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  /* No moving. */
  Scheduler(Scheduler &&other) = delete;
  Scheduler &operator=(Scheduler &&other) = delete;

  Status<void> AddServer(ServerID server_id) {
    return stats_mgr_->AddServer(server_id);
  }

  Status<void> SetServerStats(const ServerStatsList &servers) {
    return stats_mgr_->SetServerStats(servers);
  }

  Status<ServerID> SelectReadServer(const ServerSet *subset, VolumeID vol_id,
                                    const IODesc *iod) {
    auto weights = stats_mgr_->GetReadOnlyWeights();
    if (weights) {
      const auto srv = sched_->SelectReadServer(*weights, subset, vol_id, iod);
      if (srv) {
        return *srv;
      }
    }

    /* Either read-only weights were not found or a selection could not be made
     * from read-only servers for the current request; try among all servers. */
    weights = stats_mgr_->GetAllReadWeights();
    if (!weights) {
      return MakeError(weights);
    }

    return sched_->SelectReadServer(*weights, subset, vol_id, iod);
  }

  Status<ServerReplicaList> SelectWriteReplicas(VolumeID vol_id,
                                                const IODesc *iod) {
    const auto weights = stats_mgr_->GetWriteWeights();
    if (!weights) {
      return MakeError(weights);
    }

    return sched_->SelectWriteReplicas(*weights, vol_id, iod);
  }

  void SignalCongested(ServerID server_id) {
    stats_mgr_->SignalCongested(server_id);
  }

 private:
  std::unique_ptr<ServerStatsManager> stats_mgr_;
  std::unique_ptr<BaseScheduler> sched_;
  VolumeID vol_id_{kInvalidVolumeID};
};

}  // namespace sandook::schedulers::data_plane
