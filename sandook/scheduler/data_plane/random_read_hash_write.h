#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_scheduler.h"
#include "sandook/scheduler/data_plane/hash_write.h"
#include "sandook/scheduler/data_plane/random_read.h"

namespace sandook::schedulers::data_plane {

class RandomReadHashWrite : public BaseScheduler {
 public:
  RandomReadHashWrite() = default;
  ~RandomReadHashWrite() override = default;

  /* No copying. */
  RandomReadHashWrite(const RandomReadHashWrite &) = delete;
  RandomReadHashWrite &operator=(const RandomReadHashWrite &) = delete;

  /* No moving. */
  RandomReadHashWrite(RandomReadHashWrite &&other) = delete;
  RandomReadHashWrite &operator=(RandomReadHashWrite &&other) = delete;

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
  HashWrite write_sched_;
};

}  // namespace sandook::schedulers::data_plane
