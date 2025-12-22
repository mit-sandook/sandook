#pragma once

#include <cstddef>
#include <span>

#include "sandook/base/error.h"
#include "sandook/base/msg.h"
#include "sandook/controller/controller_agent.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

class ControllerConnHandler : public RPCHandler {
 public:
  explicit ControllerConnHandler(ControllerAgent *ctrl) : ctrl_(ctrl) {}
  ~ControllerConnHandler() override = default;

  /* No copying. */
  ControllerConnHandler(const ControllerConnHandler &) = delete;
  ControllerConnHandler &operator=(const ControllerConnHandler &) = delete;

  /* No moving. */
  ControllerConnHandler(ControllerConnHandler &&) noexcept;
  ControllerConnHandler &operator=(ControllerConnHandler &&) noexcept;

  RPCReturnBuffer HandleMsg(std::span<const std::byte> payload) override;

 private:
  ControllerAgent *ctrl_;

  [[nodiscard]] Status<RPCReturnBuffer> HandleAllocateBlocks(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleRegisterServer(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleRegisterVolume(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleUpdateServerStats(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleCommitServerMode(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleServerReady(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] Status<RPCReturnBuffer> HandleGetServerStats(
      const MsgHeader *header, std::span<const std::byte> payload);

  [[nodiscard]] static Status<RPCReturnBuffer> HandleGetControllerTime(
      const MsgHeader *header, std::span<const std::byte> payload);
};

}  // namespace sandook
