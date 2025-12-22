#pragma once

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <iterator>
#include <random>
#include <vector>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_read_scheduler.h"
#include "sandook/scheduler/data_plane/common.h"

namespace sandook::schedulers::data_plane {

class RandomRead : public BaseReadScheduler {
 public:
  RandomRead() : rand_gen_(rand_dev_()) {}
  ~RandomRead() override = default;

  /* No copying. */
  RandomRead(const RandomRead &) = delete;
  RandomRead &operator=(const RandomRead &) = delete;

  /* No moving. */
  RandomRead(RandomRead &&other) = delete;
  RandomRead &operator=(RandomRead &&other) = delete;

  Status<ServerID> SelectReadServer(
      const ServerWeights &weights, const ServerSet *subset,
      [[maybe_unused]] VolumeID vol_id,
      [[maybe_unused]] const IODesc *iod) override {
    assert(subset != nullptr);

    const auto servers = GetValidServers(weights);
    assert(!servers.empty());

    std::vector<ServerID> choices;
    std::ranges::set_intersection(servers, *subset,
                                  std::back_inserter(choices));

    if (choices.empty()) {
      return MakeError(ENOENT);
    }

    const auto n_choices = static_cast<int>(choices.size());
    std::uniform_int_distribution<> dist(0, n_choices - 1);
    const auto idx = dist(rand_gen_);
    const auto chosen = choices.at(idx);

    return chosen;
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
};

}  // namespace sandook::schedulers::data_plane
