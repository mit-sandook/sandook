#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>
#include <random>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/types.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/log.h"

namespace sandook::controller {

namespace block_allocator {

struct ServerAllocation {
  /* Allocation status of the associated remote server. */
  std::vector<bool> allocation_map;
  std::atomic<ServerBlockAddr> next_allocation{0};
};

/* Each index corresponds to a ServerID. */
using ServerAllocations = std::array<ServerAllocation, kNumMaxServers>;

}  // namespace block_allocator

class BlockAllocator {
 public:
  BlockAllocator() = default;

  Status<void> AddServer(const ServerID server_id, uint64_t nsectors) {
    assert(server_id < kNumMaxServers && server_id > kInvalidServerID);

    srv_allocs_.at(server_id).allocation_map.resize(nsectors);
    srv_allocs_.at(server_id).next_allocation = 0;

    return {};
  }

  Status<std::vector<ServerBlockInfo>> AllocateBlocks(ServerID server_id,
                                                      int n) {
    assert(server_id < kNumMaxServers && server_id > kInvalidServerID);

    auto &server = srv_allocs_.at(server_id);
    std::vector<ServerBlockInfo> allocs(n);
    const auto start_idx = server.next_allocation.fetch_add(n);

    for (int i = 0; i < n; i++) {
      const auto idx = start_idx + i;
      allocs.at(i) = {.server_id = server_id, .block_addr = idx};
    }

    return allocs;
  }

 private:
  block_allocator::ServerAllocations srv_allocs_;
};

}  // namespace sandook::controller
