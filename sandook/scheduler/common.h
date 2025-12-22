#pragma once

#include <array>
#include <memory>

#include "sandook/base/constants.h"
#include "sandook/base/server_stats.h"
#include "sandook/telemetry/disk_server_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"

namespace sandook::schedulers {

struct ServerSchedStats {
  /* Server stats related to scheduling. */
  ServerStats stats{};
  std::unique_ptr<TelemetryStream<DiskServerTelemetry>> telemetry;
};

using SchedulingMap = std::array<ServerSchedStats, kNumMaxServers>;

}  // namespace sandook::schedulers
