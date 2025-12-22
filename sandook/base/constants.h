#pragma once

extern "C" {
#include <asm/cpu.h>
#include <base/limits.h>
}
#include <cstddef>

// NOLINTBEGIN
int constexpr length(const char* str) {
  return *str != 0 ? 1 + length(str + 1) : 0;
}
// NOLINTEND

namespace sandook {

constexpr static auto kIPAddrStrLen = 16;
constexpr static auto kNameStrLen = 20;
constexpr static auto kDefaultServerName = "disk_server";
constexpr static auto kSPDKDeviceSerialNumberLen = 20;
constexpr int kDefaultServerNameLen = length(kDefaultServerName);
static_assert(kDefaultServerNameLen <= kNameStrLen);
static_assert(kNameStrLen == kSPDKDeviceSerialNumberLen,
              "Server name size must be at least large enough to hold SPDK "
              "device serial number");

constexpr static auto kNumMaxServers = 20;
constexpr static auto kNumMaxVolumes = 16;

constexpr static auto kNumReplicas = 2;
constexpr static size_t kAllocationBatch = 2048;
static_assert(kAllocationBatch >= kNumReplicas,
              "Allocation batch size must be at least equal to num replicas");
constexpr static size_t kDiscardBatch = 2048;

constexpr static auto kSectorShift = 12;
constexpr static auto kLinuxSectorShift = 9;
static_assert(kSectorShift >= kLinuxSectorShift,
              "Sandook sector must be multiples of a 512B Linux sector!");
constexpr static auto kNumLinuxSectorsPerSandookSector =
    1 << (kSectorShift - kLinuxSectorShift);
constexpr static auto kDeviceAlignment = 1 << kSectorShift;

constexpr static auto kCacheLineSizeBytes = CACHE_LINE_SIZE;
constexpr static auto kMaxNumCores = NCPU;

/* Percentile constants. */
constexpr static auto kP50 = 0.50;
constexpr static auto kP90 = 0.90;
constexpr static auto kP99 = 0.99;

constexpr static auto kMillion = 1000 * 1000;

constexpr static auto kOneMicroSecond = 1;
constexpr static auto kOneMilliSecond = 1000 * kOneMicroSecond;
constexpr static auto kOneSecond = 1000 * kOneMilliSecond;
constexpr static auto kOneMinute = 60 * kOneSecond;

/* Garbage collection interval. */
constexpr auto kGarbageCollectionIntervalUs = 0;  // GC disabled
/* Interval to push disk server stats to the controller. */
constexpr auto kDiskServerStatsUpdateIntervalUs = 100 * kOneMicroSecond;
/* Interval to run the control plane policies. */
constexpr auto kControlPlaneUpdateIntervalUs = kDiskServerStatsUpdateIntervalUs;
/* Interval to pull server stats from controller (in virtual disk). */
constexpr auto kServerStatsPullIntervalUs = kControlPlaneUpdateIntervalUs;
/* Interval to wait before switching server modes. */
constexpr auto kModeSwitchIntervalUs = 500 * kOneMilliSecond;
/* Interval to allow potential mixing of requests in the disk server after a
 * mode switch has occured. This prevents very aggressive rejections when the
 * client has a (slightly) stale view of the disk server's mode.
 */
constexpr auto kDiskServerModeSwitchGracePeriodUs = 1 * kOneMilliSecond;
/* Once a disk server enters rejection mode, it stays in this for at least this
 * much duration.
 */
constexpr auto kCongestionControlWindowUs = 50 * kOneMilliSecond;

/* SSD properties used by the static allocation scheme. */
constexpr auto kPeakWriteIOPSPerSSD = 0.20 * kMillion;
constexpr auto kPeakReadIOPSPerSSD = 0.6 * kMillion;

}  // namespace sandook
