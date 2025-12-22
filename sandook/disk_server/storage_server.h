#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/bindings/thread.h"
#include "sandook/disk_server/disk_monitor.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

class StorageServer {
 public:
  virtual ~StorageServer();

  /* No copying. */
  StorageServer(const StorageServer &) = delete;
  StorageServer &operator=(const StorageServer &) = delete;

  /* No moving. */
  StorageServer(StorageServer &&) noexcept;
  StorageServer &operator=(StorageServer &&) noexcept;

  void HandleSignal(int sig) { mon_.HandleSignal(sig); }

  void Ready();

  [[nodiscard]] static Status<size_t> GetMsgResponseSize(
      const StorageOpMsg *msg);

  [[nodiscard]] virtual Status<int> HandleStorageOp(
      const StorageOpMsg *msg, std::span<const std::byte> req_payload,
      std::span<std::byte> resp_payload) = 0;

  [[nodiscard]] virtual Status<void> HandleDiscardBlocks(
      const std::vector<ServerBlockAddr> &blocks) {
    return MakeError(ENOTSUP);
  }

  void HandleRejection(OpType op) {
    switch (op) {
      case OpType::kRead:
        mon_.ReadRejected();
        break;

      case OpType::kWrite:
        mon_.WriteRejected();
        break;

      default:
        break;
    }
  }

  [[nodiscard]] bool IsCongested() const { return mon_.IsCongested(); }

  [[nodiscard]] bool IsAllowingWrites() const {
    return mon_.IsAllowingWrites();
  }

 protected:
  StorageServer(RPCClient *ctrl, uint64_t num_sectors, const std::string &name);

  uint64_t hook_read_started() { return mon_.ReadStarted(); }
  void hook_read_completed(uint64_t start_time, bool success) {
    mon_.ReadCompleted(start_time, success);
  }

  uint64_t hook_write_started() { return mon_.WriteStarted(); }
  void hook_write_completed(uint64_t start_time, bool success) {
    mon_.WriteCompleted(start_time, success);
  }

 private:
  /* Connection to the controller. */
  RPCClient *ctrl_;

  /* Thread to periodically push server stats to the controller. */
  rt::Thread th_ctrl_stats_;

  /* Name of the server (specified at launch-time). */
  std::string name_;

  /* Server ID assigned by the controller upon registration. */
  ServerID server_id_{0};

  /* Indicate if background threads need to stop. */
  bool stop_{false};

  /* Agent for monitoring performance statistics of this disk. */
  DiskMonitor mon_;

  void ControllerStatsUpdater();

  [[nodiscard]] Status<void> HandleUpdateServerStatsReply(
      std::span<const std::byte> payload);

  [[nodiscard]] Status<void> HandleRegisterServerReply(
      std::span<const std::byte> payload);
};

}  // namespace sandook
