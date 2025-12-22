#include "sandook/disk_server/spdk_server.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/storage.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

SPDKServer::SPDKServer(RPCClient *ctrl)
    : StorageServer(
          ctrl,
          [] {
            const uint32_t sector_sz = 1 << kSectorShift;
            const auto dev_blk_sz = rt::Storage::get_block_size();
            if (dev_blk_sz != sector_sz) {
              throw std::runtime_error("The SPDK device's block size must be " +
                                       std::to_string(sector_sz) + " (!= " +
                                       std::to_string(dev_blk_sz) + ")");
            }
            const auto num_blks = rt::Storage::get_num_blocks();
            LOG(INFO) << "Sectors: " << num_blks;
            return num_blks;
          }(),
          [] {
            std::unique_ptr<char[]> const sn_buf(
                new char[kSPDKDeviceSerialNumberLen]);
            [[maybe_unused]] const auto sn_len =
                rt::Storage::get_serial_number(sn_buf.get());
            assert(sn_len == kSPDKDeviceSerialNumberLen);
            std::string serial_num = sn_buf.get();
            serial_num.erase(
                std::remove_if(
                    serial_num.begin(), serial_num.end(),
                    [](auto const &c) -> bool { return !std::isalnum(c); }),
                serial_num.end());
            serial_num.erase(
                std::remove(serial_num.begin(), serial_num.end(), ' '),
                serial_num.end());
            LOG(INFO) << "SerialNumber: " << serial_num;
            return serial_num;
          }()),
      gen(rd()),
      block_dist(0, rt::Storage::get_num_blocks() - 1) {}

[[nodiscard]] Status<int> SPDKServer::HandleStorageOp(
    const StorageOpMsg *msg, std::span<const std::byte> req_payload,
    std::span<std::byte> resp_payload) {
  const IODesc *iod = &msg->iod;
  const OpType op = IODesc::get_op(iod);
  const auto start_lba = iod->start_sector;
  const auto num_sectors = iod->num_sectors;
  const auto len = num_sectors << kSectorShift;

  switch (op) {
    case OpType::kRead: {
      assert(len == resp_payload.size());
      const auto start_time = hook_read_started();
      // const auto ret = rt::Storage::Read(resp_payload, block_dist(gen));
      const auto ret = rt::Storage::Read(resp_payload, start_lba);
      if (unlikely(!ret)) {
        hook_read_completed(start_time, false /* success */);
        LOG_ONCE(ERR) << "Read IO error: " << ret.error();
        return MakeError(ret);
      }
      hook_read_completed(start_time, true /* success */);
      return len;
    }

    case OpType::kWrite: {
      assert(len == req_payload.size());
      const auto start_time = hook_write_started();
      // const auto ret = rt::Storage::Write(req_payload, block_dist(gen));
      const auto ret = rt::Storage::Write(req_payload, start_lba);
      if (unlikely(!ret)) {
        hook_write_completed(start_time, false /* success */);
        LOG_ONCE(ERR) << "Write IO error: " << ret.error();
        return MakeError(ret);
      }
      hook_write_completed(start_time, true /* success */);
      return len;
    }

    case OpType::kDiscard: {
      const auto ret = rt::Storage::Deallocate(start_lba, num_sectors);
      if (unlikely(!ret)) {
        LOG_ONCE(ERR) << "Discard error: " << ret.error();
        return MakeError(ret);
      }
      return len;
    }

    default:
      LOG(ERR) << "Unsupported operation: " << static_cast<int>(op);
  }

  return MakeError(EINVAL);
}

[[nodiscard]] Status<void> SPDKServer::HandleDiscardBlocks(
    const std::vector<ServerBlockAddr> &blocks) {
  static const uint32_t one_block = 1;

  for (const auto blk_addr : blocks) {
    const auto ret = rt::Storage::Deallocate(blk_addr, one_block);
    if (unlikely(!ret)) {
      LOG(ERR) << "Discard error on block " << blk_addr << ": " << ret.error();
      return MakeError(ret);
    }
  }

  return {};
}

}  // namespace sandook
