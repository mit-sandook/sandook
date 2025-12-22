#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <utility>

#include "sandook/base/constants.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/telemetry/system_load_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"

namespace sandook::schedulers::control_plane {

constexpr static auto kControllerLoggingIntervalUs = 1 * kOneSecond;
constexpr static auto kLoadCalculationIntervalUs = 10 * kOneMilliSecond;

constexpr static double kLoadScaleFactor =
    static_cast<double>(kOneSecond) /
    static_cast<double>(kLoadCalculationIntervalUs);

using ServerStatsMap = std::array<std::unique_ptr<ServerStats>, kNumMaxServers>;

class ServerStatsManager {
 public:
  ServerStatsManager()
      : th_load_calculator_([this]() { LoadCalculator(); }),
        th_stats_logger_([this]() { StatsLogger(); }) {}

  ~ServerStatsManager() {
    Stop();
    th_load_calculator_.Join();
    th_stats_logger_.Join();
  }

  /* No copying. */
  ServerStatsManager(const ServerStatsManager &) = delete;
  ServerStatsManager &operator=(const ServerStatsManager &) = delete;

  /* No moving. */
  ServerStatsManager(ServerStatsManager &&) noexcept;
  ServerStatsManager &operator=(ServerStatsManager &&) noexcept;

  void HandleSignal([[maybe_unused]] int sig) {}

  void AddServer(ServerID server_id, [[maybe_unused]] const std::string &name) {
    /* Create default stats for the server. */
    auto stats = std::make_unique<ServerStats>();
    stats->server_id = server_id;
    stats->mode = ServerMode::kMix;
    stats->read_weight = kDefaultServerWeight;
    stats->write_weight = kDefaultServerWeight;

    stats_map_.at(server_id) = std::move(stats);

    servers_.insert(server_id);
  }

  SystemLoad GetSystemLoad() const {
    // Intentionally being (safely!) racy below for better scalability.
    return {read_ops_, write_ops_};
  }

  ServerStatsList GetServerStats() {
    const std::shared_lock lock(stats_lock_);

    const auto num_servers = servers_.size();
    const auto stats_v =
        std::views::all(stats_map_) | std::views::drop(1) |
        std::ranges::views::take(num_servers) |
        std::views::transform([&](const auto &server) { return *server; });

    return {stats_v.cbegin(), stats_v.cend()};
  }

  ServerStats GetServerStats(ServerID server_id) {
    const std::shared_lock lock(stats_lock_);

    return *(stats_map_.at(server_id));
  }

  DataPlaneServerStats GetDataPlaneServerStats(ServerID server_id) {
    const auto stats = GetServerStats(server_id);
    return {stats.mode, stats.congestion_state, stats.read_weight,
            stats.write_weight};
  }

  void UpdateServerStats(ServerID server_id, ServerStats stats,
                         bool is_override, bool is_update_load) {
    assert(server_id == stats.server_id);  // NOLINT
    auto new_stats = std::make_unique<ServerStats>(stats);

    /* Restore back the parameters we do not want to modify. */
    if (!is_override) {
      const std::shared_lock lock(stats_lock_);

      const auto *old_stats = stats_map_.at(server_id).get();
      new_stats->mode = old_stats->mode;
      new_stats->read_weight = old_stats->read_weight;
      new_stats->write_weight = old_stats->write_weight;
    }

    const auto reads = stats.completed_reads + stats.rejected_reads;
    const auto writes = stats.completed_writes + stats.rejected_writes;

    {
      const std::unique_lock lock(stats_lock_);

      if (is_update_load) {
        stats_system_reads_ += reads;
        stats_system_writes_ += writes;
      }

      stats_map_.at(server_id) = std::move(new_stats);
    }
  }

  void CommitServerMode(ServerID server_id, ServerMode mode) {
    const std::unique_lock lock(stats_lock_);
    stats_map_.at(server_id)->committed_mode = mode;
  }

  void Stop() { stop_ = true; }

  void FreezeLoad() { freeze_load_ = true; }

 private:
  TelemetryStream<SystemLoadTelemetry> telemetry_;

  ServerSet servers_;
  ServerStatsMap stats_map_;
  uint64_t stats_system_reads_{0};
  uint64_t stats_system_writes_{0};
  rt::SharedMutex stats_lock_;

  uint64_t read_ops_{0};
  uint64_t write_ops_{0};

  bool stop_{false};
  bool freeze_load_{false};
  rt::Thread th_load_calculator_;
  rt::Thread th_stats_logger_;

  void CalculateLoad() {
    uint64_t system_reads = 0;
    uint64_t system_writes = 0;

    {
      const std::unique_lock lock(stats_lock_);

      system_reads = stats_system_reads_;
      system_writes = stats_system_writes_;

      if (!freeze_load_) {
        stats_system_reads_ = 0;
        stats_system_writes_ = 0;
      }
    }

    double read_ops = static_cast<double>(system_reads) * kLoadScaleFactor;
    read_ops_ = static_cast<uint64_t>(read_ops);
    double write_ops = static_cast<double>(system_writes) * kLoadScaleFactor;
    write_ops_ = static_cast<uint64_t>(write_ops);

    telemetry_.TraceBuffered(SystemLoadTelemetry({read_ops_, write_ops_}));
  }

  void LoadCalculator() {
    const Duration interval(kLoadCalculationIntervalUs);

    while (!stop_) {
      rt::Sleep(interval);
      CalculateLoad();
    }
  }

  void LogStats() {
    for (const auto server_id : servers_) {
      const auto [mode, load, r_w, w_w] = GetDataPlaneServerStats(server_id);
      LOG(DEBUG) << "ServerID: " << server_id;
      LOG(DEBUG) << "\tMode: "
                 << ((mode == ServerMode::kRead)    ? "Read"
                     : (mode == ServerMode::kWrite) ? "Write"
                                                    : "Mix");
      LOG(DEBUG) << "\tIsAcceptingLoad: " << load;
      LOG(DEBUG) << "\tReadWeight: " << r_w;
      LOG(DEBUG) << "\tWriteWeight: " << w_w;
    }
    if (!servers_.empty()) {
      LOG(DEBUG) << "============\n";
    }
  }

  void StatsLogger() {
    const Duration interval(kControllerLoggingIntervalUs);

    while (!stop_) {
      rt::Sleep(interval);
      LogStats();
    }
  }
};

}  // namespace sandook::schedulers::control_plane
