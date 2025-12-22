#include "sandook/virtual_disk/virtual_disk_remote.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/rpc/rpc.h"
#include "sandook/scheduler/data_plane/scheduler.h"
#include "sandook/utils/calibrated_time.h"

namespace sandook {

VirtualDiskRemote::~VirtualDiskRemote() {
  stop_updates_ = true;
  stop_gc_ = true;

  LOG(INFO) << "num_read_rejections: " << num_read_rejections_.get_sum();
  LOG(INFO) << "num_read_retries: " << num_read_retries_.get_sum();
  LOG(INFO) << "num_write_rejections: " << num_write_rejections_.get_sum();
  LOG(INFO) << "num_write_retries: " << num_write_retries_.get_sum();
  LOG(INFO) << "num_reads_submitted: " << num_reads_submitted_.get_sum();
  LOG(INFO) << "num_writes_submitted: " << num_writes_submitted_.get_sum();
  LOG(INFO) << "num_gc_blocks: " << num_gc_blocks();
}

Status<int> VirtualDiskRemote::ProcessRequest(IODesc iod) {
  // TODO(girfan): Assign request IDs
  static const uint64_t req_id = 0;
  const OpType op = IODesc::get_op(&iod);

  switch (op) {
    case OpType::kRead: {
      auto ret = ResolveBlock(&iod);
      if (!ret) {
        LOG(WARN) << "Block not resolved: " << iod.start_sector;
        return MakeError(ret);
      }
      return ProcessReadOp(*ret, iod, req_id);
    } break;

    case OpType::kWrite: {
      auto blks = GetBlocks(&iod, true /* set_dirty */);
      if (!blks) {
        LOG(WARN) << "Cannot get blocks to write";
        return MakeError(blks);
      }
      const auto ret = blk_res_.AddMapping(iod.start_sector, *blks);
      if (!ret) {
        LOG(WARN) << "Cannot add virtual to physical block mapping";
        return MakeError(ret);
      }
      return ProcessWriteOp(*blks, iod, req_id);
    } break;

    case OpType::kAllocate: {
      const auto start_sector = iod.start_sector;
      for (uint64_t s = start_sector; s < start_sector + iod.num_sectors; s++) {
        iod.start_sector = s;
        auto blks = GetBlocks(&iod, false /* set_dirty */);
        if (!blks) {
          LOG(WARN) << "Cannot get blocks to allocate";
          return MakeError(blks);
        }
        const auto ret = blk_res_.AddMapping(s, *blks);
        if (!ret) {
          LOG(WARN) << "Cannot add virtual to physical block mapping";
          return MakeError(ret);
        }
      }
      return {};
    } break;

    default:
      LOG(ERR) << "Unknown operation: " << static_cast<int>(op);
      return MakeError(EINVAL);
  }

  std::unreachable();
}

Status<ServerReplicaBlockInfoList> VirtualDiskRemote::ResolveBlock(
    const IODesc *iod) {
  return blk_res_.ResolveBlock(iod->start_sector);
}

std::vector<ServerBlockInfo *> VirtualDiskRemote::AllocateBlocks(
    ServerID server_id) {
  auto msg = CreateAllocateBlocksMsg(vol_id_, server_id);
  const auto payload_size = GetMsgSize(msg.get());
  auto resp = ctrl_->Call(writable_span(msg.get(), payload_size));
  const auto ret = HandleAllocateBlocksReply(resp.get_buf());
  if (!ret) {
    LOG(ERR) << "Cannot get block allocation from controller";
    return {};
  }

  const auto block_ptrs = std::views::transform(*ret, [&](auto &blk) {
    return new ServerBlockInfo(std::move(blk));  // NOLINT
  });
  return {block_ptrs.cbegin(), block_ptrs.cend()};
}

Status<ServerReplicaBlockInfoList> VirtualDiskRemote::GetBlocks(
    const IODesc *iod, bool set_dirty) {
  if (affinity_ != kInvalidServerID) {
    return GetBlocksWithAffinity(iod, set_dirty);
  }

  const auto servers = sched_->SelectWriteReplicas(vol_id_, iod);
  if (!servers) {
    LOG(ERR) << "Cannot select write replica servers";
    return MakeError(servers);
  }

  ServerReplicaBlockInfoList blks;
  int i = 0;
  for (auto server : *servers) {
    blks.at(i++) = {*(blk_caches_.at(server)->get()), set_dirty};
  }

  return blks;
}

Status<ServerReplicaBlockInfoList> VirtualDiskRemote::GetBlocksWithAffinity(
    const IODesc *iod, bool set_dirty) {
  ServerReplicaBlockInfoList blks;
  for (auto &blk : blks) {
    blk = {*(blk_caches_.at(affinity_)->get()), set_dirty};
  }

  return blks;
}

Status<ServerAllocationBlockInfoList>
VirtualDiskRemote::HandleAllocateBlocksReply(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(AllocateBlocksReplyMsg)) {
    return MakeError(EINVAL);
  }

