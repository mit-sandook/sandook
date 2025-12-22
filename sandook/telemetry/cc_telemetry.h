#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "sandook/base/types.h"
#include "sandook/telemetry/telemetry_obj.h"

namespace sandook {

constexpr static auto kCongestionControlTelemetryName = "cc";
constexpr static auto kCongestionControlTelemetryHeader =
    "timestamp,state,rate_limit";

class CongestionControlTelemetry : public TelemetryObj {
 public:
  CongestionControlTelemetry() = default;
  explicit CongestionControlTelemetry(uint64_t timestamp,
                                      ServerCongestionState state,
                                      RateLimit rate)
      : TelemetryObj(timestamp), state_(state), rate_(rate) {}

  ~CongestionControlTelemetry() override = default;

  std::ostream &ToStream(std::ostream &os) const override {
    return os << Timestamp() << "," << state_ << "," << rate_;
  }

  static std::string Header() { return kCongestionControlTelemetryHeader; }
  static std::string Name() { return kCongestionControlTelemetryName; }

 private:
  ServerCongestionState state_;
  RateLimit rate_;
};

}  // namespace sandook
