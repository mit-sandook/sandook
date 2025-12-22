#pragma once

#include <ostream>
#include <string>

#include "sandook/base/server_stats.h"
#include "sandook/telemetry/telemetry_obj.h"

namespace sandook {

constexpr static auto kDiskServerTelemetryName = "disk_server";
// NOLINTBEGIN
constexpr static auto kDiskServerTelemetryHeader =
    "timestamp,"
    "mode,"
    "read_mops,"
    "write_mops,"
    "read_weight,"
    "write_weight,"
    "inflight_reads,"
    "inflight_writes,"
    "completed_reads,"
    "pure_reads,"
    "impure_reads,"
    "completed_writes,"
    "rejected_reads,"
    "rejected_writes,"
    "median_read_latency,"
    "median_write_latency,"
    "signal_read_latency,"
    "signal_write_latency,"
    "is_rejecting_requests,"
    "congestion_state";
// NOLINTEND

class DiskServerTelemetry : public TelemetryObj {
 public:
  DiskServerTelemetry() = default;
  explicit DiskServerTelemetry(ServerStats stats) : stats_(stats) {}

  ~DiskServerTelemetry() override = default;

  std::ostream &ToStream(std::ostream &os) const override {
    // NOLINTBEGIN
    return os << Timestamp() << "," << stats_.mode << "," << stats_.read_mops
              << "," << stats_.write_mops << "," << stats_.read_weight << ","
              << stats_.write_weight << "," << stats_.inflight_reads << ","
              << stats_.inflight_writes << "," << stats_.completed_reads << ","
              << stats_.pure_reads << "," << stats_.impure_reads << ","
              << stats_.completed_writes << "," << stats_.rejected_reads << ","
              << stats_.rejected_writes << "," << stats_.median_read_latency
              << "," << stats_.median_write_latency << ","
              << stats_.signal_read_latency << ","
              << stats_.signal_write_latency << ","
              << stats_.is_rejecting_requests << "," << stats_.congestion_state;
    // NOLINTEND
  }

  bool IsEmpty() const override {
    return stats_.read_mops == 0 && stats_.write_mops == 0;
  }

  static std::string Header() { return kDiskServerTelemetryHeader; }
  static std::string Name() { return kDiskServerTelemetryName; }

 private:
  ServerStats stats_{};
};

}  // namespace sandook