  const auto *msg = reinterpret_cast<const AllocateBlocksReplyMsg *>(
      payload.data() + sizeof(MsgHeader));

  return msg->server_blks;
}

Status<RPCReturnBuffer> VirtualDiskRemote::ProcessStorageOp(RPCClient *server,
                                                            IODesc iod,
                                                            uint64_t req_id) {
  const OpType op = IODesc::get_op(&iod);

  switch (op) {
    case OpType::kWrite: {
      num_writes_submitted_.inc_local();
      const unsigned payload_len = iod.num_sectors << kSectorShift;
      const char *payload = reinterpret_cast<char *>(iod.addr);
      auto write_msg = CreateStorageOpMsg(iod, req_id, affinity_, payload_len);
      auto *payload_ptr =
          write_msg.get() + sizeof(MsgHeader) + sizeof(StorageOpMsg);
      std::memcpy(payload_ptr, payload, payload_len);
      const auto msg_size = GetMsgSize(write_msg.get());
      return server->Call(writable_span(write_msg.get(), msg_size));
    }

    default: {
      num_reads_submitted_.inc_local();
      auto rd_msg = CreateStorageOpMsg(iod, req_id, affinity_);
      const auto msg_size = GetMsgSize(rd_msg.get());
      return server->Call(writable_span(rd_msg.get(), msg_size));
    }
  }

  std::unreachable();
}

Status<int> VirtualDiskRemote::HandleStorageOpReply(
    std::span<const std::byte> payload, ServerID server_id) {
  if (payload.size() < sizeof(StorageOpReplyMsg)) {
    return MakeError(EINVAL);
  }

  const auto *msg = reinterpret_cast<const StorageOpReplyMsg *>(
      payload.data() + sizeof(MsgHeader));
  const IODesc *iod = &msg->iod;
  const OpType op = IODesc::get_op(iod);

  /* Handle congestion signal. */
  if (msg->code == StorageOpReplyCode::kSuccessCongested) {
    if (server_id != affinity_) {
      sched_->SignalCongested(server_id);
    }
  }

  switch (op) {
    case OpType::kRead: {
      /* Success. */
      if (msg->code == StorageOpReplyCode::kSuccess ||
          msg->code == StorageOpReplyCode::kSuccessCongested) {
        const unsigned len = iod->num_sectors << kSectorShift;
        char *buf = reinterpret_cast<char *>(iod->addr);
        if (len > 0 && buf != nullptr) {
          /* Read the response payload into the associated buffer. */
          const auto msg_offset = sizeof(MsgHeader) + sizeof(StorageOpReplyMsg);
          const auto *payload_ptr = payload.data() + msg_offset;
          std::memcpy(buf, payload_ptr, len);
        }
        return msg->res;
      }

      /* Device busy. */
      if (msg->code == StorageOpReplyCode::kRejectDeviceBusy) {
        DLOG(DEBUG) << "IO rejected (device busy), retrying...";
        if (server_id != affinity_) {
          sched_->SignalCongested(server_id);
        }
        return MakeError(EBUSY);
      }

      /* Failed. */
      if (msg->code == StorageOpReplyCode::kFailure) {
        return MakeError(EINVAL);
      }

      /* Other. */
      LOG(ERR) << "Storage op failed with error code: " << msg->code;
      throw std::runtime_error("Invalid storage reply op code");
    } break;

    case OpType::kWrite: {
      /* Success. */
      if (msg->code == StorageOpReplyCode::kSuccess ||
          msg->code == StorageOpReplyCode::kSuccessCongested) {
        return msg->res;
      }

      /* Device busy. */
      if (msg->code == StorageOpReplyCode::kRejectDeviceBusy) {
        DLOG(DEBUG) << "IO rejected (device busy), retrying...";
        if (server_id != affinity_) {
          sched_->SignalCongested(server_id);
        }
        return MakeError(EBUSY);
      }

      /* Mode mismatch. Device was in read-only mode. */
      if (msg->code == StorageOpReplyCode::kRejectModeMismatch) {
        DLOG(DEBUG) << "IO rejected (mode mismatch), retrying...";
        return MakeError(EROFS);
      }

      /* Failed. */
      if (msg->code == StorageOpReplyCode::kFailure) {
        return MakeError(EINVAL);
      }

      /* Other. */
      LOG(ERR) << "Storage op failed with error code: " << msg->code;
      throw std::runtime_error("Invalid storage reply op code");
    } break;

    default: {
      throw std::runtime_error("Invalid operation");
    }
  }

  std::unreachable();
}

