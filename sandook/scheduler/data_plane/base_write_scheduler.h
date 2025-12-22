#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"

namespace sandook::schedulers::data_plane {

class BaseWriteScheduler {
 public:
  virtual ~BaseWriteScheduler() = default;

  /* No copying. */
  BaseWriteScheduler(const BaseWriteScheduler &) = delete;
  BaseWriteScheduler &operator=(const BaseWriteScheduler &) = delete;

  /* No moving. */
  BaseWriteScheduler(BaseWriteScheduler &&other) = delete;
  BaseWriteScheduler &operator=(BaseWriteScheduler &&other) = delete;

  virtual Status<ServerReplicaList> SelectWriteReplicas(
      const ServerWeights &weights, VolumeID vol_id, const IODesc *iod) = 0;

 protected:
  BaseWriteScheduler() = default;
};

}  // namespace sandook::schedulers::data_plane
