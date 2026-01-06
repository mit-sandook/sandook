#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>
#include <ranges>  // NOLINT
#include <string>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/disk_model/disk_model.h"
#include "sandook/scheduler/control_plane/base_scheduler.h"

namespace sandook::schedulers::control_plane {

class ProfileGuidedRWIsolation;

constexpr auto kMinWeightChangeIntervalUs = 5 * kOneSecond;

constexpr auto kStaticWeightPeakLoadThreshold = 0.95;
constexpr auto kStableMeanBound = 0.05;
constexpr auto kMaxIterativeMethodMeanBound = 1.0;
constexpr auto kMaxIterations = 5000;
constexpr auto kBias = 0.005;
constexpr auto kMinWeight = 0.0;
constexpr auto kMaxWeight = 1.0;

class ProfileGuided : public BaseScheduler {
 public:
  ProfileGuided() : rand_gen_(rand_dev_()) {}
  ~ProfileGuided() override = default;

  /* No copying. */
  ProfileGuided(const ProfileGuided &) = delete;
  ProfileGuided &operator=(const ProfileGuided &) = delete;

  /* No moving. */
  ProfileGuided(ProfileGuided &&other) = delete;
  ProfileGuided &operator=(ProfileGuided &&other) = delete;

  Status<void> AddServer(ServerID server_id, const std::string &name,
                         const DiskModel *model) override {
    if (model != nullptr) {
      models_.at(server_id) = *model;
    } else {
      models_.at(server_id) = sandook::DiskModel(name);
      LOG(INFO) << "Model added for: " << name;
      LOG(INFO) << "Peak IOPS (read): "
                << models_.at(server_id).GetPeakIOPS(ServerMode::kRead);
      LOG(INFO) << "Peak IOPS (write): "
                << models_.at(server_id).GetPeakIOPS(ServerMode::kWrite);
      LOG(INFO) << "Peak IOPS (mix): "
                << models_.at(server_id).GetPeakIOPS(ServerMode::kMix);
    }

    return {};
  }

  bool UseIterativeMethod(const ServerStatsList &stats, OpType op,
                          const SystemLoad load) {
    uint64_t total_iops_capacity = 0;
    std::ranges::for_each(stats, [&](const auto &srv) {
      const auto server_id = srv.server_id;
      const auto &model = models_.at(server_id);
      /* Since we allow mixes in write mode, treat that as kMix for computing
       * peak load.
       */
      const auto peak_mode = srv.mode == ServerMode::kRead ? ServerMode::kRead
                                                           : ServerMode::kWrite;
      const auto srv_load = model.GetPeakIOPS(peak_mode);
      total_iops_capacity += srv_load;
    });

    const auto [read_ops, write_ops] = load;
    const double threshold = static_cast<double>(total_iops_capacity) *
                             kStaticWeightPeakLoadThreshold;
    if (op == OpType::kRead) {
      return static_cast<double>(read_ops) < threshold;
    }
    return static_cast<double>(write_ops) < threshold;
  }

