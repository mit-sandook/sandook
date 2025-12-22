#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <random>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/hash.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_write_scheduler.h"
#include "sandook/scheduler/data_plane/common.h"

namespace sandook::schedulers::data_plane {

class HashWrite : public BaseWriteScheduler {
 public:
  HashWrite() : rand_gen_(rand_dev_()) {}
  ~HashWrite() override = default;

  /* No copying. */
  HashWrite(const HashWrite &) = delete;
  HashWrite &operator=(const HashWrite &) = delete;

  /* No moving. */
  HashWrite(HashWrite &&other) = delete;
  HashWrite &operator=(HashWrite &&other) = delete;

  Status<ServerReplicaList> SelectWriteReplicas(const ServerWeights &weights,
                                                VolumeID vol_id,
                                                const IODesc *iod) override {
    assert(iod != nullptr);

    const auto servers = GetValidServers(weights);
    assert(!servers.empty());

    ServerReplicaList replicas{};
    if (servers.size() == kNumReplicas) {
      /* Just copy all servers as the replicas. */
      std::copy(servers.begin(), servers.end(), replicas.begin());
    } else {
      ServerSet choices(servers.cbegin(), servers.cend());
      const auto n_options =
          std::min(static_cast<int>(choices.size()), kNumReplicas);
      for (int i = 0; i < n_options; i++) {
        const auto h = Hash(vol_id, iod->start_sector);
        const auto idx = static_cast<int64_t>(h % choices.size());
        const auto choice = std::next(choices.begin(), idx);
        replicas.at(i) = *choice;
        choices.erase(choice);
      }
      if (n_options < kNumReplicas) {
        /* If not all kNumReplicas were found, use a default option to fill in
         * the remaining ones.
         */
        const auto def = replicas.at(0);
        std::ranges::fill(replicas.begin() + n_options, replicas.end(), def);
      }
    }

    return replicas;
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
};

}  // namespace sandook::schedulers::data_plane
