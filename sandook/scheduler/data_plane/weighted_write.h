#pragma once

#include <algorithm>
#include <cassert>
#include <iterator>
#include <random>
#include <ranges>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_write_scheduler.h"
#include "sandook/scheduler/data_plane/common.h"

namespace sandook::schedulers::data_plane {

class WeightedWrite : public BaseWriteScheduler {
 public:
  WeightedWrite() : rand_gen_(rand_dev_()) {}
  ~WeightedWrite() override = default;

  /* No copying. */
  WeightedWrite(const WeightedWrite &) = delete;
  WeightedWrite &operator=(const WeightedWrite &) = delete;

  /* No moving. */
  WeightedWrite(WeightedWrite &&other) = delete;
  WeightedWrite &operator=(WeightedWrite &&other) = delete;

  Status<ServerReplicaList> SelectWriteReplicas(
      const ServerWeights &weights, [[maybe_unused]] VolumeID vol_id,
      [[maybe_unused]] const IODesc *iod) override {
    assert(iod != nullptr);

    const auto servers = GetValidServers(weights);
    assert(!servers.empty());

    const auto n_servers = static_cast<int>(servers.size());
    ServerReplicaList replicas{};
    if (n_servers == kNumReplicas) {
      /* Just copy all servers as the replicas. */
      std::copy(servers.begin(), servers.end(), replicas.begin());
    } else if (n_servers < kNumReplicas) {
      /* First shuffle them all. */
      const auto n_samples = std::min(n_servers, kNumReplicas);
      std::ranges::sample(servers, replicas.begin(), n_samples, rand_gen_);
      /* Use a default option to fill in the remaining ones. */
      const auto def = replicas.at(0);
      std::ranges::fill(replicas.begin() + n_samples, replicas.end(), def);
    } else {
      /* Weighted selection. */
      int i = 0;
      ServerSet unique_servers(servers.begin(), servers.end());
      while (i < kNumReplicas) {
        auto t_weights = std::views::all(unique_servers) |
                         std::views::transform([&](const auto &choice) {
                           assert(choice != kInvalidServerID);
                           return weights.at(choice);
                         });

        std::discrete_distribution<> dist(t_weights.cbegin(), t_weights.cend());
        const auto idx = dist(rand_gen_);

        auto it = unique_servers.begin();
        std::advance(it, idx);
        replicas.at(i++) = *it;
        unique_servers.erase(it);
      }
    }

    return replicas;
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
};

}  // namespace sandook::schedulers::data_plane
