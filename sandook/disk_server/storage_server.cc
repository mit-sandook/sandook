#include "sandook/disk_server/storage_server.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/base/time.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/timer.h"
#include "sandook/config/config.h"
#include "sandook/rpc/rpc.h"
#include "sandook/utils/calibrated_time.h"

namespace sandook {

StorageServer::StorageServer(RPCClient *ctrl, uint64_t num_sectors,
                             const std::string &name)
    : ctrl_(ctrl), name_(name) {
  const auto *const ip = Config::kStorageServerIP.c_str();
  const auto port = Config::kStorageServerPort;

  const auto delta_us = utils::CalibrateTimeWithController(ctrl);
  if (!delta_us) {
    throw std::runtime_error("Cannot calibrate time with the controller");
  }
  utils::SetControllerTimeCalibration(*delta_us);

  auto req = CreateRegisterServerMsg(ip, port, name, num_sectors);
  const auto req_size = GetMsgSize(req.get());
  auto reg_resp = ctrl_->Call(writable_span(req.get(), req_size));

  auto ret = HandleRegisterServerReply(reg_resp.get_buf()).transform([&]() {
    th_ctrl_stats_ = [this] { ControllerStatsUpdater(); };
  });
  if (!ret) {
    throw std::runtime_error("Registration failed");
  }

  LOG(INFO) << "DiskServerName = " << name;
}

StorageServer::~StorageServer() {
  stop_ = true;
  th_ctrl_stats_.Join();
}

void StorageServer::ControllerStatsUpdater() {
  const Duration update_interval(kDiskServerStatsUpdateIntervalUs);

  while (!stop_) {
    auto stats_msg =
        CreateUpdateServerStatsMsg(server_id_, mon_.UpdateAndGetServerStats());

    const auto stats_payload_size = GetMsgSize(stats_msg.get());
    auto stats_resp =
        ctrl_->Call(writable_span(stats_msg.get(), stats_payload_size));
    if (!stats_resp) {
      LOG(ERR) << "Failed to update stats to the controller";
    }

    const auto stats_ret = HandleUpdateServerStatsReply(stats_resp.get_buf());
    if (!stats_ret) {
      LOG(ERR) << "Failed to update stats to the controller";
    }

    auto cmt_msg = CreateCommitServerModeMsg(server_id_, mon_.GetMode());
    const auto cmt_payload_size = GetMsgSize(cmt_msg.get());
    /* No reply is expected. */
    auto _ = ctrl_->Call(writable_span(cmt_msg.get(), cmt_payload_size));

    rt::Sleep(update_interval);
  }
}

Status<void> StorageServer::HandleUpdateServerStatsReply(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(UpdateServerStatsReplyMsg)) {
    return MakeError(EINVAL);
  }
  const auto *msg = reinterpret_cast<const UpdateServerStatsReplyMsg *>(
      const_cast<std::byte *>(
          payload.last(sizeof(UpdateServerStatsReplyMsg)).data()));

  mon_.SetModeAndWeights(msg->mode, msg->read_weight, msg->write_weight);

  return {};
}

[[nodiscard]] Status<void> StorageServer::HandleRegisterServerReply(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(RegisterServerReplyMsg)) {
    return MakeError(EINVAL);
  }
  const auto *msg =
      reinterpret_cast<const RegisterServerReplyMsg *>(const_cast<std::byte *>(
          payload.last(sizeof(RegisterServerReplyMsg)).data()));

  server_id_ = msg->server_id;
  mon_.SetServerID(server_id_);
  mon_.SetServerName(name_);
  mon_.SetIsRejectionsEnabled(msg->is_rejections_enabled);

  LOG(INFO) << "DiskServerID = " << server_id_;
  LOG(INFO) << "DiskServerRejectionsEnabled = " << msg->is_rejections_enabled;

  return {};
}

[[nodiscard]] Status<size_t> StorageServer::GetMsgResponseSize(
    const StorageOpMsg *msg) {
  const IODesc *iod = &msg->iod;
  const OpType op = IODesc::get_op(iod);

  if (op == OpType::kRead) {
    return iod->num_sectors << kSectorShift;
  }
  return 0;
}

}  // namespace sandook
