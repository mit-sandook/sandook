#pragma once

#include <ostream>
#include <string>

#include "sandook/base/types.h"
#include "sandook/telemetry/telemetry_obj.h"

namespace sandook {

constexpr static auto kSystemLoadTelemetryName = "system_load";
constexpr static auto kSystemLoadTelemetryHeader =
    "timestamp,read_ops,write_ops,total_ops";

class SystemLoadTelemetry : public TelemetryObj {
 public:
  SystemLoadTelemetry() = default;
  explicit SystemLoadTelemetry(SystemLoad load) : load_(load) {}

  ~SystemLoadTelemetry() override = default;

  std::ostream &ToStream(std::ostream &os) const override {
    const auto [read_ops, write_ops] = load_;
    const auto total_ops = read_ops + write_ops;
    return os << Timestamp() << "," << read_ops << "," << write_ops << ","
              << total_ops;
  }

  bool IsEmpty() const override {
    const auto [read_ops, write_ops] = load_;
    const auto total_ops = read_ops + write_ops;
    return read_ops == 0 && write_ops == 0 && total_ops == 0;
  }

  static std::string Header() { return kSystemLoadTelemetryHeader; }
  static std::string Name() { return kSystemLoadTelemetryName; }

 private:
  SystemLoad load_;
};

}  // namespace sandook
