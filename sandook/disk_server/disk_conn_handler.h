#pragma once

#include <cstddef>
#include <span>

#include "sandook/base/error.h"
#include "sandook/base/msg.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

class DiskConnHandler : public RPCHandler {
 public:
  explicit DiskConnHandler(StorageServer *server) : server_(server) {}
  ~DiskConnHandler() override = default;

  /* No copying. */
  DiskConnHandler(const DiskConnHandler &) = delete;
  DiskConnHandler &operator=(const DiskConnHandler &) = delete;

  /* No moving. */
  DiskConnHandler(DiskConnHandler &&) noexcept;
  DiskConnHandler &operator=(DiskConnHandler &&) noexcept;

  RPCReturnBuffer HandleMsg(std::span<const std::byte> payload) override;

 private:
  StorageServer *server_;

  [[nodiscard]] Status<RPCReturnBuffer> HandleStorageOp(
      const MsgHeader *header, std::span<const std::byte> payload) const;

  [[nodiscard]] Status<RPCReturnBuffer> HandleDiscardBlocks(
      const MsgHeader *header, std::span<const std::byte> payload) const;

  Status<RPCReturnBuffer> RejectStorageOp(StorageOpMsg *msg,
                                          StorageOpReplyCode code) const;
};

}  // namespace sandook
