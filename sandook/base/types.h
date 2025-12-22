#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <tuple>

#include "sandook/base/constants.h"

namespace sandook {

using VolumeBlockAddr = uint64_t;
using ServerBlockAddr = uint64_t;
using VolumeID = uint32_t;
using ServerID = uint32_t;

constexpr static auto kInvalidServerID = 0;
constexpr static auto kInvalidVolumeID = 0;

struct ServerBlockInfo {
  /* ID of the disk server. */
  ServerID server_id;

  /* Block address in the specified disk server. */
  ServerBlockAddr block_addr;
};

struct VolumeBlockInfo {
  /* ID of the volume. */
  VolumeID vol_id;

  /* Block address in the specified volume. */
  VolumeBlockAddr block_addr;
};

using ServerSet = std::set<ServerID>;

using ServerAllocationList = std::array<ServerID, kAllocationBatch>;
using ServerAllocationBlockInfoList =
    std::array<ServerBlockInfo, kAllocationBatch>;

using ServerReplicaList = std::array<ServerID, kNumReplicas>;

/* bool represent dirty bit. */
using ServerReplicaBlockInfo = std::pair<ServerBlockInfo, bool>;
using ServerReplicaBlockInfoList =
    std::array<ServerReplicaBlockInfo, kNumReplicas>;

/* IsTraffic, <NumReadServers, NumWriteServers>
 * The first element is a bool which is true if there is any traffic in the
 * system; false otherwise.
 * The second element is the number of read servers to allocate.
 * The third element is the number of write servers to allocate.
 */
using ServerAllocation = std::tuple<bool, size_t, size_t>;

/* <CurrentReadLoadIOPS, <CurrentWriteLoadIOPS> */
using SystemLoad = std::tuple<uint64_t, uint64_t>;

/* <PeakReadIOPS, PeakWriteIOPS, PeakMixIOPS> */
using DiskPeakIOPS = std::tuple<uint64_t, uint64_t, uint64_t>;

/* Weight when using congestion control. */
using RateLimit = double;

enum ServerCongestionState {
  kInvalid = 0,
  kUnCongested = 1,
  kCongested = 2,
  kCongestedUnstable = 3,
  kCongestedStable = 4,
};

}  // namespace sandook
