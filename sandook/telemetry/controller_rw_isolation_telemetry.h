#pragma once

#include <cstddef>
#include <ostream>
#include <string>

#include "sandook/base/types.h"
#include "sandook/telemetry/telemetry_obj.h"

namespace sandook {

constexpr static auto kControllerRWIsolationTelemetryName =
    "controller_rw_isolation";
constexpr static auto kControllerRWIsolationTelemetryHeader =
    "timestamp,is_traffic,num_read_servers,num_write_servers,num_servers";

class ControllerRWIsolationTelemetry : public TelemetryObj {
 public:
  ControllerRWIsolationTelemetry() = default;
  explicit ControllerRWIsolationTelemetry(ServerAllocation allocation,
                                          size_t num_servers)
      : allocation_(allocation), num_servers_(num_servers) {}

  ~ControllerRWIsolationTelemetry() override = default;

  std::ostream &ToStream(std::ostream &os) const override {
    const auto [is_traffic, n_read, n_write] = allocation_;
    return os << Timestamp() << "," << is_traffic << "," << n_read << ","
              << n_write << "," << num_servers_;
  }

  bool IsEmpty() const override {
    const auto [is_traffic, n_read, n_write] = allocation_;
    return !is_traffic;
  }

  static std::string Header() { return kControllerRWIsolationTelemetryHeader; }
  static std::string Name() { return kControllerRWIsolationTelemetryName; }

 private:
  ServerAllocation allocation_;
  size_t num_servers_;
};

}  // namespace sandook
