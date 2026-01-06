#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "base/compiler.h"
#include "lib/tdigest/TDigest.h"
#include "sandook/base/constants.h"
#include "sandook/base/counter.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/disk_model/disk_model.h"

namespace sandook {

constexpr static auto kLoadCalculationIntervalUs = 5 * kOneMilliSecond;
constexpr static double kLoadScaleFactor =
    static_cast<double>(kOneSecond) /
    static_cast<double>(kLoadCalculationIntervalUs);

constexpr static auto kMaxBufferEntries = 1UZ << 22;
constexpr static auto kFlushIntervalUs = 10 * kOneMilliSecond;
static_assert(kFlushIntervalUs * 2 <= kCongestionControlWindowUs,
              "DiskServer flush interval must be lower than half of CC window");
constexpr static auto kLogIntervalUs = 1 * kOneSecond;

/* If the current latency signal is more than the expected times this factor,
 * then consider the disk to be congested.
 */
constexpr static auto kModelRejectionReadLatencyThreshold = 1.0;
constexpr static auto kModelRejectionWriteLatencyThreshold = 1.0;

constexpr static auto kCongestionLatencyThresholdUs = 2500;
/* Congestion trigger based on (observed / expected) tail-latency ratio. */
constexpr static auto kCongestionSignalRatioThreshold = 2.5;
constexpr static auto kMinExpectedLatencyUs = 1.0;
constexpr static auto kCongestedUnstableFactor = 0.7;
constexpr static auto kCongestedStableFactor = 0.9;

// NOLINTBEGIN(clang-analyzer-optin.performance.Padding)
class DiskMonitor {
 public:
  explicit DiskMonitor() : th_logger_([this]() { Logger(); }) {
    r_buf_active_.resize(kMaxBufferEntries);
    r_buf_inactive_.resize(kMaxBufferEntries);
    w_buf_active_.resize(kMaxBufferEntries);
    w_buf_inactive_.resize(kMaxBufferEntries);

    // Since it depends on r_buf & w_buf, we must construct it after them.
    th_flusher_ = rt::Thread([this]() { Flusher(); });
    th_load_calculator_ = rt::Thread([this]() { LoadCalculator(); });
  }

  ~DiskMonitor() {
    stop_ = true;
    th_flusher_.Join();
    th_load_calculator_.Join();
    th_logger_.Join();
  }

  /* No copying. */
  DiskMonitor(const DiskMonitor &) = delete;
  DiskMonitor &operator=(const DiskMonitor &) = delete;

  /* No moving. */
  DiskMonitor(DiskMonitor &&) noexcept;
  DiskMonitor &operator=(DiskMonitor &&) noexcept;

  void LogSummary() {
    LOG(INFO) << "Pure reads    : " << tot_pure_reads_.get_sum();
    LOG(INFO) << "Pure writes   : " << pure_writes_.get_sum();
    LOG(INFO) << "Mixed reads   : " << mixed_reads_.get_sum();
    LOG(INFO) << "Mixed writes  : " << mixed_writes_.get_sum();
    LOG(INFO) << "Impure reads  : " << tot_impure_reads_.get_sum();
    LOG(INFO) << "Impure writes : " << impure_writes_.get_sum();
    LOG(INFO) << "Failed reads  : " << total_failed_reads_.get_sum();
    LOG(INFO) << "Failed writes : " << total_failed_writes_.get_sum();
  }

  void HandleSignal([[maybe_unused]] int sig) { LogSummary(); }

  void SetServerID(ServerID server_id) { server_id_ = server_id; }

  void SetServerName(const std::string &name) { name_ = name; }

  void SetIsRejectionsEnabled(bool is_rejections_enabled) {
    is_rejections_enabled_ = is_rejections_enabled;
    assert(!name_.empty());
    model_ = DiskModel(name_);
  }

  void SetDiskPeakIOPS(uint64_t read_iops, uint64_t write_iops,
                       uint64_t mix_iops) {
    peak_read_iops_ = read_iops;
    peak_write_iops_ = write_iops;
    peak_mix_iops_ = mix_iops;

    LOG(INFO) << "Peak read IOPS  : " << peak_read_iops_;
    LOG(INFO) << "Peak write IOPS : " << peak_write_iops_;
    LOG(INFO) << "Peak mix IOPS   : " << peak_mix_iops_;
  }

