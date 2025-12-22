#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "sandook/base/time.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"

/* Root directory where all telemetry outputs are written to.
 * It is preferred to have this point to an in-memory file system.
 */
const std::filesystem::path kRootPath = "/dev/shm/sandook";
const std::filesystem::path kDefaultTag = "default";

/* Maximum number of telemetry entries to buffer before flushing. */
constexpr static auto kMaxBufferEntries = 1 << 23;

/* Interval (in microseconds) to flush telemetry. */
constexpr static auto kUpdateIntervalUs = 1 * 1000 * 1000;

namespace sandook {

template <typename T>
class TelemetryStream {
 public:
  explicit TelemetryStream(const std::string &tag)
      : buf_active_(&buf_A_),
        buf_inactive_(&buf_B_),
        th_flusher_([this]() { Run(); }) {
    buf_A_.resize(kMaxBufferEntries);
    buf_B_.resize(kMaxBufferEntries);

    const auto stream_path = kRootPath / (T::Name() + "_" + tag);
    std::filesystem::remove_all(stream_path_);
    if (!std::filesystem::exists(kRootPath) &&
        !std::filesystem::create_directories(kRootPath)) {
      std::cerr << "Cannot create telemetry stream: " << kRootPath << '\n';
      return;
    }

    stream_ = std::ofstream(stream_path);
    stream_path_ = stream_path;

    {
      const rt::MutexGuard s_lock(stream_lock_);

      stream_ << T::Header() << '\n';
      stream_.flush();
    }
  }

  TelemetryStream() : TelemetryStream(kDefaultTag) {}

  ~TelemetryStream() {
    stop_ = true;
    th_flusher_.Join();
    stream_.close();
  }

  /* No copying. */
  TelemetryStream(const TelemetryStream &) = delete;
  TelemetryStream &operator=(const TelemetryStream &) = delete;

  /* No moving. */
  TelemetryStream(TelemetryStream &&) = delete;
  TelemetryStream &operator=(TelemetryStream &&) = delete;

  void TraceBuffered(T obj) {
    const rt::MutexGuard b_lock(buf_lock_);

    if (buf_idx_ == kMaxBufferEntries) {
      std::cerr << "Telemetry stream is full" << '\n';
      return;
    }

    buf_active_->at(buf_idx_++) = std::move(obj);
  }

  void Trace(T obj) {
    const rt::MutexGuard s_lock(stream_lock_);

    stream_ << obj << '\n';
  }

  std::filesystem::path get_path() const { return stream_path_; }

 private:
  size_t buf_idx_{0};
  std::vector<T> buf_A_;
  std::vector<T> buf_B_;
  std::vector<T> *buf_active_;
  std::vector<T> *buf_inactive_;
  rt::Mutex buf_lock_;

  std::filesystem::path stream_path_;
  std::ofstream stream_;
  rt::Mutex stream_lock_;

  rt::Thread th_flusher_;
  bool stop_{false};
  bool last_empty_{false};

  void Flush() {
    size_t buf_idx = 0;

    {
      const rt::MutexGuard b_lock(buf_lock_);

      buf_idx = buf_idx_;
      buf_idx_ = 0;
      std::swap(buf_active_, buf_inactive_);
    }

    {
      const rt::MutexGuard s_lock(stream_lock_);

      for (size_t i = 0; i < buf_idx; i++) {
        if (buf_inactive_->at(i).IsEmpty()) {
          if (last_empty_) {
            continue;
          }
          last_empty_ = true;
        } else {
          last_empty_ = false;
        }

        stream_ << buf_inactive_->at(i) << '\n';
      }
      stream_.flush();
    }
  }

  void Run() {
    const Duration interval(kUpdateIntervalUs);
    while (!stop_) {
      rt::Sleep(interval);
      Flush();
    }
  }
};

}  // namespace sandook