/* Note:
 * This function will never return a failure and keep on retrying by recursively
 * calling ProcessRequest.
 *
 * TODO(girfan): Must put a limit to retries.
 */
Status<int> VirtualDiskRemote::ProcessReadOp(
    const ServerReplicaBlockInfoList servers, IODesc iod, uint64_t req_id) {
  /* Get a set of server IDs from the block info list. */
  const auto server_ids_v = std::views::all(servers) |
                            std::views::transform([&](const auto &blk_info) {
                              return blk_info.first.server_id;
                            });

  /* Save the volume block address; in the "good" case, this will be
   * resolved to a server block address. However, if the request is rejected
   * in the "bad" case, we will replace it back to the volume block address
   * and attempt to resolve again to a different server.
   */
  const VolumeBlockAddr vdisk_start_sector = iod.start_sector;

  while (true) {
    ServerSet server_ids{server_ids_v.cbegin(), server_ids_v.cend()};

    while (!server_ids.empty()) {
      /* Replace the block address back to the volume block address to
       * attempt another resolve operation (only necessary in case we are
       * coming back here from a failure/retry case).
       */
      iod.start_sector = vdisk_start_sector;

      /* Select an appropriate server from the options available. */
      const auto server_id =
          sched_->SelectReadServer(&server_ids, vol_id_, &iod);
      if (!server_id) {
        DLOG(WARN) << "Failed to select read server; retrying...";

        /* All servers that are valid for this operation may currently be
         * known to reject reads; wait for recent stats to be pulled and try
         * again.
         */
        rt::Sleep(Duration(kServerStatsPullIntervalUs));
        return ProcessRequest(iod);
      }

      /* Get an RPC handle to the server to route the request to. */
      const auto srv = GetRPCClientForServer(*server_id);
      if (!srv) {
        LOG(ERR) << "Failed to get RPC client";
        return MakeError(srv);
      }

      /* Get block info for the chosen server. */
      auto blk_info_v = servers | std::views::filter([&](const auto &blk) {
                          return blk.first.server_id == server_id;
                        });
      const auto blk_info = blk_info_v.front().first;

      /* Update the IODesc to use the resolved block address in the server. */
      iod.start_sector = blk_info.block_addr;

      /* Process the request from a remote storage server. */
      auto resp = ProcessStorageOp(*srv, iod, req_id);
      if (!resp) {
        server_ids.erase(*server_id);
        num_read_retries_.inc_local();

        rt::Sleep(Duration(kServerStatsPullIntervalUs));
        continue;
      }

      const auto res =
          HandleStorageOpReply(std::move(resp.value()).get_buf(), *server_id);
      if (!res) {
        server_ids.erase(*server_id);

        if (res.error() == EBUSY) {
          num_read_rejections_.inc_local();
        } else {
          num_read_retries_.inc_local();
        }

        continue;
      }

      return *res;
    }

    rt::Sleep(Duration(kServerStatsPullIntervalUs));
  }

  std::unreachable();
}

/* Send kNumReplicas request and upon completion of all the requests, invoke the
 * callback function only once.
 *
 * Note:
 * This function will never return a failure and keep on retrying by recursively
 * calling ProcessRequest.
 *
 * TODO(girfan): Must put a limit to retries.
 */
