#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_scheduler.h"
#include "sandook/scheduler/data_plane/weighted_read.h"
#include "sandook/scheduler/data_plane/weighted_write.h"

namespace sandook::schedulers::data_plane {

class WeightedReadWrite : public BaseScheduler {
 public:
  WeightedReadWrite() = default;
  ~WeightedReadWrite() override = default;

  /* No copying. */
  WeightedReadWrite(const WeightedReadWrite &) = delete;
  WeightedReadWrite &operator=(const WeightedReadWrite &) = delete;

  /* No moving. */
  WeightedReadWrite(WeightedReadWrite &&other) = delete;
  WeightedReadWrite &operator=(WeightedReadWrite &&other) = delete;

  Status<ServerID> SelectReadServer(const ServerWeights &weights,
                                    const ServerSet *subset, VolumeID vol_id,
                                    const IODesc *iod) override {
    return read_sched_.SelectReadServer(weights, subset, vol_id, iod);
  }

  Status<ServerReplicaList> SelectWriteReplicas(const ServerWeights &weights,
                                                VolumeID vol_id,
                                                const IODesc *iod) override {
    return write_sched_.SelectWriteReplicas(weights, vol_id, iod);
  }

 private:
  WeightedRead read_sched_;
  WeightedWrite write_sched_;
};

}  // namespace sandook::schedulers::data_plane
