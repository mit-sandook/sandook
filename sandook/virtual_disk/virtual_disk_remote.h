#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/core_local_cache.h"
#include "sandook/base/counter.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/runtime.h"
#include "sandook/bindings/thread.h"
#include "sandook/config/config.h"
#include "sandook/rpc/rpc.h"
#include "sandook/scheduler/data_plane/scheduler.h"
#include "sandook/virtual_disk/block_resolver.h"
#include "sandook/virtual_disk/virtual_disk_base.h"

/* Handle to each remote server.
 *
 * First: RPCClient to communicate with the server.
 * Second: Metadata about the server as received by UpdateStats messages.
 */
using ServerHandle =
    std::pair<std::unique_ptr<sandook::RPCClient>, sandook::ServerStats>;

namespace sandook {

constexpr auto kPerCoreCachedBlocks = kAllocationBatch;

class VirtualDiskRemote : public VirtualDiskBase {
 public:
  explicit VirtualDiskRemote(uint64_t n_sectors)
      : VirtualDiskBase(n_sectors),
        ctrl_(RPCClient::Connect(Config::kControllerIP.c_str(),
                                 Config::kControllerPort)),
        ip_(Config::kVirtualDiskIP),
        port_(Config::kVirtualDiskPort),
        affinity_(Config::kVirtualDiskServerAffinity),
        vol_id_(Register()),
        blk_res_(n_sectors),
        th_ctrl_stats_([this] { ServerStatsUpdater(); }),
        th_gc_([this] { GarbageCollector(); }) {
    for (const auto &[server_id, _] : servers_) {
      if (affinity_ != kInvalidServerID) {
        /* If affinity to a disk server is set, only allocate block cache for
         * that server.
         */
        if (server_id != affinity_) {
          continue;
        }
      }
      blk_caches_.at(server_id) =
          std::make_unique<CoreLocalCache<ServerBlockInfo>>(
              kPerCoreCachedBlocks,
              [this, server_id]() { return AllocateBlocks(server_id); });
      blk_caches_.at(server_id)->reserve(
          static_cast<size_t>(rt::RuntimeMaxCores()) * kPerCoreCachedBlocks);
    }

    LOG(INFO) << "VirtualDisk created with " << n_sectors << " sectors";
  }

  ~VirtualDiskRemote() override;

  /* No copying. */
  VirtualDiskRemote(const VirtualDiskRemote &) = delete;
  VirtualDiskRemote &operator=(const VirtualDiskRemote &) = delete;

  /* No moving. */
  VirtualDiskRemote(VirtualDiskRemote &&) noexcept;
  VirtualDiskRemote &operator=(VirtualDiskRemote &&) noexcept;

 protected:
  Status<int> ProcessRequest(IODesc iod) override;

 private:
  /* Scheduler for selecting which server to route requests to. */
  std::unique_ptr<schedulers::data_plane::Scheduler> sched_;

  /* Handles to remote servers.
   *
   * Key: Storage server ID.
   * Value: Handle to the remote server.
   */
  std::unordered_map<ServerID, ServerHandle> servers_;

  /* RPCClient to communicate with the controller. */
  std::unique_ptr<RPCClient> ctrl_;

  /* IP and port of this virtual disk. */
  std::string ip_;
  int port_{};

  /* Affinity towards a particular server. If this is not kInvalidServerID, then
   * all requests will be routed to just that server.
   */
  ServerID affinity_{kInvalidServerID};

  /* Volume ID assigned by the controller upon registration. */
  VolumeID vol_id_{0};

  /* Cached entries (from controller) for virtual block to server block. */
  virtual_disk::BlockResolver blk_res_;

  /* Cache of pre-allocated blocks from the controller. */
  std::array<std::unique_ptr<CoreLocalCache<ServerBlockInfo>>, kNumMaxServers>
      blk_caches_;

  /* Thread to periodically pull server stats from the controller. */
  rt::Thread th_ctrl_stats_;
  bool stop_updates_{false};

  /* Thread to periodically perform garbage collection of discarded blocks. */
  rt::Thread th_gc_;
  bool stop_gc_{false};

  /* Track the number of rejections. */
  ThreadSafeCounter num_read_rejections_;
  ThreadSafeCounter num_write_rejections_;

  /* Track the number of retries. */
  ThreadSafeCounter num_read_retries_;
  ThreadSafeCounter num_write_retries_;
  ThreadSafeCounter num_reads_submitted_;
  ThreadSafeCounter num_writes_submitted_;

  /* Register this virtual disk with the controller. */
  VolumeID Register();

  /* Process the request by attempting to resolve the block from local cache. */
  Status<ServerReplicaBlockInfoList> ResolveBlock(const IODesc *iod);

  /* Get allocated blocks. */
  Status<ServerReplicaBlockInfoList> GetBlocks(const IODesc *iod,
                                               bool set_dirty);

  /* Get allocated blocks at the disk server with affinity to this virtual disk.
   * Only applicable when affinity_ is set.
   */
  Status<ServerReplicaBlockInfoList> GetBlocksWithAffinity(const IODesc *iod,
                                                           bool set_dirty);

  /* Allocate blocks to this virtual disk from the controller. */
  std::vector<ServerBlockInfo *> AllocateBlocks(ServerID server_id);

  /* Submit the IO request to the server and process the response. */
  Status<RPCReturnBuffer> ProcessStorageOp(RPCClient *server, IODesc iod,
                                           uint64_t req_id);

  /* Submit a read request to one of the given servers. */
  Status<int> ProcessReadOp(ServerReplicaBlockInfoList servers, IODesc iod,
                            uint64_t req_id);

  /* Submit write requests to multiple servers */
  Status<int> ProcessWriteOp(ServerReplicaBlockInfoList servers, IODesc iod,
                             uint64_t req_id);

  /* Update server stats periodically. */
  void ServerStatsUpdater();
  void UpdateServerStats();

  /* Garbage collection of overwritten/discarded blocks. */
  void GarbageCollector();
  void RunGarbageCollector();

  /* Handle response of registering with the controller. */
  Status<VolumeID> HandleRegisterVolumeReply(
      std::span<const std::byte> payload);

  /* Handle the response from the controller for allocating blocks. */
  static Status<ServerAllocationBlockInfoList> HandleAllocateBlocksReply(
      std::span<const std::byte> payload);

  /* Handle the response of the server stats request. */
  Status<void> HandleGetServerStatsReply(std::span<const std::byte> payload);

  /* Handle the response of the IO request from the server. */
  Status<int> HandleStorageOpReply(std::span<const std::byte> payload,
                                   ServerID server_id);

  /* Get a RPCClient to the server with the given ID. */
  Status<RPCClient *> GetRPCClientForServer(uint32_t server_id) {
    assert(servers_.find(server_id) != servers_.end());
    return servers_[server_id].first.get();
  }
};

}  // namespace sandook
