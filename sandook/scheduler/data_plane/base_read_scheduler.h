#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"

namespace sandook::schedulers::data_plane {

class BaseReadScheduler {
 public:
  virtual ~BaseReadScheduler() = default;

  /* No copying. */
  BaseReadScheduler(const BaseReadScheduler &) = delete;
  BaseReadScheduler &operator=(const BaseReadScheduler &) = delete;

  /* No moving. */
  BaseReadScheduler(BaseReadScheduler &&other) = delete;
  BaseReadScheduler &operator=(BaseReadScheduler &&other) = delete;

  virtual Status<ServerID> SelectReadServer(const ServerWeights &weights,
                                            const ServerSet *subset,
                                            VolumeID vol_id,
                                            const IODesc *iod) = 0;

 protected:
  BaseReadScheduler() = default;
};

}  // namespace sandook::schedulers::data_plane
