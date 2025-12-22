#include <linux/falloc.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "base/compiler.h"
#include "base/types.h"

extern "C" {
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errno.h"
}

#include <span>
#include <stdexcept>
#include <string>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/msg.h"
#include "sandook/bindings/log.h"
#include "sandook/disk_server/blk_server.h"
#include "sandook/disk_server/storage_server.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

BlkServer::BlkServer(RPCClient *ctrl, const std::string &dev)
    : StorageServer(ctrl, GetNumSectors(dev), kDefaultServerName),
      fd_(open(dev.c_str(), O_RDWR | O_DIRECT | O_SYNC)) {
  if (fd_ < 0) {
    throw std::runtime_error("Failed to open backing device");
  }
}

BlkServer::~BlkServer() { close(fd_); }

uint64_t BlkServer::GetNumSectors(const std::string &dev) {
  const int fd = open(dev.c_str(), O_RDWR);
  if (fd < 0) {
    throw std::runtime_error("Failed to open backing device");
  }

  struct stat st {};
  fstat(fd, &st);

  if (!S_ISBLK(st.st_mode)) {
    throw std::runtime_error("Backing device is not a block device");
  }

  uint64_t bytes = 0;
  int err = ioctl(fd, BLKGETSIZE64, &bytes);
  if (err != 0) {
    throw std::runtime_error("Failed to query device size");
  }

  uint64_t dev_block_sz = 0;
  err = ioctl(fd, BLKSSZGET, &dev_block_sz);
  if (err != 0) {
    throw std::runtime_error("Failed to query device block size");
  }

  close(fd);

  const auto num_sectors = bytes >> kSectorShift;

  LOG(INFO) << "Backing device: " << dev;
  LOG(INFO) << "\tSize: " << bytes << " bytes";
  LOG(INFO) << "\tBlockSize: " << dev_block_sz << " bytes";
  LOG(INFO) << "\tSectors: " << num_sectors;

  return num_sectors;
}

Status<int> BlkServer::HandleStorageOp(const StorageOpMsg *msg,
                                       std::span<const std::byte> req_payload,
                                       std::span<std::byte> resp_payload) {
  const IODesc *iod = &msg->iod;
  const OpType op = IODesc::get_op(iod);
  const unsigned len = iod->num_sectors << kSectorShift;
  const uint64_t offset = iod->start_sector << kSectorShift;
  int mode = FALLOC_FL_KEEP_SIZE;

  int result = 0;
  switch (op) {
    case OpType::kRead: {
      const auto start_time = hook_read_started();
      const auto ret = HandleRead(offset, len, resp_payload);
      if (unlikely(!ret)) {
        static const auto success = false;
        hook_read_completed(start_time, success);
        return MakeError(ret);
      }
      static const auto success = true;
      hook_read_completed(start_time, success);
      result = *ret;
    } break;

    case OpType::kWrite: {
      const auto start_time = hook_write_started();
      const auto ret = HandleWrite(offset, len, req_payload);
      if (unlikely(!ret)) {
        static const auto success = false;
        hook_write_completed(start_time, success);
        return MakeError(ret);
      }
      static const auto success = true;
      hook_write_completed(start_time, success);
      result = *ret;
    } break;

    case OpType::kFlush: {
      const auto ret = HandleFlush();
      if (!ret) {
        return MakeError(ret);
      }
    } break;

    case OpType::kWriteZeroes:
      mode |= FALLOC_FL_ZERO_RANGE;
      [[fallthrough]];

    case OpType::kDiscard: {
      const auto ret = HandleDiscard(offset, len, mode);
      if (!ret) {
        return MakeError(ret);
      }
    } break;

    default:
      return MakeError(EINVAL);
  }

  return result;
}

Status<void> BlkServer::Seek(uint64_t offset) const {
  const auto ret = lseek(fd_, static_cast<off_t>(offset), SEEK_SET);
  if (ret == -1) {
    return MakeError(errno);
  }
  return {};
}

Status<int> BlkServer::HandleRead(uint64_t offset, unsigned len,
                                  std::span<std::byte> resp_payload) const {
  assert(len <= resp_payload.size());

  const auto seek = Seek(offset);
  if (!seek) {
    return MakeError(seek);
  }

  const auto ret = ReadFull(fd_, resp_payload);
  if (!ret) {
    return MakeError(ret);
  }

  return len;
}

Status<int> BlkServer::HandleWrite(
    uint64_t offset, unsigned len,
    std::span<const std::byte> req_payload) const {
  assert(len <= req_payload.size());

  const auto seek = Seek(offset);
  if (!seek) {
    LOG(ERR) << "Cannot seek: " << offset;
    return MakeError(seek);
  }

  const auto ret = WriteFull(fd_, req_payload);
  if (!ret) {
    LOG(ERR) << "Cannot write: " << len;
    return MakeError(ret);
  }

  return len;
}

Status<void> BlkServer::HandleFlush() const {
  const int ret = fdatasync(fd_);
  if (ret != 0) {
    return MakeError(ret);
  }
  return {};
}

Status<void> BlkServer::HandleDiscard(uint64_t offset, unsigned len,
                                      int mode) const {
  const auto seek = Seek(offset);
  if (!seek) {
    return MakeError(seek);
  }

  const auto ret = fallocate(fd_, mode, static_cast<off_t>(offset), len);
  if (ret != 0) {
    return MakeError(ret);
  }

  return {};
}

}  // namespace sandook
