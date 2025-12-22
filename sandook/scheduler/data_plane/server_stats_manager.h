#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/rcu.h"
#include "sandook/bindings/sync.h"
#include "sandook/scheduler/data_plane/congestion_control.h"

constexpr static auto kMinReadServers = 1;

namespace sandook::schedulers::data_plane {

constexpr static auto kDataPlaneLoggingIntervalUs = 1 * kOneSecond;

class ServerStatsManager {
 public:
  ServerStatsManager(VolumeID vol_id)
      : vol_id_(vol_id), th_stats_logger_([this]() { StatsLogger(); }) {
    cc_ = std::make_unique<CongestionControl>(vol_id);
    InitServerWeights(read_weights_);
    InitServerWeights(write_weights_);
  }

  ~ServerStatsManager() {
    stop_ = true;
    th_stats_logger_.Join();
  }

  /* No copying. */
  ServerStatsManager(const ServerStatsManager &) = delete;
  ServerStatsManager &operator=(const ServerStatsManager &) = delete;

  /* No moving. */
  ServerStatsManager(ServerStatsManager &&) noexcept;
  ServerStatsManager &operator=(ServerStatsManager &&) noexcept;

  Status<void> AddServer(ServerID server_id) {
    const auto ret = servers_.insert(server_id);
    if (!ret.second) {
      return MakeError(EALREADY);
    }
    return cc_->AddServer(server_id);
  }

  void SignalCongested(ServerID server_id) { cc_->SignalCongested(server_id); }

  void SetCongestionState(ServerID server_id, ServerCongestionState state) {
    cc_->SetCongestionState(server_id, state);
  }

  Status<void> SetServerStats(const ServerStatsList &servers) {
    std::ranges::for_each(servers, [&](const auto &srv) {
      assert(servers_.find(srv.server_id) != servers_.end());
      modes_.at(srv.server_id) = srv.committed_mode;
      read_weights_.at(srv.server_id) = srv.read_weight;
      write_weights_.at(srv.server_id) = srv.write_weight;
      SetCongestionState(srv.server_id, srv.congestion_state);
    });
    return {};
  }

  [[nodiscard]] Status<ServerWeights> GetReadOnlyWeights() const {
    auto all_read_weights = GetRateLimitedReadWeights();

    /* 1) Find servers that are in read mode. */
    ServerWeights filtered_read_weights;
    InitServerWeights(filtered_read_weights);

    size_t n_found = 0;
    for (auto server_id : servers_) {
      if (modes_.at(server_id) != ServerMode::kRead) {
        continue;
      }
      n_found++;
      filtered_read_weights.at(server_id) = all_read_weights.at(server_id);
    }
    if (n_found >= kMinReadServers) {
      return filtered_read_weights;
    }

    /* 2) If all servers were filtered out, then just return all servers and
     * hope that the request lands on a server accepting reads.
     * Otherwise this will be tried again.
     */
    return all_read_weights;
  }

  [[nodiscard]] Status<ServerWeights> GetAllReadWeights() const {
    return GetRateLimitedReadWeights();
  }

  [[nodiscard]] Status<ServerWeights> GetWriteWeights() const {
    auto all_write_weights = GetRateLimitedWriteWeights();

    /* 1) Find servers that are not in read mode. */
    ServerWeights filtered_write_weights;
    InitServerWeights(filtered_write_weights);

    size_t n_found = 0;
    for (auto server_id : servers_) {
      if (modes_.at(server_id) == ServerMode::kRead) {
        continue;
      }
      n_found++;
      filtered_write_weights.at(server_id) = all_write_weights.at(server_id);
    }
    if (n_found >= kNumReplicas) {
      return filtered_write_weights;
    }

    /* 2) If all servers were filtered out, then just return all servers and
     * hope that the request lands on a server accepting writes.
     * Otherwise this will be tried again.
     */
    return all_write_weights;
  }

 private:
  VolumeID vol_id_;
  ServerSet servers_;
  ServerModes modes_;
  ServerWeights read_weights_;
  ServerWeights write_weights_;
  std::unique_ptr<CongestionControl> cc_;

  bool stop_{false};
  rt::Thread th_stats_logger_;

  [[nodiscard]] ServerWeights GetRateLimitedReadWeights() const {
    ServerWeights weights = read_weights_;
    for (auto server_id : servers_) {
      weights.at(server_id) *= cc_->GetRateLimit(server_id);
    }
    return weights;
  }

  [[nodiscard]] ServerWeights GetRateLimitedWriteWeights() const {
    ServerWeights weights = write_weights_;
    for (auto server_id : servers_) {
      weights.at(server_id) *= cc_->GetRateLimit(server_id);
    }
    return weights;
  }

  void LogStats() {
    for (const auto server_id : servers_) {
      const auto cc = cc_->GetRateLimit(server_id);
      const auto r_w = read_weights_.at(server_id);
      const auto w_w = write_weights_.at(server_id);
      const auto cc_r_w = r_w * cc;
      const auto cc_w_w = w_w * cc;
      LOG(DEBUG) << "ServerID: " << server_id;
      LOG(DEBUG) << "\tReadWeight: " << r_w;
      LOG(DEBUG) << "\tWriteWeight: " << w_w;
      LOG(DEBUG) << "\tRateLimitedReadWeight: " << cc_r_w;
      LOG(DEBUG) << "\tRateLimitedWriteWeight: " << cc_w_w;
      LOG(DEBUG) << "\tRateLimitingFactor: " << cc;
    }
    if (!servers_.empty()) {
      LOG(DEBUG) << "============\n";
    }
  }

  void StatsLogger() {
    const Duration interval(kDataPlaneLoggingIntervalUs);

    while (!stop_) {
      rt::Sleep(interval);
      LogStats();
    }
  }
};

}  // namespace sandook::schedulers::data_plane
