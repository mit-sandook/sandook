#include "sandook/disk_server/disk_server.h"

#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>

#include "sandook/bindings/log.h"
#include "sandook/config/config.h"
#include "sandook/disk_server/blk_server.h"
#include "sandook/disk_server/disk_conn_handler.h"
#include "sandook/disk_server/mem_server.h"
#include "sandook/disk_server/spdk_server.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

std::unique_ptr<sandook::StorageServer> storage_server_;  // NOLINT

namespace sandook {

void SignalHandler(int sig) { storage_server_->HandleSignal(sig); }

void DiskServer::Launch(const std::string& backing_device) {
  const auto* const ip = Config::kControllerIP.c_str();
  const auto port = Config::kControllerPort;
  const std::unique_ptr<RPCClient> ctrl = RPCClient::Connect(ip, port);

  switch (Config::kDiskServerBackend) {
    case Config::DiskServerBackend::kPOSIX:
      storage_server_ = std::make_unique<BlkServer>(ctrl.get(), backing_device);
      break;

    case Config::DiskServerBackend::kMemory:
      storage_server_ = std::make_unique<MemServer>(ctrl.get());
      break;

    case Config::DiskServerBackend::kSPDK:
      storage_server_ = std::make_unique<SPDKServer>(ctrl.get());
      break;

    default:
      throw std::runtime_error("Invalid disk server backend");
  }

  std::signal(SIGTERM, SignalHandler);

  DiskConnHandler handler(storage_server_.get());
  RPCServerInit(&handler, Config::kStorageServerPort,
                []() { LOG(INFO) << "Disk server started..."; });
}

}  // namespace sandook
