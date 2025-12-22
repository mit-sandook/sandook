#pragma once

#include <algorithm>
#include <cassert>
#include <random>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_write_scheduler.h"
#include "sandook/scheduler/data_plane/common.h"

namespace sandook::schedulers::data_plane {

class RandomWrite : public BaseWriteScheduler {
 public:
  RandomWrite() : rand_gen_(rand_dev_()) {}
  ~RandomWrite() override = default;

  /* No copying. */
  RandomWrite(const RandomWrite &) = delete;
  RandomWrite &operator=(const RandomWrite &) = delete;

  /* No moving. */
  RandomWrite(RandomWrite &&other) = delete;
  RandomWrite &operator=(RandomWrite &&other) = delete;

  Status<ServerReplicaList> SelectWriteReplicas(
      const ServerWeights &weights, [[maybe_unused]] VolumeID vol_id,
      [[maybe_unused]] const IODesc *iod) override {
    const auto servers = GetValidServers(weights);
    assert(!servers.empty());

    ServerReplicaList replicas{};
    if (servers.size() == kNumReplicas) {
      /* Just copy all servers as the replicas. */
      std::copy(servers.begin(), servers.end(), replicas.begin());
    } else {
      /* First shuffle them all. */
      const auto n_servers = static_cast<int>(servers.size());
      const auto n_samples = std::min(n_servers, kNumReplicas);
      std::ranges::sample(servers, replicas.begin(), n_samples, rand_gen_);
      if (n_samples < kNumReplicas) {
        /* If not all kNumReplicas were found, use a default option to fill in
         * the remaining ones.
         */
        const auto def = replicas.at(0);
        std::ranges::fill(replicas.begin() + n_samples, replicas.end(), def);
      }
    }

    return replicas;
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
};

}  // namespace sandook::schedulers::data_plane
