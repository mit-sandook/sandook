#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include "sandook/base/constants.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/config/config.h"

namespace sandook {

enum MsgType {
  kStorageOp = 0,
  kStorageOpReply = 1,
  kAllocateBlocks = 2,
  kAllocateBlocksReply = 3,
  kDiscardBlocks = 4,
  kRegisterServer = 5,
  kRegisterServerReply = 6,
  kRegisterVolume = 7,
  kRegisterVolumeReply = 8,
  kUpdateServerStats = 9,
  kUpdateServerStatsReply = 10,
  kCommitServerMode = 11,
  kGetServerStats = 12,
  kGetServerStatsReply = 13,
  kGetControllerTime = 14,
  kGetControllerTimeReply = 15
};

struct MsgHeader {
  /* Size of the message. */
  size_t len;

  /* Size of the payload sent after the message itself. */
  uint32_t payload_size;

  /* Type of the message. */
  MsgType type;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<MsgHeader> &&
              std::is_trivial_v<MsgHeader>);

struct ServerInfo {
  char ip[kIPAddrStrLen];
  int port;
  char name[kNameStrLen];
  uint32_t id;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<ServerInfo> &&
              std::is_trivial_v<ServerInfo>);

inline size_t GetMsgSize(const std::byte *base) {
  const auto *header = reinterpret_cast<const MsgHeader *>(base);
  return (header->len + sizeof(MsgHeader));
}

struct StorageOpMsg {
  /* Descriptor of the IO operation being performed. */
  IODesc iod;

  /* Pointer to the request object in the client.
   * Used to identify the request when processing response messages. */
  uint64_t req_id;

  /* If this is set to the Server ID of the destination server, the server will
   * never reject this request. */
  ServerID affinity;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<StorageOpMsg> &&
              std::is_trivial_v<StorageOpMsg>);

inline std::unique_ptr<std::byte[]> CreateStorageOpMsg(
    IODesc iod, uint64_t req_id, ServerID affinity = kInvalidServerID,
    uint32_t payload_size = 0) {
  auto buffer_len = sizeof(MsgHeader) + sizeof(StorageOpMsg) + payload_size;
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(buffer_len);
  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(StorageOpMsg) + payload_size;
  header->type = MsgType::kStorageOp;
  header->payload_size = payload_size;

  auto *msg =
      reinterpret_cast<StorageOpMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->iod = iod;
  msg->req_id = req_id;
  msg->affinity = affinity;

  return buffer;
}

enum StorageOpReplyCode {
  kSuccess = 0,
  kFailure = 1,
  kRejectModeMismatch = 2,
  kRejectDeviceBusy = 3,
  kSuccessCongested = 4
};

struct StorageOpReplyMsg {
  /* Descriptor of the IO operation being performed. */
  IODesc iod;

  /* Pointer to the request object in the client.
   * Used to identify the request when processing response messages. */
  uint64_t req_id;

  /* Code indicating the result of the IO operation or device state. */
  StorageOpReplyCode code;

  /* Result of the IO operation to pass on to the user/application. */
  int res;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<StorageOpReplyMsg> &&
              std::is_trivial_v<StorageOpReplyMsg>);

inline std::unique_ptr<std::byte[]> CreateStorageOpReplyMsg(
    IODesc iod, uint64_t req_id, uint32_t payload_size, int res,
    StorageOpReplyCode code) {
  auto response_size =
      sizeof(MsgHeader) + sizeof(StorageOpReplyMsg) + payload_size;

  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);
  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(StorageOpReplyMsg) + payload_size;
  header->type = MsgType::kStorageOpReply;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<StorageOpReplyMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->iod = iod;
  msg->req_id = req_id;
  msg->code = code;
  msg->res = res;

  return buffer;
}

struct AllocateBlocksMsg {
  /* Volume ID that made this request. */
  VolumeID vol_id;

  /* Server in which to allocate. */
  ServerID server_id;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<AllocateBlocksMsg> &&
              std::is_trivial_v<AllocateBlocksMsg>);

