#include <cassert>

#include "base/compiler.h"
extern "C" {
#include <runtime/net.h>
}

#include <array>
#include <cerrno>
#include <cstdint>
#include <ranges>
#include <string>
#include <utility>

#include "sandook/base/error.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/bindings/log.h"
#include "sandook/controller/controller_agent.h"

namespace sandook {

ControllerAgent::ControllerAgent() {
  LOG(INFO) << "Sandook: ";
  LOG(INFO) << "\tReplicationFactor = " << kNumReplicas;
}

Status<ServerID> ControllerAgent::RegisterServer(const std::string &ip,
                                                 int port,
                                                 const std::string &name,
                                                 uint64_t n_sectors) {
  const auto server_id = next_server_id_.fetch_add(1);
  assert(server_id < kNumMaxServers);

  const auto blk_add = blk_alloc_.AddServer(server_id, n_sectors);
  if (!blk_add) {
    LOG(ERR) << "Cannot add server to block resolver";
    return MakeError(EINVAL);
  }

  const auto sched_add = sched_.AddServer(server_id, name);
  if (!sched_add) {
    LOG(ERR) << "Cannot add server to scheduler";
    return MakeError(EINVAL);
  }

  const auto &[it, ok] =
      servers_.try_emplace(server_id, server_id, ip, port, name, n_sectors);
  if (!ok) {
    LOG(ERR) << "Cannot add server";
    return MakeError(EINVAL);
  }

  LOG(INFO) << it->second;

  return server_id;
}

Status<DiskPeakIOPS> ControllerAgent::GetDiskPeakIOPS(
    ServerID server_id) const {
  return sched_.GetDiskPeakIOPS(server_id);
}

Status<VolumeID> ControllerAgent::RegisterVolume(const std::string &ip,
                                                 int port, uint64_t n_sectors) {
  const auto vol_id = vol_id_.fetch_add(1);
  assert(vol_id < kNumMaxVolumes);

  netaddr raddr{};
  const std::string addr = ip + ":" + std::to_string(port);
  str_to_netaddr(addr.c_str(), &raddr);

  const auto &[it, ok] = vols_.try_emplace(vol_id, vol_id, ip, port, n_sectors);
  if (!ok) {
    return MakeError(EINVAL);
  }
  LOG(INFO) << it->second;

  return vol_id;
}

Status<void> ControllerAgent::UpdateServerStats(ServerID server_id,
                                                ServerStats stats) {
  assert(servers_.find(server_id) != servers_.end());
  return sched_.UpdateServerStats(server_id, stats);
}

Status<void> ControllerAgent::CommitServerMode(ServerID server_id,
                                               ServerMode mode) {
  assert(servers_.find(server_id) != servers_.end());
  return sched_.CommitServerMode(server_id, mode);
}

Status<ServerStatsList> ControllerAgent::GetServerStats() {
  return sched_.GetServerStats();
}

Status<DataPlaneServerStats> ControllerAgent::GetDataPlaneServerStats(
    ServerID server_id) {
  return sched_.GetDataPlaneServerStats(server_id);
}

Status<ServerAllocationBlockInfoList> ControllerAgent::AllocateBlocks(
    ServerID server_id) {
  ServerAllocationBlockInfoList server_blks{};
  const auto blks = blk_alloc_.AllocateBlocks(server_id, server_blks.size());
  if (!blks) {
    LOG(ERR) << "Cannot allocate block in server: " << server_id;
    return MakeError(blks);
  }

  int next_blk = 0;
  for (const auto &blk : *blks) {
    server_blks.at(next_blk++) = blk;
  }

  return server_blks;
}

void ControllerAgent::HandleSignal(int sig) { sched_.HandleSignal(sig); }

}  // namespace sandook