  ServerStats UpdateAndGetServerStats() {
    UpdateStats();
    return GetServerStats();
  }

  [[nodiscard]] ServerStats GetServerStats() const {
    return ServerStats{.server_id = server_id_,
                       .mode = mode_,
                       .read_mops = read_mops_,
                       .write_mops = write_mops_,
                       .read_weight = read_weight_,
                       .write_weight = write_weight_,
                       .inflight_reads = inflight_reads_t_,
                       .inflight_writes = inflight_writes_t_,
                       .completed_reads = completed_reads_t_,
                       .pure_reads = pure_reads_t_,
                       .impure_reads = impure_reads_t_,
                       .completed_writes = completed_writes_t_,
                       .rejected_reads = rejected_reads_t_,
                       .rejected_writes = rejected_writes_t_,
                       .median_read_latency = median_read_latency_td_,
                       .median_write_latency = median_write_latency_td_,
                       .signal_read_latency = signal_read_latency_,
                       .signal_write_latency = signal_write_latency_,
                       .is_rejecting_requests = false,
                       .congestion_state = congestion_state_};
  }

  uint64_t ReadStarted() {
    inflight_reads_.inc_local();
    return MicroTime();
  }

  void ReadCompleted(uint64_t start_time, bool success) {
    auto duration_us = MicroTime() - start_time;
    inflight_reads_.dec_local();
    disk_reads_.inc_local();

    if (success) {
      completed_reads_.inc_local();
      if (mode_ == ServerMode::kRead) {
        pure_reads_.inc_local();
      } else if (mode_ == ServerMode::kWrite) {
        impure_reads_.inc_local();
      } else {
        mixed_reads_.inc_local();
      }
    } else {
      total_failed_reads_.inc_local();
      failed_reads_.inc_local();
    }

    // Intentionally being (safely!) racy below for better scalability.
    auto idx = r_buf_idx_++;
    if (unlikely(idx >= kMaxBufferEntries)) {
      LOG(WARN) << "Buffer is full" << '\n';
      return;
    }

    r_buf_active_[idx] = duration_us;
  }

  uint64_t WriteStarted() {
    inflight_writes_.inc_local();
    return MicroTime();
  }

  void WriteCompleted(uint64_t start_time, bool success) {
    auto duration_us = MicroTime() - start_time;
    inflight_writes_.dec_local();
    disk_writes_.inc_local();

    if (success) {
      completed_writes_.inc_local();
      if (mode_ == ServerMode::kWrite) {
        pure_writes_.inc_local();
      } else if (mode_ == ServerMode::kRead) {
        impure_writes_.inc_local();
      } else {
        mixed_writes_.inc_local();
      }
    } else {
      total_failed_writes_.inc_local();
      failed_writes_.inc_local();
    }

    // Intentionally being (safely!) racy below for better scalability.
    auto idx = w_buf_idx_++;
    if (unlikely(idx >= kMaxBufferEntries)) {
      LOG(WARN) << "Buffer is full" << '\n';
      return;
    }

    w_buf_active_[idx] = duration_us;
  }

  void SetModeAndWeights(ServerMode mode, ServerWeight read_weight,
                         ServerWeight write_weight) {
    if (mode != mode_) {
      mode_switch_time_us_ = MicroTime();
    }
    mode_ = mode;
    read_weight_ = read_weight;
    write_weight_ = write_weight;
  }

  [[nodiscard]] ServerMode GetMode() const { return mode_; }

  [[nodiscard]] bool IsModeSwitchGracePeriod() const {
    const auto time_since_mode_switch_us_ = MicroTime() - mode_switch_time_us_;
    return time_since_mode_switch_us_ <= kDiskServerModeSwitchGracePeriodUs;
  }

  void ReadRejected() { rejected_reads_.inc_local(); }
  void WriteRejected() { rejected_writes_.inc_local(); }

  [[nodiscard]] bool IsCongested() const {
    return congestion_state_ == ServerCongestionState::kCongested;
  }

  [[nodiscard]] bool IsAllowingWrites() const {
    return mode_ != ServerMode::kRead || IsModeSwitchGracePeriod();
  }