Status<int> VirtualDiskRemote::ProcessWriteOp(
    const ServerReplicaBlockInfoList servers, IODesc iod, uint64_t req_id) {
  /* Each replica's write is performed on a separate thread. */
  std::array<sandook::rt::Thread, kNumReplicas> threads;

  /* Prevent multiple threads from attempting to retry; if any one gets rejected
   * we want to submit a single retry.
   */
  std::atomic_bool is_retrying = false;

  for (auto i = 0; i < kNumReplicas; i++) {
    const auto srv_info = servers.at(i);

    threads.at(i) = [this, srv_info = srv_info.first, req_id = req_id,
                     iod = iod, is_retrying = &is_retrying] mutable {
      auto srv = GetRPCClientForServer(srv_info.server_id);
      if (!srv) {
        LOG(ERR) << "Unable to get server info for the request" << srv.error();
        return;
      }

      /* Save the volume block address; in the "good" case, this will be
       * resolved to a server block address. However, if the request is rejected
       * in the "bad" case, we will replace it back to the volume block address
       * and attempt to resolve again to a different server.
       */
      const VolumeBlockAddr vdisk_start_sector = iod.start_sector;

      /* Replace the volume block address with the server block address after
       * resolving. This will be used for the storage operation sent to the
       * storage server.
       */
      iod.start_sector = srv_info.block_addr;
      auto ret = ProcessStorageOp(*srv, iod, req_id);
      if (!ret) {
        LOG(ERR) << "Failed to process storage op: " << ret.error();
        return;
      }

      auto res = HandleStorageOpReply(std::move(ret.value()).get_buf(),
                                      srv_info.server_id);
      if (!res) {
        if (res.error() == EROFS) {
          num_write_rejections_.inc_local();
        } else {
          num_write_retries_.inc_local();
        }

        if (is_retrying->exchange(true)) {
          /* Another retry after rejection is already in progress; abort. */
          return;
        }

        DLOG(WARN) << "Failed to process request on: " << srv_info.server_id
                   << " (" << res.error() << ")";

        /* Replace the block address back to the volume block address to
         * attempt another resolve operation.
         */
        iod.start_sector = vdisk_start_sector;

        // TODO(girfan): Need this?
        /* The server is in a different mode than what was known to the client
         * or is congested; wait for updated stats to arrive from the
         * controller and try again with a more updated view of the servers.
         */
        // rt::Sleep(Duration(kServerStatsPullIntervalUs));

        /* Retry on another server. Also, the control flow should not reach
         * the callback as the request is not completed.
         */
        [[maybe_unused]] const auto _ = ProcessRequest(iod);
        return;
      }
    };
  }

  for (auto &t : threads) {
    t.Join();
  }

  auto ret = static_cast<int>(iod.num_sectors) << kSectorShift;
  return ret;
}

VolumeID VirtualDiskRemote::Register() {
  const auto delta_us = utils::CalibrateTimeWithController(ctrl_.get());
  if (!delta_us) {
    throw std::runtime_error("Cannot calibrate time with the controller");
  }
  utils::SetControllerTimeCalibration(*delta_us);

  auto msg = CreateRegisterVolumeMsg(ip_, port_, num_sectors());
  const auto payload_size = GetMsgSize(msg.get());

  auto resp = ctrl_->Call(writable_span(msg.get(), payload_size));
  if (!resp) {
    throw std::runtime_error("Cannot register volume");
  }

  const auto ret = HandleRegisterVolumeReply(resp.get_buf());
  if (!ret) {
    throw std::runtime_error("Cannot register volume");
  }

  return *ret;
}

Status<VolumeID> VirtualDiskRemote::HandleRegisterVolumeReply(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(RegisterVolumeReplyMsg)) {
    return MakeError(EINVAL);
  }

  const auto *msg = reinterpret_cast<const RegisterVolumeReplyMsg *>(
      payload.data() + sizeof(MsgHeader));

  const auto sched_type = msg->sched_type;
  const auto vol_id = msg->vol_id;
  sched_ =
      std::make_unique<schedulers::data_plane::Scheduler>(sched_type, vol_id);

  for (int i = 0; i < msg->num_servers; i++) {
    const auto &srv = msg->servers.at(i);
    auto client =
        RPCClient::Connect(static_cast<const char *>(srv.ip), srv.port);
    const auto &[it, okay] =
        servers_.try_emplace(srv.id, std::move(client), ServerStats{});
    if (!okay) {
      LOG(ERR) << "Cannot add: " << static_cast<const char *>(srv.ip) << ":"
               << srv.port;
      throw std::runtime_error("Cannot add RPC client for server");
    }
    const auto add = sched_->AddServer(srv.id);
    if (!add) {
      LOG(ERR) << "Cannot add server to scheduler: " << srv.id;
      throw std::runtime_error("Cannot add server to scheduler");
    }
  };

  LOG(INFO) << "VolumeID = " << vol_id;

  return vol_id;
}