  Status<ServerWeights> ComputeWeights(const ServerStatsList &stats, OpType op,
                                       const SystemLoad load) override {
    if (UseIterativeMethod(stats, op, load)) {
      auto bound = kStableMeanBound;
      while (bound < kMaxIterativeMethodMeanBound) {
        auto ret = ComputeWeightsIterative(stats, op, load, bound);
        if (ret) {
          return *ret;
        }
        bound += kStableMeanBound;
      }
    }

    return ComputeWeightsFromPeak(stats, op, load);
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
  sandook::DiskModels models_;

  static void ResetWeights(ServerWeights *weights,
                           const ServerStatsList *stats) {
    const auto num_servers = stats->size();
    const auto default_weight = 1.0 / static_cast<double>(num_servers);
    std::ranges::for_each(*stats, [&weights, default_weight](const auto &srv) {
      weights->at(srv.server_id) = default_weight;
    });
  }

  static double GetResidualLoad(const ServerStats &server, OpType op) {
    static const double scale_factor =
        static_cast<double>(kOneSecond) / kDiskServerStatsUpdateIntervalUs;

    if (server.mode == ServerMode::kRead) {
      return 0;
    }
    auto residual_load = 0.0;
    if (op == OpType::kRead) {
      residual_load = server.inflight_writes;
    } else {
      residual_load = server.inflight_reads;
    }
    return static_cast<double>(residual_load) * scale_factor;
  }

  Status<ServerWeights> ComputeWeightsIterative(const ServerStatsList &stats,
                                                OpType op,
                                                const SystemLoad load,
                                                double bound) {
    const size_t num_servers = stats.size();

    /* Nothing to stabilize with less than 2 servers. */
    if (num_servers < 2) {
      return MakeError(EAGAIN);
    }

    const auto [read_ops, write_ops] = load;
    const auto total_ops = read_ops + write_ops;
    const double write_ratio =
        static_cast<double>(write_ops) / static_cast<double>(total_ops);

    /* If there is no load in the system, nothing else to do. */
    if (total_ops == 0) {
      return MakeError(EAGAIN);
    }

    /* Start with equal default weights for all valid servers. */
    ServerWeights weights{0.0};
    ResetWeights(&weights, &stats);
    bool is_stable = false;

    double diff = 1.0;
    for (int i = 0; i < kMaxIterations; i++) {
      ServerSignals signals{0};
      ServerID best_server_id = kInvalidServerID;
      auto best_server_signal = std::numeric_limits<uint64_t>::max();
      uint64_t sum_signal = 0;

      std::ranges::for_each(stats, [&](const auto &srv) {
        const auto server_id = srv.server_id;

        const auto residual_load = GetResidualLoad(srv, op);
        const auto new_load = total_ops * weights.at(server_id);
        const auto load = new_load + residual_load;

        const auto &model = models_.at(server_id);
        const auto sig = model.GetLatency(load, op, srv.mode, write_ratio);

        if (sig < best_server_signal) {
          best_server_signal = sig;
          best_server_id = server_id;
        }

        signals.at(server_id) = sig;
        sum_signal += sig;
      });

      /* Compute the average signal to compare with. */
      const auto d_sum_signal = static_cast<double>(sum_signal);
      const auto d_num_servers = static_cast<double>(num_servers);
      const double mean_signal = d_sum_signal / d_num_servers;

      /* Compute how much better it is from the mean. */
      const auto d_diff_signal =
          std::abs(static_cast<double>(best_server_signal) - mean_signal);
      diff = d_diff_signal / mean_signal;
      is_stable = diff <= bound;

      /* Stop when a stable state has been reached. */
      if (is_stable) {
        break;
      }

      /* Set the weights for future. */
      const auto cur_best_weight = weights.at(best_server_id);
      const auto new_best_weight = cur_best_weight + kBias;
      const auto cur_rem_weight = 1.0 - cur_best_weight;
      if (cur_rem_weight == 0) {
        return MakeError(EAGAIN);
      }

      /* Determine the scale factor for all non-best server weights. */
      const auto new_rem_weight = 1.0 - new_best_weight;
      const auto scale_factor = new_rem_weight / cur_rem_weight;
      if (scale_factor == 0) {
        return MakeError(EAGAIN);
      }

      /* Update all weights. */
      std::ranges::for_each(stats, [&weights, scale_factor, best_server_id,
                                    new_best_weight](const auto &srv) {
        const auto server_id = srv.server_id;
        auto weight = weights.at(server_id);
        if (server_id == best_server_id) {
          weight = std::min(new_best_weight, kMaxWeight);
        } else {
          weight = std::max(scale_factor * weight, kMinWeight);
        }
        weights.at(server_id) = weight;
      });
    }

    if (!is_stable) {
      return MakeError(EAGAIN);
    }

    return weights;
  }

  Status<ServerWeights> ComputeWeightsFromPeak(const ServerStatsList &stats,
                                               OpType /*op*/,
                                               const SystemLoad load) {
    const size_t num_servers = stats.size();

    /* Nothing to stabilize with less than 2 servers. */
    if (num_servers < 2) {
      return MakeError(EAGAIN);
    }

    const auto [read_ops, write_ops] = load;
    const auto total_ops = read_ops + write_ops;
    const double write_ratio =
        static_cast<double>(write_ops) / static_cast<double>(total_ops);

    /* If there is no load in the system, nothing else to do. */
    if (total_ops == 0) {
      return MakeError(EAGAIN);
    }

    /* Start with equal default weights for all valid servers. */
    ServerWeights weights{0.0};
    ResetWeights(&weights, &stats);

    std::vector<uint64_t> loads;
    uint64_t total_iops_capacity = 0;
    std::ranges::for_each(stats, [&](const auto &srv) {
      const auto server_id = srv.server_id;
      const auto &model = models_.at(server_id);
      const auto srv_load = model.GetPeakIOPS(srv.mode, write_ratio);
      loads.emplace_back(srv_load);
      total_iops_capacity += srv_load;
    });
    if (total_iops_capacity == 0) {
      // All servers are saturated per their profiles; fall back to equal weights.
      return weights;
    }
    std::sort(std::begin(loads), std::end(loads));
    const auto median_load = loads.at(static_cast<size_t>(loads.size() / 2));

    double sum_weights = 0.0;
    std::ranges::for_each(stats, [&](const auto &srv) {
      const auto server_id = srv.server_id;
      const auto &model = models_.at(server_id);
      const auto srv_load = model.GetPeakIOPS(srv.mode, write_ratio);
      double weight = static_cast<double>(srv_load) /
                      static_cast<double>(total_iops_capacity);
      if (srv_load >= median_load) {
        weight += kBias;
      } else {
        weight -= kBias;
      }
      weight = std::clamp(weight, kMinWeight, kMaxWeight);
      weights.at(server_id) = weight;
      sum_weights += weight;
    });

    if (sum_weights <= 0.0) {
      ResetWeights(&weights, &stats);
      return weights;
    }
    std::ranges::for_each(stats, [&](const auto &srv) {
      const auto server_id = srv.server_id;
      weights.at(server_id) /= sum_weights;
    });

    return weights;
  }
};

}  // namespace sandook::schedulers::control_plane
