#include "sandook/disk_server/disk_conn_handler.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/base/payload.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

RPCReturnBuffer DiskConnHandler::HandleMsg(std::span<const std::byte> payload) {
  const auto* header = reinterpret_cast<const MsgHeader*>(payload.data());
  auto msg = payload.subspan(sizeof(MsgHeader));
  switch (header->type) {
    case MsgType::kStorageOp:
      // TODO(girfan): Return error code like EIO etc. to handle at client.
      return HandleStorageOp(header, msg).value_or(RPCReturnBuffer{});

    case MsgType::kDiscardBlocks:
      return HandleDiscardBlocks(header, msg).value_or(RPCReturnBuffer{});

    default:
      LOG(ERR) << "Unexpected msg type: " << header->type;
      break;
  }

  return {};
}

Status<RPCReturnBuffer> DiskConnHandler::HandleDiscardBlocks(
    const MsgHeader* header, std::span<const std::byte> payload) const {
  assert(payload.size() >= sizeof(StorageOpMsg));

  /* Extract the message. */
  auto* msg = reinterpret_cast<DiscardBlocksMsg*>(
      const_cast<std::byte*>(payload.first(header->len).data()));

  auto* it_start = msg->blocks.begin();
  auto* it_end = msg->blocks.begin() + msg->num_blocks;
  const auto ret = server_->HandleDiscardBlocks({it_start, it_end});
  if (!ret) {
    LOG(ERR) << "Cannot discard " << msg->num_blocks << " blocks";
  }

  return {};
}

Status<RPCReturnBuffer> DiskConnHandler::HandleStorageOp(
    const MsgHeader* header, std::span<const std::byte> payload) const {
  assert(payload.size() >= sizeof(StorageOpMsg));

  /* Extract the message. */
  auto* msg = reinterpret_cast<StorageOpMsg*>(
      const_cast<std::byte*>(payload.first(header->len).data()));
  auto op = IODesc::get_op(&msg->iod);

  /* Early rejection/congestion checks.
   * The only exception is if the request has an affinity for this server in
   * which case this check is bypassed.
   */
  if (msg->affinity == kInvalidServerID) {
    /* Operating in read mode; prevent mixing writes. */
    if (op == OpType::kWrite && !server_->IsAllowingWrites()) {
      return RejectStorageOp(msg, StorageOpReplyCode::kRejectModeMismatch);
    }
  }

  /* Copy the message payload into an aligned memory region. */
  const Payload req_payload(kDeviceAlignment, header->payload_size);
  std::memcpy(req_payload.data(), payload.last(header->payload_size).data(),
              header->payload_size);

  /* Evaluate the reply payload size and allocate the reply payload. */
  const auto reply_payload_size =
      sandook::StorageServer::GetMsgResponseSize(msg);
  const Payload reply_payload(kDeviceAlignment, *reply_payload_size);

  /* Handle the request and fill the reply payload (if applicable). */
  const auto ret =
      server_->HandleStorageOp(msg, req_payload.view(), reply_payload.view());
  if (!ret) {
    return MakeError(ret);
  }

  auto reply_status = StorageOpReplyCode::kSuccess;
  if (server_->IsCongested()) {
    reply_status = StorageOpReplyCode::kSuccessCongested;
  }

  /* Create the reply message and copy the payload into it. */
  auto reply = CreateStorageOpReplyMsg(msg->iod, msg->req_id,
                                       *reply_payload_size, *ret, reply_status);
  std::memcpy(reply.get() + sizeof(MsgHeader) + sizeof(StorageOpReplyMsg),
              reply_payload.data(), *reply_payload_size);

  const auto reply_size =
      sizeof(MsgHeader) + sizeof(StorageOpReplyMsg) + *reply_payload_size;
  return {RPCReturnBuffer{writable_span(reply.get(), reply_size),
                          [b = std::move(reply)]() mutable {}}};
}

Status<RPCReturnBuffer> DiskConnHandler::RejectStorageOp(
    StorageOpMsg* msg, StorageOpReplyCode code) const {
  auto op = IODesc::get_op(&msg->iod);

  server_->HandleRejection(op);

  static const auto reply_payload_size = 0;
  static const auto ret = 0;
  auto reply = CreateStorageOpReplyMsg(msg->iod, msg->req_id,
                                       reply_payload_size, ret, code);
  const auto reply_size = sizeof(MsgHeader) + sizeof(StorageOpReplyMsg);
  return {RPCReturnBuffer{writable_span(reply.get(), reply_size),
                          [b = std::move(reply)]() mutable {}}};
}

}  // namespace sandook
