#pragma once

#include <cerrno>
#include <string>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/disk_model/disk_model.h"

namespace sandook::schedulers::control_plane {

class BaseScheduler {
 public:
  BaseScheduler() = default;
  virtual ~BaseScheduler() = default;

  /* No copying. */
  BaseScheduler(const BaseScheduler &) = delete;
  BaseScheduler &operator=(const BaseScheduler &) = delete;

  /* No moving. */
  BaseScheduler(BaseScheduler &&other) = delete;
  BaseScheduler &operator=(BaseScheduler &&other) = delete;

  virtual void HandleSignal([[maybe_unused]] int sig) {}

  virtual Status<void> AddServer([[maybe_unused]] ServerID server_id,
                                 [[maybe_unused]] const std::string &name,
                                 [[maybe_unused]] const DiskModel *model) {
    return {};
  }

  virtual Status<ServerModes> ComputeModes(
      [[maybe_unused]] const ServerStatsList &stats,
      [[maybe_unused]] const SystemLoad load) {
    return MakeError(ENOTSUP);
  }

  virtual Status<ServerWeights> ComputeWeights(
      [[maybe_unused]] const ServerStatsList &stats, [[maybe_unused]] OpType op,
      [[maybe_unused]] const SystemLoad load) {
    return MakeError(ENOTSUP);
  }

  [[nodiscard]] virtual Status<DiskPeakIOPS> GetDiskPeakIOPS(
      ServerID server_id) const {
    return MakeError(ENOTSUP);
  }
};

}  // namespace sandook::schedulers::control_plane