inline std::unique_ptr<std::byte[]> CreateAllocateBlocksMsg(
    VolumeID vol_id, ServerID server_id) {
  auto payload_size = sizeof(MsgHeader) + sizeof(AllocateBlocksMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(payload_size);
  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(AllocateBlocksMsg);
  header->type = MsgType::kAllocateBlocks;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<AllocateBlocksMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->vol_id = vol_id;
  msg->server_id = server_id;

  return buffer;
}

struct AllocateBlocksReplyMsg {
  /* Details of the block to access from a disk server. */
  ServerAllocationBlockInfoList server_blks;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<AllocateBlocksReplyMsg> &&
              std::is_trivial_v<AllocateBlocksReplyMsg>);

inline std::unique_ptr<std::byte[]> CreateAllocateBlocksReplyMsg(
    ServerAllocationBlockInfoList server_blks) {
  auto response_size = sizeof(MsgHeader) + sizeof(AllocateBlocksReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(AllocateBlocksReplyMsg);
  header->type = MsgType::kAllocateBlocksReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<AllocateBlocksReplyMsg *>(buffer.get() +
                                                         sizeof(MsgHeader));
  for (size_t i = 0; i < server_blks.size(); i++) {
    msg->server_blks.at(i) = server_blks.at(i);
  }

  return buffer;
}

struct DiscardBlocksMsg {
  /* List of blocks to discard. */
  std::array<ServerBlockAddr, kDiscardBatch> blocks;

  /* Number of blocks inserted in the list. */
  size_t num_blocks;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<DiscardBlocksMsg> &&
              std::is_trivial_v<DiscardBlocksMsg>);

inline std::unique_ptr<std::byte[]> CreateDiscardBlocksMsg(
    std::array<ServerBlockAddr, kDiscardBatch> blocks, size_t num_blocks) {
  assert(num_blocks <= kDiscardBatch);
  auto payload_size = sizeof(MsgHeader) + sizeof(DiscardBlocksMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(payload_size);
  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(DiscardBlocksMsg);
  header->type = MsgType::kDiscardBlocks;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<DiscardBlocksMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->blocks = blocks;
  msg->num_blocks = num_blocks;

  return buffer;
}

struct RegisterServerMsg {
  /* IP address of this storage server; for clients to connect to. */
  char ip[kIPAddrStrLen];

  /* Port of this storage server; for clients to connect to. */
  int port;

  /* Name of this server. */
  char name[kNameStrLen];

  /* Number of sectors available at this storage server. */
  uint64_t nsectors;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<RegisterServerMsg> &&
              std::is_trivial_v<RegisterServerMsg>);

inline std::unique_ptr<std::byte[]> CreateRegisterServerMsg(
    const std::string &ip, int port, const std::string &name,
    uint64_t nsectors) {
  assert(name.size() <= kNameStrLen);
  assert(ip.size() <= kIPAddrStrLen);
  auto response_size = sizeof(MsgHeader) + sizeof(RegisterServerMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(RegisterServerMsg);
  header->type = MsgType::kRegisterServer;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<RegisterServerMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->port = port;
  msg->nsectors = nsectors;
  void *msg_name = static_cast<void *>(msg->name);
  memset(msg_name, '\0', kNameStrLen);
  std::strncpy(static_cast<char *>(msg->ip), ip.c_str(), ip.size());
  std::strncpy(static_cast<char *>(msg->name), name.c_str(), name.size());

  return buffer;
}

struct RegisterServerReplyMsg {
  /* ID assigned to this server. */
  ServerID server_id;

  /* Are IO rejections at the disk server (before submitting to the device)
   * enabled?
   */
  bool is_rejections_enabled;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<RegisterServerReplyMsg> &&
              std::is_trivial_v<RegisterServerReplyMsg>);

inline std::unique_ptr<std::byte[]> CreateRegisterServerReplyMsg(
    ServerID server_id, bool is_rejections_enabled) {
  auto response_size = sizeof(MsgHeader) + sizeof(RegisterServerReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(RegisterServerReplyMsg);
  header->type = MsgType::kRegisterServerReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<RegisterServerReplyMsg *>(buffer.get() +
                                                         sizeof(MsgHeader));
  msg->server_id = server_id;
  msg->is_rejections_enabled = is_rejections_enabled;

  return buffer;
}

struct RegisterVolumeMsg {
  /* IP address of the client registering with the controller. */
  char ip[kIPAddrStrLen];

  /* Port of the client. */
  int port;

  /* Number of sectors in the volume. */
  uint64_t nsectors;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<RegisterVolumeMsg> &&
              std::is_trivial_v<RegisterVolumeMsg>);

inline std::unique_ptr<std::byte[]> CreateRegisterVolumeMsg(
    const std::string &ip, int port, uint64_t nsectors) {
  assert(ip.size() <= kIPAddrStrLen);
  auto payload_size = sizeof(MsgHeader) + sizeof(RegisterVolumeMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(payload_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(RegisterVolumeMsg);
  header->type = MsgType::kRegisterVolume;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<RegisterVolumeMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->port = port;
  msg->nsectors = nsectors;
  std::strncpy(static_cast<char *>(msg->ip), ip.c_str(), ip.size());

  return buffer;
}

struct RegisterVolumeReplyMsg {
  /* Information about servers registered with the controller. */
  std::array<ServerInfo, kNumMaxServers> servers;

  /* Number of servers whose information is sent in the 'servers' field. */
  int num_servers;

  /* ID assigned to this volume by the controller. */
  uint32_t vol_id;

  /* Scheduling policy to run at the client. */
  Config::DataPlaneSchedulerType sched_type;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<RegisterVolumeReplyMsg> &&
              std::is_trivial_v<RegisterVolumeReplyMsg>);

inline void AddServer(RegisterVolumeReplyMsg *msg, ServerInfo info) {
  assert(msg->num_servers < kNumMaxServers);
  msg->servers.at(msg->num_servers++) = info;
}

inline std::unique_ptr<std::byte[]> CreateRegisterVolumeReplyMsg(
    uint32_t vol_id, Config::DataPlaneSchedulerType sched_type) {
  auto reponse_size = sizeof(MsgHeader) + sizeof(RegisterVolumeReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(reponse_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(RegisterVolumeReplyMsg);
  header->type = MsgType::kRegisterVolumeReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<RegisterVolumeReplyMsg *>(buffer.get() +
                                                         sizeof(MsgHeader));
  msg->num_servers = 0;
  msg->vol_id = vol_id;
  msg->sched_type = sched_type;

  return buffer;
}

struct UpdateServerStatsMsg {
  /* Server sending this msg. */
  ServerID server_id;

  /* Latest statistics about this server. */
  ServerStats stats;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<UpdateServerStatsMsg> &&
              std::is_trivial_v<UpdateServerStatsMsg>);

inline std::unique_ptr<std::byte[]> CreateUpdateServerStatsMsg(
    ServerID server_id, ServerStats stats) {
  auto response_size = sizeof(MsgHeader) + sizeof(UpdateServerStatsMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(UpdateServerStatsMsg);
  header->type = MsgType::kUpdateServerStats;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<UpdateServerStatsMsg *>(buffer.get() +
                                                       sizeof(MsgHeader));
  msg->server_id = server_id;
  stats.server_id = server_id;
  msg->stats = stats;

  return buffer;
}

struct UpdateServerStatsReplyMsg {
  /* Server sending this msg. */
  ServerID server_id;

  /* Latest mode suggested to this server. */
  ServerMode mode;

  /* Congestion state of the server. */
  ServerCongestionState congestion_state;

  /* Weights assigned to this server. */
  ServerWeight read_weight;
  ServerWeight write_weight;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<UpdateServerStatsReplyMsg> &&
              std::is_trivial_v<UpdateServerStatsReplyMsg>);

inline std::unique_ptr<std::byte[]> CreateUpdateServerStatsReplyMsg(
    ServerID server_id, ServerMode mode, ServerCongestionState congestion_state,
    ServerWeight read_weight, ServerWeight write_weight) {
  auto response_size = sizeof(MsgHeader) + sizeof(UpdateServerStatsReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(UpdateServerStatsReplyMsg);
  header->type = MsgType::kUpdateServerStatsReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<UpdateServerStatsReplyMsg *>(buffer.get() +
                                                            sizeof(MsgHeader));
  msg->server_id = server_id;
  msg->mode = mode;
  msg->congestion_state = congestion_state;
  msg->read_weight = read_weight;
  msg->write_weight = write_weight;

  return buffer;
}

struct CommitServerModeMsg {
  /* Server sending this msg. */
  ServerID server_id;

  /* Acknowledged mode. */
  ServerMode mode;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<CommitServerModeMsg> &&
              std::is_trivial_v<CommitServerModeMsg>);

inline std::unique_ptr<std::byte[]> CreateCommitServerModeMsg(
    ServerID server_id, ServerMode mode) {
  auto response_size = sizeof(MsgHeader) + sizeof(CommitServerModeMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(CommitServerModeMsg);
  header->type = MsgType::kCommitServerMode;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<CommitServerModeMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->server_id = server_id;
  msg->mode = mode;

  return buffer;
}

struct GetServerStatsMsg {
  /* Volume ID that made this request. */
  VolumeID vol_id;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<GetServerStatsMsg> &&
              std::is_trivial_v<GetServerStatsMsg>);

inline std::unique_ptr<std::byte[]> CreateGetServerStatsMsg(VolumeID vol_id) {
  auto response_size = sizeof(MsgHeader) + sizeof(GetServerStatsMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(GetServerStatsMsg);
  header->type = MsgType::kGetServerStats;
  header->payload_size = 0;

  auto *msg =
      reinterpret_cast<GetServerStatsMsg *>(buffer.get() + sizeof(MsgHeader));
  msg->vol_id = vol_id;

  return buffer;
}

struct GetServerStatsReplyMsg {
  /* Volume ID that made this request. */
  VolumeID vol_id;

  /* Number of servers whose information is sent in 'servers'. */
  int num_servers;
  std::array<ServerStats, kNumMaxServers> servers;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<GetServerStatsReplyMsg> &&
              std::is_trivial_v<GetServerStatsReplyMsg>);

inline void AddServerStats(GetServerStatsReplyMsg *msg, ServerStats stats) {
  assert(msg->num_servers < kNumMaxServers);
  msg->servers.at(msg->num_servers++) = stats;
}

inline std::unique_ptr<std::byte[]> CreateGetServerStatsReplyMsg(
    VolumeID vol_id) {
  auto response_size = sizeof(MsgHeader) + sizeof(GetServerStatsReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(GetServerStatsReplyMsg);
  header->type = MsgType::kGetServerStatsReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<GetServerStatsReplyMsg *>(buffer.get() +
                                                         sizeof(MsgHeader));
  msg->num_servers = 0;
  msg->vol_id = vol_id;

  return buffer;
}

struct GetControllerTimeMsg {
  /* No content. */
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<GetControllerTimeMsg> &&
              std::is_trivial_v<GetControllerTimeMsg>);

inline std::unique_ptr<std::byte[]> CreateGetControllerTimeMsg() {
  auto response_size = sizeof(MsgHeader) + sizeof(GetControllerTimeMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(GetControllerTimeMsg);
  header->type = MsgType::kGetControllerTime;
  header->payload_size = 0;

  return buffer;
}

struct GetControllerTimeReplyMsg {
  /* Current time in microseconds at the controller. */
  uint64_t microtime;
} __attribute__((aligned(4)));

static_assert(std::is_standard_layout_v<GetControllerTimeReplyMsg> &&
              std::is_trivial_v<GetControllerTimeReplyMsg>);

inline std::unique_ptr<std::byte[]> CreateGetControllerTimeReplyMsg(
    uint64_t microtime) {
  auto response_size = sizeof(MsgHeader) + sizeof(GetControllerTimeReplyMsg);
  auto buffer = std::make_unique_for_overwrite<std::byte[]>(response_size);

  auto *header = reinterpret_cast<MsgHeader *>(buffer.get());
  header->len = sizeof(GetControllerTimeReplyMsg);
  header->type = MsgType::kGetControllerTimeReply;
  header->payload_size = 0;

  auto *msg = reinterpret_cast<GetControllerTimeReplyMsg *>(buffer.get() +
                                                            sizeof(MsgHeader));
  msg->microtime = microtime;

  return buffer;
}

}  // namespace sandook
