#pragma once

#include <cstddef>
#include <random>
#include <span>

#include "sandook/base/error.h"
#include "sandook/base/msg.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

struct alignas(kCacheLineSizeBytes) Traces {
  std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> t;
  uint64_t idx{0};
};

class SPDKServer : public StorageServer {
 public:
  explicit SPDKServer(RPCClient *ctrl);
  ~SPDKServer() override = default;

  /* No copying. */
  SPDKServer(const SPDKServer &) = delete;
  SPDKServer &operator=(const SPDKServer &) = delete;

  /* No moving. */
  SPDKServer(SPDKServer &&) noexcept;
  SPDKServer &operator=(SPDKServer &&) noexcept;

  [[nodiscard]] Status<int> HandleStorageOp(
      const StorageOpMsg *msg, std::span<const std::byte> req_payload,
      std::span<std::byte> resp_payload) override;

  [[nodiscard]] Status<void> HandleDiscardBlocks(
      const std::vector<ServerBlockAddr> &blocks) override;

 private:
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<uint64_t> block_dist;
};

}  // namespace sandook
