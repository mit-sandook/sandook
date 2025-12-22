#include "sandook/disk_server/mem_server.h"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <string>

#include "base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/bindings/log.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace {

constexpr auto kBlockSizeBytes = 4096;
constexpr auto kCapacityBytes = 1L << 33;  // 8GB

std::string random_string(std::size_t length) {
  const std::string CHARACTERS =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  std::random_device random_device;
  std::mt19937 generator(random_device());
  std::uniform_int_distribution<> distribution(
      0, static_cast<int>(CHARACTERS.size()) - 1);

  std::string random_string;
  for (std::size_t i = 0; i < length; ++i) {
    random_string += CHARACTERS[distribution(generator)];
  }

  return random_string;
}

}  // namespace

namespace sandook {

MemServer::MemServer(RPCClient *ctrl)
    : StorageServer(
          ctrl,
          [] {
            const auto num_blks = kCapacityBytes / kBlockSizeBytes;
            LOG(INFO) << "Bytes  : " << kCapacityBytes;
            LOG(INFO) << "Sectors: " << num_blks;
            return num_blks;
          }(),
          [] {
            const auto name = "memserver_" + random_string(5);
            LOG(INFO) << "Name: " << name;
            return name;
          }()) {
  buf_.resize(kCapacityBytes);
}

Status<void> MemServer::HandleReadOp(std::span<std::byte> dst,
                                     uint64_t start_lba) {
  const auto off = start_lba * kBlockSizeBytes;
  assert(off >= 0 && off < buf_.size());
  assert(off + dst.size_bytes() < buf_.size());
  const std::span<std::byte> buf(buf_);
  const auto *src = buf.subspan(off, dst.size_bytes()).data();
  std::memcpy(dst.data(), src, dst.size_bytes());

  return {};
}

Status<void> MemServer::HandleWriteOp(std::span<const std::byte> src,
                                      uint64_t start_lba) {
  const auto off = start_lba * kBlockSizeBytes;
  assert(off >= 0 && off < buf_.size());
  assert(off + src.size_bytes() < buf_.size());
  const std::span<std::byte> buf(buf_);
  auto *dst = buf.subspan(off, src.size_bytes()).data();
  std::memcpy(dst, src.data(), src.size_bytes());

  return {};
}

[[nodiscard]] Status<int> MemServer::HandleStorageOp(
    const StorageOpMsg *msg, std::span<const std::byte> req_payload,
    std::span<std::byte> resp_payload) {
  const IODesc *iod = &msg->iod;
  const OpType op = IODesc::get_op(iod);
  const uint64_t start_lba = iod->start_sector;
  auto len = iod->num_sectors << kSectorShift;

  switch (op) {
    case OpType::kRead: {
      assert(len == resp_payload.size());
      const auto start_time = hook_read_started();
      const auto ret = HandleReadOp(resp_payload, start_lba);
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
      const auto ret = HandleWriteOp(req_payload, start_lba);
      if (unlikely(!ret)) {
        hook_write_completed(start_time, false /* success */);
        LOG_ONCE(ERR) << "Write IO error: " << ret.error();
        return MakeError(ret);
      }
      hook_write_completed(start_time, true /* success */);
      return len;
    }

    default:
      LOG(ERR) << "Unsupported operation: " << static_cast<int>(op);
  }

  return MakeError(EINVAL);
}

}  // namespace sandook
