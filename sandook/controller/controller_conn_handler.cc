#include "sandook/controller/controller_conn_handler.h"

#include <cerrno>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

#include "base/compiler.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/msg.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/bindings/log.h"
#include "sandook/config/config.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

RPCReturnBuffer ControllerConnHandler::HandleMsg(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(MsgHeader)) {
    LOG(ERR) << "Payload too small: " << payload.size();
    return {};
  }
  const auto* header = reinterpret_cast<const MsgHeader*>(payload.data());
  const auto msg = payload.subspan(sizeof(MsgHeader));

  switch (header->type) {
    case MsgType::kAllocateBlocks:
      return HandleAllocateBlocks(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kUpdateServerStats:
      return HandleUpdateServerStats(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kGetServerStats:
      return HandleGetServerStats(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kCommitServerMode:
      return HandleCommitServerMode(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kGetControllerTime:
      return HandleGetControllerTime(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kRegisterServer:
      return HandleRegisterServer(header, msg).value_or(RPCReturnBuffer{});
    case MsgType::kRegisterVolume:
      return HandleRegisterVolume(header, msg).value_or(RPCReturnBuffer{});
    default:
      LOG(ERR) << "Unexpected msg type: " << header->type;
      return {};
  }
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleAllocateBlocks(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(AllocateBlocksMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<AllocateBlocksMsg*>(
      const_cast<std::byte*>(payload.data()));

  const auto server_blks = ctrl_->AllocateBlocks(msg->server_id);
  auto reply = CreateAllocateBlocksReplyMsg(*server_blks);

  const auto response_size = sizeof(MsgHeader) + sizeof(AllocateBlocksReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleRegisterServer(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(RegisterServerMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<RegisterServerMsg*>(
      const_cast<std::byte*>(payload.data()));

  std::string name = msg->name;
  name.erase(
      std::remove_if(name.begin(), name.end(),
                     [](const auto& c) -> bool { return !std::isalnum(c); }),
      name.end());
  name.erase(std::remove(name.begin(), name.end(), ' '), name.end());

  auto id = ctrl_->RegisterServer(static_cast<const char*>(msg->ip), msg->port,
                                  name.c_str(), msg->nsectors);
  if (!id) {
    return MakeError(id);
  }

  auto reply =
      CreateRegisterServerReplyMsg(id.value(), Config::kDiskServerRejections);
  auto response_size = sizeof(MsgHeader) + sizeof(RegisterServerReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleRegisterVolume(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(RegisterVolumeMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<RegisterVolumeMsg*>(
      const_cast<std::byte*>(payload.data()));
  auto id = ctrl_->RegisterVolume(static_cast<const char*>(msg->ip), msg->port,
                                  msg->nsectors);
  if (!id) {
    return MakeError(id);
  }

  auto reply =
      CreateRegisterVolumeReplyMsg(id.value(), Config::kDataPlaneSchedulerType);
  auto* reply_msg = reinterpret_cast<RegisterVolumeReplyMsg*>(
      reply.get() + sizeof(MsgHeader));
  for (const auto& [id, server] : ctrl_->get_servers()) {
    AddServer(reply_msg, server.info());
  }

  const auto response_size = sizeof(MsgHeader) + sizeof(RegisterVolumeReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleUpdateServerStats(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(UpdateServerStatsMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<UpdateServerStatsMsg*>(
      const_cast<std::byte*>(payload.data()));
  const auto ret = ctrl_->UpdateServerStats(msg->server_id, msg->stats);
  if (!ret) {
    return MakeError(ret);
  }

  auto stats = ctrl_->GetDataPlaneServerStats(msg->server_id);
  if (!stats) {
    throw std::runtime_error("Cannot get data plane stats for server");
  }
  const auto [mode, c_state, read_weight, write_weight] = *stats;
  auto reply = CreateUpdateServerStatsReplyMsg(msg->server_id, mode, c_state,
                                               read_weight, write_weight);
  auto response_size = sizeof(MsgHeader) + sizeof(UpdateServerStatsReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleCommitServerMode(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(CommitServerModeMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<CommitServerModeMsg*>(
      const_cast<std::byte*>(payload.data()));

  const auto cmt = ctrl_->CommitServerMode(msg->server_id, msg->mode);
  if (!cmt) {
    LOG(ERR) << "Cannot commit server mode";
    return MakeError(cmt);
  }

  return {};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleGetServerStats(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(GetServerStatsMsg)) {
    return MakeError(EINVAL);
  }

  auto* msg = reinterpret_cast<GetServerStatsMsg*>(
      const_cast<std::byte*>(payload.data()));

  auto reply = CreateGetServerStatsReplyMsg(msg->vol_id);
  const auto response_size = GetMsgSize(reply.get());
  auto* reply_msg = reinterpret_cast<GetServerStatsReplyMsg*>(
      reply.get() + sizeof(MsgHeader));

  const auto server_stats = ctrl_->GetServerStats();
  if (!server_stats) {
    LOG(ERR) << "Cannot get server stats";
    return MakeError(server_stats);
  }
  for (const auto& stats : *server_stats) {
    AddServerStats(reply_msg, stats);
  }

  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> ControllerConnHandler::HandleGetControllerTime(
    [[maybe_unused]] const MsgHeader* header,
    std::span<const std::byte> payload) {
  if (payload.size() != sizeof(GetControllerTimeMsg)) {
    return MakeError(EINVAL);
  }

  barrier();
  const auto microtime = MicroTime();
  barrier();

  auto reply = CreateGetControllerTimeReplyMsg(microtime);

  const auto response_size =
      sizeof(MsgHeader) + sizeof(GetControllerTimeReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), response_size),
                          [b = std::move(reply)]() mutable {}}};
}

}  // namespace sandook
