#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <tuple>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/types.h"

namespace sandook {

constexpr auto kDefaultServerWeight = 1.0;

enum ServerMode { kMix = 0, kRead = 1, kWrite = 2 };

struct ServerStats {
  ServerID server_id;
  ServerMode mode;
  ServerMode committed_mode;
  double read_mops;
  double write_mops;
  double read_weight;
  double write_weight;
  uint32_t inflight_reads;
  uint32_t inflight_writes;
  uint32_t completed_reads;
  uint32_t pure_reads;
  uint32_t impure_reads;
  uint32_t completed_writes;
  uint32_t rejected_reads;
  uint32_t rejected_writes;
  uint64_t median_read_latency;
  uint64_t median_write_latency;
  uint64_t signal_read_latency;
  uint64_t signal_write_latency;
  bool is_rejecting_requests;
  ServerCongestionState congestion_state;

  friend std::ostream& operator<<(std::ostream& out, const ServerStats& p) {
    out << "server_id: " << p.server_id << '\n'
        << "mode: " << p.mode << '\n'
        << "committed_mode: " << p.committed_mode << '\n'
        << "read_mops: " << p.read_mops << '\n'
        << "write_mops: " << p.write_mops << '\n'
        << "read_weight: " << p.read_weight << '\n'
        << "write_weight: " << p.write_weight << '\n'
        << "inflight_reads: " << p.inflight_reads << '\n'
        << "inflight_writes: " << p.inflight_writes << '\n'
        << "completed_reads: " << p.completed_reads << '\n'
        << "pure_reads: " << p.pure_reads << '\n'
        << "impure_reads: " << p.impure_reads << '\n'
        << "completed_writes: " << p.completed_writes << '\n'
        << "rejected_reads: " << p.rejected_reads << '\n'
        << "rejected_writes: " << p.rejected_writes << '\n'
        << "median_read_latency: " << p.median_read_latency << '\n'
        << "median_write_latency: " << p.median_write_latency << '\n'
        << "signal_read_latency: " << p.signal_read_latency << '\n'
        << "signal_write_latency: " << p.signal_write_latency << '\n'
        << "is_rejecting_requests: " << p.is_rejecting_requests << '\n'
        << "congestion_state: " << p.congestion_state << '\n';
    return out;
  }
};

using ServerWeight = double;
using ServerSignal = uint64_t;

constexpr static auto kInvalidServerWeight =
    std::numeric_limits<ServerWeight>::min();

using ServerModes = std::array<ServerMode, kNumMaxServers>;
using ServerWeights = std::array<ServerWeight, kNumMaxServers>;
using ServerSignals = std::array<ServerSignal, kNumMaxServers>;

/* Order: mode, congestion_state, read weight, write weight. */
using DataPlaneServerStats =
    std::tuple<ServerMode, ServerCongestionState, ServerWeight, ServerWeight>;

using ServerStatsList = std::vector<ServerStats>;

inline void InitServerWeights(ServerWeights& weights) {
  for (auto& weight : weights) {
    weight = kInvalidServerWeight;
  }
}

}  // namespace sandook
