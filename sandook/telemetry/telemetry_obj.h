#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "sandook/base/time.h"

namespace sandook {

class TelemetryObj {
 public:
  explicit TelemetryObj() : timestamp_(MicroTime()) {}
  explicit TelemetryObj(uint64_t timestamp) : timestamp_(timestamp) {}
  virtual ~TelemetryObj() = default;

  virtual std::ostream& ToStream(std::ostream& os) const = 0;

  /* Does this telemetry object contain any information or can this be skipped
   * from logging?
   * Two consecutive entries with IsEmpty() true will only result in the first
   * one being logged to the stream and all subsequent ones to be skipped to
   * keep the log size much lower.
   *
   * e.g., when the load in the system is zero, we don't want to constantly keep
   * logging this until we see a non-zero load point.
   */
  virtual bool IsEmpty() const { return false; }

  [[nodiscard]] uint64_t Timestamp() const { return timestamp_; }

  /* Header to output at the start of the trace stream. */
  static std::string Header();

  /* Name of the stream. */
  static std::string Name();

 private:
  uint64_t timestamp_;
};

inline std::ostream& operator<<(std::ostream& os, const TelemetryObj& obj) {
  return obj.ToStream(os);
}

}  // namespace sandook