 private:
  ServerID server_id_{kInvalidServerID};
  std::string name_;
  bool is_rejections_enabled_{false};
  DiskModel model_;

  std::atomic_size_t r_buf_idx_{0};
  std::vector<uint64_t> r_buf_active_;
  std::vector<uint64_t> r_buf_inactive_;

  std::atomic_size_t w_buf_idx_{0};
  std::vector<uint64_t> w_buf_active_;
  std::vector<uint64_t> w_buf_inactive_;

  /* Disk properties. */
  uint64_t peak_read_iops_{std::numeric_limits<uint64_t>::max()};
  uint64_t peak_write_iops_{std::numeric_limits<uint64_t>::max()};
  uint64_t peak_mix_iops_{std::numeric_limits<uint64_t>::max()};

  /* Summary stats. */
  ThreadSafeCounter pure_reads_;
  ThreadSafeCounter tot_pure_reads_;
  ThreadSafeCounter pure_writes_;
  ThreadSafeCounter impure_reads_;
  ThreadSafeCounter tot_impure_reads_;
  ThreadSafeCounter impure_writes_;
  ThreadSafeCounter mixed_reads_;
  ThreadSafeCounter mixed_writes_;
  ThreadSafeCounter total_failed_reads_;
  ThreadSafeCounter total_failed_writes_;

  /* Runtime stats. */
  ServerMode mode_{ServerMode::kMix};
  uint64_t mode_switch_time_us_{0};
  ServerWeight read_weight_{0.0};
  ServerWeight write_weight_{0.0};
  ThreadSafeCounter inflight_reads_;
  ThreadSafeCounter inflight_writes_;
  ThreadSafeCounter completed_reads_;
  ThreadSafeCounter completed_writes_;
  ThreadSafeCounter rejected_reads_;
  ThreadSafeCounter rejected_writes_;
  ThreadSafeCounter failed_reads_;
  ThreadSafeCounter failed_writes_;
  ThreadSafeCounter disk_reads_;
  ThreadSafeCounter disk_writes_;

  /* Background computed stats. */
  bool is_rejecting_requests_{false};
  uint32_t completed_reads_t_{0};
  uint32_t pure_reads_t_{0};
  uint32_t impure_reads_t_{0};
  uint32_t completed_writes_t_{0};
  uint32_t inflight_reads_t_{0};
  uint32_t inflight_writes_t_{0};
  uint32_t rejected_reads_t_{0};
  uint32_t rejected_writes_t_{0};
  double read_mops_{0.0};
  double write_mops_{0.0};
  uint64_t read_load_ops_{0};
  uint64_t write_load_ops_{0};
  uint64_t total_load_ops_{0};
  double write_ratio_{0.0};

  /* Used for determining rejection status. */
  ServerCongestionState congestion_state_{ServerCongestionState::kUnCongested};
  uint64_t median_read_latency_td_{0};
  uint64_t p90_read_latency_td_{0};
  uint64_t p99_read_latency_td_{0};
  uint64_t median_write_latency_td_{0};
  uint64_t p90_write_latency_td_{0};
  uint64_t p99_write_latency_td_{0};
  uint64_t signal_read_latency_{0};
  uint64_t signal_write_latency_{0};
  uint64_t state_transition_read_latency_{0};
  uint64_t state_transition_write_latency_{0};
  double signal_ratio_{0.0};
  double state_transition_ratio_{0.0};
  tdigest::TDigest td_reads_;
  tdigest::TDigest td_writes_;

  uint64_t last_stats_us_{0};
  rt::Thread th_flusher_;
  rt::Thread th_load_calculator_;
  rt::Thread th_logger_;
  bool stop_{false};

  void UpdateStats() {
    const auto elapsed_us = MicroTime() - last_stats_us_;

    completed_reads_t_ = completed_reads_.get_sum_and_reset();
    completed_writes_t_ = completed_writes_.get_sum_and_reset();
    pure_reads_t_ = pure_reads_.get_sum_and_reset();
    impure_reads_t_ = impure_reads_.get_sum_and_reset();
    tot_pure_reads_.inc_local(pure_reads_t_);
    tot_impure_reads_.inc_local(impure_reads_t_);

    inflight_reads_t_ = inflight_reads_.get_sum();
    inflight_writes_t_ = inflight_writes_.get_sum();

    rejected_reads_t_ = rejected_reads_.get_sum_and_reset();
    rejected_writes_t_ = rejected_writes_.get_sum_and_reset();

    last_stats_us_ = MicroTime();

    read_mops_ = static_cast<double>(completed_reads_t_) /
                 static_cast<double>(elapsed_us);
    write_mops_ = static_cast<double>(completed_writes_t_) /
                  static_cast<double>(elapsed_us);
  }

