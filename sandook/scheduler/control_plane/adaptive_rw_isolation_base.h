#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <tuple>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/disk_model/disk_model.h"
#include "sandook/scheduler/control_plane/rw_isolation_base.h"

namespace sandook::schedulers::control_plane {

class AdaptiveRWIsolationBase : public RWIsolationBase {
 public:
  AdaptiveRWIsolationBase() = default;
  ~AdaptiveRWIsolationBase() override = default;

  /* No copying. */
  AdaptiveRWIsolationBase(const AdaptiveRWIsolationBase &) = delete;
  AdaptiveRWIsolationBase &operator=(const AdaptiveRWIsolationBase &) = delete;

  /* No moving. */
  AdaptiveRWIsolationBase(AdaptiveRWIsolationBase &&other) = delete;
  AdaptiveRWIsolationBase &operator=(AdaptiveRWIsolationBase &&other) = delete;

  Status<void> AddServer(ServerID server_id, const std::string &name,
                         const DiskModel *model) override {
    const auto ret = RWIsolationBase::AddServer(server_id, name, model);
    if (!ret) {
      return MakeError(ret);
    }

    if (model != nullptr) {
      models_.at(server_id) = *model;
    } else {
      models_.at(server_id) = sandook::DiskModel(name);
      LOG(INFO) << "Model added for: " << name;
    }

    return {};
  }

  uint64_t GetPeakIOPS(ServerID server_id, ServerMode mode) override {
    return models_.at(server_id).GetPeakIOPS(ServerMode::kMix);
  }

 private:
  sandook::DiskModels models_;
};

}  // namespace sandook::schedulers::control_plane
