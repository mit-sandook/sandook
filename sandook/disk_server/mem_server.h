#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sandook/base/error.h"
#include "sandook/base/msg.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

class MemServer : public StorageServer {
 public:
  explicit MemServer(RPCClient *ctrl);
  ~MemServer() override = default;

  /* No copying. */
  MemServer(const MemServer &) = delete;
  MemServer &operator=(const MemServer &) = delete;

  /* No moving. */
  MemServer(MemServer &&) noexcept;
  MemServer &operator=(MemServer &&) noexcept;

  [[nodiscard]] Status<int> HandleStorageOp(
      const StorageOpMsg *msg, std::span<const std::byte> req_payload,
      std::span<std::byte> resp_payload) override;

 private:
  std::vector<std::byte> buf_;

  Status<void> HandleReadOp(std::span<std::byte> dst, uint64_t start_lba);
  Status<void> HandleWriteOp(std::span<const std::byte> src,
                             uint64_t start_lba);
};

}  // namespace sandook