  void FlushReads() {
    size_t buf_idx = 0;
    buf_idx = r_buf_idx_.exchange(buf_idx);
    buf_idx = std::min(buf_idx, kMaxBufferEntries);
    r_buf_active_.swap(r_buf_inactive_);

    td_reads_ = tdigest::TDigest();
    for (size_t i = 0; i < buf_idx; i++) {
      const auto lat = r_buf_inactive_[i];
      td_reads_.add(lat);
    }

    median_read_latency_td_ = td_reads_.quantile(kP50);
    p90_read_latency_td_ = td_reads_.quantile(kP90);
    p99_read_latency_td_ = td_reads_.quantile(kP99);

    signal_read_latency_ = p99_read_latency_td_;
  }

  void FlushWrites() {
    size_t buf_idx = 0;
    buf_idx = w_buf_idx_.exchange(buf_idx);
    buf_idx = std::min(buf_idx, kMaxBufferEntries);
    w_buf_active_.swap(w_buf_inactive_);

    td_writes_ = tdigest::TDigest();
    for (size_t i = 0; i < buf_idx; i++) {
      const auto lat = w_buf_inactive_[i];
      td_writes_.add(lat);
    }

    median_write_latency_td_ = td_writes_.quantile(kP50);
    p90_write_latency_td_ = td_writes_.quantile(kP90);
    p99_write_latency_td_ = td_writes_.quantile(kP99);

    signal_write_latency_ = p99_write_latency_td_;
  }

  void Flush() {
    FlushReads();
    FlushWrites();
  }

  void Flusher() {
    const Duration interval(kFlushIntervalUs);

    while (!stop_) {
      rt::Sleep(interval);
      Flush();
      UpdateCongestionState();
    }
  }

  void CalculateLoad() {
    uint64_t disk_reads = disk_reads_.get_sum_and_reset();
    uint64_t disk_writes = disk_writes_.get_sum_and_reset();

    const double read_load_ops =
        static_cast<double>(disk_reads) * kLoadScaleFactor;
    read_load_ops_ = static_cast<uint64_t>(read_load_ops);
    const double write_load_ops =
        static_cast<double>(disk_writes) * kLoadScaleFactor;
    write_load_ops_ = static_cast<uint64_t>(write_load_ops);

    total_load_ops_ = read_load_ops_ + write_load_ops_;
    if (total_load_ops_ > 0) {
      write_ratio_ = static_cast<double>(write_load_ops_) /
                     static_cast<double>(total_load_ops_);
    } else {
      write_ratio_ = 0.0;
    }
  }

  void LoadCalculator() {
    const Duration interval(kLoadCalculationIntervalUs);

    while (!stop_) {
      rt::Sleep(interval);
      CalculateLoad();
    }
  }

