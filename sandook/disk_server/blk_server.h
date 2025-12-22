#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "sandook/base/error.h"
#include "sandook/base/msg.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

class BlkServer : public StorageServer {
 public:
  BlkServer(RPCClient *ctrl, const std::string &dev);
  ~BlkServer() override;

  /* No copying. */
  BlkServer(const BlkServer &) = delete;
  BlkServer &operator=(const BlkServer &) = delete;

  /* No moving. */
  BlkServer(BlkServer &&) noexcept;
  BlkServer &operator=(BlkServer &&) noexcept;

  [[nodiscard]] Status<int> HandleStorageOp(
      const StorageOpMsg *msg, std::span<const std::byte> req_payload,
      std::span<std::byte> resp_payload) override;

 private:
  int fd_{};

  static uint64_t GetNumSectors(const std::string &dev);

  [[nodiscard]] Status<void> Seek(uint64_t offset) const;
  [[nodiscard]] Status<int> HandleRead(uint64_t offset, unsigned len,
                                       std::span<std::byte> resp_payload) const;
  [[nodiscard]] Status<int> HandleWrite(
      uint64_t offset, unsigned len,
      std::span<const std::byte> req_payload) const;
  [[nodiscard]] Status<void> HandleFlush() const;
  [[nodiscard]] Status<void> HandleDiscard(uint64_t offset, unsigned len,
                                           int mode) const;
};

}  // namespace sandook
