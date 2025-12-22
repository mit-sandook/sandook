#pragma once

#include "sandook/scheduler/data_plane/base_read_scheduler.h"
#include "sandook/scheduler/data_plane/base_write_scheduler.h"

namespace sandook::schedulers::data_plane {

class BaseScheduler : public BaseReadScheduler, public BaseWriteScheduler {
 public:
  ~BaseScheduler() override = default;

  /* No copying. */
  BaseScheduler(const BaseScheduler &) = delete;
  BaseScheduler &operator=(const BaseScheduler &) = delete;

  /* No moving. */
  BaseScheduler(BaseScheduler &&other) = delete;
  BaseScheduler &operator=(BaseScheduler &&other) = delete;

 protected:
  BaseScheduler() = default;
};

}  // namespace sandook::schedulers::data_plane
