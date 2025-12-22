#pragma once

#include "sandook/scheduler/control_plane/base_scheduler.h"

namespace sandook::schedulers::control_plane {

class NoOp : public BaseScheduler {
 public:
  NoOp() = default;
  ~NoOp() override = default;

  /* No copying. */
  NoOp(const NoOp &) = delete;
  NoOp &operator=(const NoOp &) = delete;

  /* No moving. */
  NoOp(NoOp &&other) = delete;
  NoOp &operator=(NoOp &&other) = delete;
};

}  // namespace sandook::schedulers::control_plane