  void UpdateCongestionState() {
    if (!is_rejections_enabled_) {
      return;
    }

    if (IsModeSwitchGracePeriod()) {
      congestion_state_ = ServerCongestionState::kUnCongested;
      return;
    }

    /* Build a more robust congestion signal:
     * - Compare observed tail latency against the model's expected tail latency
     *   for the current load and mode.
     * - Use matching percentiles: mix models are generated from p90, whereas
     *   pure read/write models are generated from p99.
     * - Consider both reads and writes, since either can be the bottleneck.
     */
    const auto exp_read_lat = static_cast<double>(std::max<uint64_t>(
        model_.GetLatency(total_load_ops_, OpType::kRead, mode_, write_ratio_),
        static_cast<uint64_t>(kMinExpectedLatencyUs)));
    const auto exp_write_lat = static_cast<double>(std::max<uint64_t>(
        model_.GetLatency(total_load_ops_, OpType::kWrite, mode_, write_ratio_),
        static_cast<uint64_t>(kMinExpectedLatencyUs)));

    const auto obs_read_sig = static_cast<double>(
        (mode_ == ServerMode::kRead) ? p99_read_latency_td_ : p90_read_latency_td_);
    const auto obs_write_sig = static_cast<double>(
        (mode_ == ServerMode::kWrite) ? p99_write_latency_td_ : p90_write_latency_td_);

    const auto read_ratio = obs_read_sig / exp_read_lat;
    const auto write_ratio = obs_write_sig / exp_write_lat;
    signal_ratio_ = std::max(read_ratio, write_ratio);

    const bool is_congested_abs =
        (signal_read_latency_ > kCongestionLatencyThresholdUs) ||
        (signal_write_latency_ > kCongestionLatencyThresholdUs);
    const bool is_congested_ratio =
        signal_ratio_ > kCongestionSignalRatioThreshold;
    if (is_congested_abs || is_congested_ratio) {
      // * --> kCongested
      congestion_state_ = ServerCongestionState::kCongested;
      return;
    }

    // kCongested --> kCongestedUnstable
    if (congestion_state_ == ServerCongestionState::kCongested) {
      /* No more congestion.
       * Transition into additive increase; expected signal does not change
       * because multiplicitive decrease pushes the signal down below it.
       * Now we want to ramp back up close to it.
       */
      congestion_state_ = ServerCongestionState::kCongestedUnstable;
      state_transition_ratio_ = kCongestedUnstableFactor;
      return;
    }

    // kCongestedUnstable --> ?
    if (congestion_state_ == ServerCongestionState::kCongestedUnstable) {
      // kCongestedUnstable --> kCongestedStable
      if (signal_ratio_ > state_transition_ratio_) {
        congestion_state_ = ServerCongestionState::kCongestedStable;
        /* Stop doing additive increase.
         * Next transition is when we fall back on the model.
         */
        state_transition_ratio_ = kCongestedStableFactor;
        return;
      }

      // kCongestedUnstable --> kCongestedUnstable
      /* Keep doing additive increase. Nothing to do. */
      return;
    }

    // kCongestedStable --> ?
    if (congestion_state_ == ServerCongestionState::kCongestedStable) {
      // kCongestedStable --> kUnCongested
      if (signal_ratio_ < state_transition_ratio_) {
        congestion_state_ = ServerCongestionState::kUnCongested;
      }

      // kCongestedStable --> kCongestedStable
      /* Still congested but cannot handle more load. Nothing to do. */
      return;
    }

    // kUnCongested --> kUnCongested
    if (congestion_state_ == ServerCongestionState::kUnCongested) {
      /* Nothing to do. */
      return;
    }
  }

  void Log() {
    LOG(INFO) << "ServerID           : " << server_id_;
    LOG(INFO) << "Mode               : "
              << ((mode_ == ServerMode::kRead)    ? "Read"
                  : (mode_ == ServerMode::kWrite) ? "Write"
                                                  : "Mix");

    LOG(INFO) << "Inflight reads     : " << inflight_reads_t_;
    LOG(INFO) << "Inflight writes    : " << inflight_writes_t_;

    LOG(INFO) << "Completed reads    : " << completed_reads_t_;
    LOG(INFO) << "Completed writes   : " << completed_writes_t_;

    LOG(INFO) << "CongestionState    : " << congestion_state_;

    LOG(INFO) << "Rejected reads     : " << rejected_reads_t_;
    LOG(INFO) << "Rejected writes    : " << rejected_writes_t_;

    LOG(INFO) << "Signal (R)         : " << signal_read_latency_;
    LOG(INFO) << "Signal (W)         : " << signal_write_latency_;

    LOG(INFO) << "Read OPS           : " << read_load_ops_;
    LOG(INFO) << "Write OPS          : " << write_load_ops_;
    LOG(INFO) << "Total OPS          : " << total_load_ops_;
    LOG(INFO) << "Write ratio        : " << write_ratio_;

    LOG(INFO) << "============\n";
  }

  void Logger() {
    const Duration interval(kLogIntervalUs);
    while (!stop_) {
      rt::Sleep(interval);
      Log();
    }
  }
};
// NOLINTEND(clang-analyzer-optin.performance.Padding)

}  // namespace sandook
