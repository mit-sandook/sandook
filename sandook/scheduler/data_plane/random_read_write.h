#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_scheduler.h"
#include "sandook/scheduler/data_plane/random_read.h"
#include "sandook/scheduler/data_plane/random_write.h"

namespace sandook::schedulers::data_plane {

class RandomReadWrite : public BaseScheduler {
 public:
  RandomReadWrite() = default;
  ~RandomReadWrite() override = default;

  /* No copying. */
  RandomReadWrite(const RandomReadWrite &) = delete;
  RandomReadWrite &operator=(const RandomReadWrite &) = delete;

  /* No moving. */
  RandomReadWrite(RandomReadWrite &&other) = delete;
  RandomReadWrite &operator=(RandomReadWrite &&other) = delete;

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
  RandomRead read_sched_;
  RandomWrite write_sched_;
};

}  // namespace sandook::schedulers::data_plane
