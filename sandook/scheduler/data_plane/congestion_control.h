#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/timer.h"
#include "sandook/telemetry/cc_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"
#include "sandook/utils/calibrated_time.h"

namespace sandook::schedulers::data_plane {

using TelemetryMap =
    std::array<std::unique_ptr<TelemetryStream<CongestionControlTelemetry>>,
               kNumMaxServers>;

constexpr static auto kBestCongestedRateLimit = 0.001;
constexpr static auto kBestUncongestedRateLimit = 1.0;

constexpr static auto kMultiplicativeDecreaseDelta = 0.5;
constexpr static auto kAdditiveIncreaseDelta = 0.05;

class CongestionControl {
 public:
  CongestionControl(VolumeID vol_id) : vol_id_(vol_id) {
    for (auto &state : states_) {
      state = ServerCongestionState::kUnCongested;
    }
    for (auto &factor : cc_rate_limits_) {
      factor = kBestUncongestedRateLimit;
    }
    for (auto &congested_at : congested_at_) {
      congested_at = 0;
    }
    for (auto &responded_at : congestion_responded_at_) {
      responded_at = 0;
    }
    th_cc_ = [this] { CongestionControlWorker(); };
  }

  ~CongestionControl() {
    stop_ = true;
    th_cc_.Join();
  }

  /* No copying. */
  CongestionControl(const CongestionControl &) = delete;
  CongestionControl &operator=(const CongestionControl &) = delete;

  /* No moving. */
  CongestionControl(CongestionControl &&other) = delete;
  CongestionControl &operator=(CongestionControl &&other) = delete;

  Status<void> AddServer(ServerID server_id) {
    const auto telemetry_tag =
        "vol_" + std::to_string(vol_id_) + "_disk_" + std::to_string(server_id);
    telemetry_map_.at(server_id) =
        std::make_unique<TelemetryStream<CongestionControlTelemetry>>(
            telemetry_tag);
    telemetry_map_.at(server_id)->TraceBuffered(CongestionControlTelemetry(
        utils::CalibratedMicroTime(), ServerCongestionState::kUnCongested,
        kBestUncongestedRateLimit));

    const auto ret = servers_.insert(server_id);
    if (!ret.second) {
      return MakeError(EALREADY);
    }

    return {};
  }

  void SignalCongested(ServerID server_id) {
    auto cur_state = states_.at(server_id);

    /* Fast-path for congestion avoidance. */
    if (cur_state == ServerCongestionState::kUnCongested) {
      const rt::MutexGuard lock(cc_lock_);

      /* Check the state again after holding the lock. */
      cur_state = states_.at(server_id);
      if (cur_state != ServerCongestionState::kUnCongested) {
        return;
      }

      states_.at(server_id) = ServerCongestionState::kCongested;
      congested_at_.at(server_id) = MicroTime();
      UpdateServerRateLimit(server_id);
    } else {
      states_.at(server_id) = ServerCongestionState::kCongested;
      congested_at_.at(server_id) = MicroTime();
    }
  }

  void SetCongestionState(ServerID server_id, ServerCongestionState state) {
    const auto cur_state = states_.at(server_id);
    if (cur_state != ServerCongestionState::kInvalid) {
      return;
    }
    states_.at(server_id) = state;
  }

  [[nodiscard]] RateLimit GetRateLimit(ServerID server_id) const {
    return cc_rate_limits_.at(server_id);
  }

 private:
  VolumeID vol_id_;
  ServerSet servers_;
  std::array<ServerCongestionState, kNumMaxServers> states_;
  std::array<uint64_t, kNumMaxServers> congested_at_;
  std::array<uint64_t, kNumMaxServers> congestion_responded_at_;
  std::array<RateLimit, kNumMaxServers> cc_rate_limits_;
  TelemetryMap telemetry_map_;

  bool stop_{false};
  rt::Mutex cc_lock_;
  rt::Thread th_cc_;
  rt::Thread th_stats_logger_;

  void UpdateServerRateLimit(ServerID server_id) {
    auto state = states_.at(server_id);
    auto cc_rate_limit = cc_rate_limits_.at(server_id);

    switch (state) {
      case ServerCongestionState::kUnCongested: {
        if (cc_rate_limit != kBestUncongestedRateLimit) {
          cc_rate_limit += kAdditiveIncreaseDelta;
          cc_rate_limit = std::min(kBestUncongestedRateLimit, cc_rate_limit);

          LOG(DEBUG) << server_id << " Uncongested: " << cc_rate_limit;
        }

      } break;

      case ServerCongestionState::kCongestedUnstable: {
        if (cc_rate_limit != kBestUncongestedRateLimit) {
          cc_rate_limit += kAdditiveIncreaseDelta;
          cc_rate_limit = std::min(kBestUncongestedRateLimit, cc_rate_limit);

          LOG(DEBUG) << server_id << " CongestedUnstable: " << cc_rate_limit;
        }
      } break;

      case ServerCongestionState::kCongested: {
        if (cc_rate_limit != kBestCongestedRateLimit) {
          const auto last_reaction = congestion_responded_at_.at(server_id);
          const auto congested_since = congested_at_.at(server_id);
          if (last_reaction >= congested_since) {
            /* No congestion signal was received after the last reaction. */
            break;
          }

          const auto reaction_delay = congested_since - last_reaction;
          if (reaction_delay < kCongestionControlWindowUs) {
            /* The previous action may not have taken impact yet. */
            break;
          }

          cc_rate_limit *= kMultiplicativeDecreaseDelta;
          cc_rate_limit = std::max(kBestCongestedRateLimit, cc_rate_limit);

          congestion_responded_at_.at(server_id) = MicroTime();

          LOG(DEBUG) << server_id << " Congested: " << cc_rate_limit;
        }
      } break;

      default:
        break;
    }

    telemetry_map_.at(server_id)->TraceBuffered(CongestionControlTelemetry(
        utils::CalibratedMicroTime(), state, cc_rate_limit));

    cc_rate_limits_.at(server_id) = cc_rate_limit;
  }

  void UpdateAllServers() {
    const rt::MutexGuard lock(cc_lock_);

    for (auto server_id : servers_) {
      UpdateServerRateLimit(server_id);
      states_.at(server_id) = ServerCongestionState::kInvalid;
    }
  }

  void CongestionControlWorker() {
    const Duration cc_window(kCongestionControlWindowUs);

    while (!stop_) {
      UpdateAllServers();
      rt::Sleep(cc_window);
    }
  }
};

}  // namespace sandook::schedulers::data_plane
