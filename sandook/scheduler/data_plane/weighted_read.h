#pragma once

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <iterator>
#include <random>
#include <ranges>
#include <vector>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/scheduler/data_plane/base_read_scheduler.h"
#include "sandook/scheduler/data_plane/common.h"

namespace sandook::schedulers::data_plane {

class WeightedRead : public BaseReadScheduler {
 public:
  WeightedRead() : rand_gen_(rand_dev_()) {}
  ~WeightedRead() override = default;

  /* No copying. */
  WeightedRead(const WeightedRead &) = delete;
  WeightedRead &operator=(const WeightedRead &) = delete;

  /* No moving. */
  WeightedRead(WeightedRead &&other) = delete;
  WeightedRead &operator=(WeightedRead &&other) = delete;

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

    auto t_weights = std::views::all(choices) |
                     std::views::transform([&](const auto &choice) {
                       assert(choice != kInvalidServerID);
                       return weights.at(choice);
                     });

    std::discrete_distribution<> dist(t_weights.cbegin(), t_weights.cend());
    const auto idx = dist(rand_gen_);
    const auto chosen = choices.at(idx);

    return chosen;
  }

 private:
  std::random_device rand_dev_;
  std::mt19937 rand_gen_;
};

}  // namespace sandook::schedulers::data_plane