void VirtualDiskRemote::UpdateServerStats() {
  auto msg = CreateGetServerStatsMsg(vol_id_);
  const auto payload_size = GetMsgSize(msg.get());
  auto resp = ctrl_->Call(writable_span(msg.get(), payload_size));
  if (!resp) {
    LOG(ERR) << "Failed to get server stats";
    return;
  }

  auto ret = HandleGetServerStatsReply(resp.get_buf());
  if (!ret) {
    LOG(ERR) << "Failed to handle server stats";
    return;
  }
}

void VirtualDiskRemote::ServerStatsUpdater() {
  const Duration interval(kServerStatsPullIntervalUs);

  while (!stop_updates_) {
    UpdateServerStats();
    rt::Sleep(interval);
  }
}

Status<void> VirtualDiskRemote::HandleGetServerStatsReply(
    std::span<const std::byte> payload) {
  if (payload.size() < sizeof(GetServerStatsReplyMsg)) {
    return MakeError(EINVAL);
  }

  const auto *msg = reinterpret_cast<const GetServerStatsReplyMsg *>(
      payload.data() + sizeof(MsgHeader));

  assert(msg->num_servers <= kNumMaxServers);
  const ServerStatsList servers(msg->servers.cbegin(),
                                msg->servers.cbegin() + msg->num_servers);

  const auto ret = sched_->SetServerStats(servers);
  if (!ret) {
    LOG(ERR) << "Cannot set servers";
    return MakeError(ret);
  }

  return {};
}

void VirtualDiskRemote::GarbageCollector() {
  if (kGarbageCollectionIntervalUs == 0) {
    LOG(INFO) << "Garbage collection is disabled";
    return;
  }

  const Duration interval(kGarbageCollectionIntervalUs);

  while (!stop_gc_) {
    RunGarbageCollector();
    rt::Sleep(interval);
  }
}

void VirtualDiskRemote::RunGarbageCollector() {
  auto discarded_blks = blk_res_.GetAndResetDiscardedBlocks();
  if (!discarded_blks) {
    return;
  }

  if ((*discarded_blks).empty()) {
    return;
  }

  std::unordered_map<ServerID, std::vector<ServerBlockAddr>> server_blks;
  for (auto replicas : *discarded_blks) {
    for (auto replica : replicas) {
      const auto &blk_info = replica.first;
      server_blks[blk_info.server_id].emplace_back(blk_info.block_addr);
    }
  }

  size_t total_blocks_gc = 0;

  for (auto &[server_id, blks] : server_blks) {
    const auto srv = GetRPCClientForServer(server_id);
    if (!srv) {
      LOG(ERR) << "Failed to get RPC client";
      return;
    }

    auto *const server = *srv;
    auto num_blocks_remaining = blks.size();
    auto it_batch_start = blks.begin();

    while (num_blocks_remaining > 0) {
      const auto N = std::min(num_blocks_remaining, kDiscardBatch);

      std::array<ServerBlockAddr, kDiscardBatch> blocks;
      std::copy_n(std::make_move_iterator(it_batch_start), N, blocks.begin());

      auto discard_msg = CreateDiscardBlocksMsg(blocks, N);
      const auto msg_size = GetMsgSize(discard_msg.get());
      const auto ret = server->Call(writable_span(discard_msg.get(), msg_size));

      num_blocks_remaining -= N;
      it_batch_start += static_cast<int>(N);
      total_blocks_gc += N;

      inc_num_gc_blocks(N);
    }
  }

  LOG(DEBUG) << "Number of blocks garbage collected: " << total_blocks_gc;
}

}  // namespace sandook
