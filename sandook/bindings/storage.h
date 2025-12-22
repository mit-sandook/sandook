// storage.h - support for flash storage

#pragma once

extern "C" {
#include <runtime/storage.h>
}

#include <cstddef>
#include <cstdint>
#include <span>

#include "sandook/base/error.h"

namespace sandook::rt {

// TODO(zainruan): this should be per-device.
class Storage {
 public:
  // Write contiguous storage blocks.
  static Status<void> Write(std::span<const std::byte> src,
                            uint64_t start_lba) {
    auto ret = storage_write(src.data(), start_lba,
                             src.size_bytes() / get_block_size());
    if (ret != 0) {
      return MakeError(-ret);
    }
    return {};
  }

  // Read contiguous storage blocks.
  static Status<void> Read(std::span<std::byte> dst, uint64_t start_lba) {
    auto ret = storage_read(dst.data(), start_lba,
                            dst.size_bytes() / get_block_size());
    if (ret != 0) {
      return MakeError(-ret);
    }
    return {};
  }

  // Discard storage blocks.
  static Status<void> Deallocate(uint64_t start_lba, uint32_t num_sectors) {
    auto ret = storage_deallocate(start_lba, num_sectors);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return {};
  }

  // Returns the size of each block.
  static uint32_t get_block_size() { return storage_block_size(); }

  // Returns the capacity of the device in blocks.
  static uint64_t get_num_blocks() { return storage_num_blocks(); }

  // Returns the capacity of the device in bytes.
  static uint64_t get_num_bytes() {
    return get_num_blocks() * get_block_size();
  }

  // Returns the serial number of the device in the provided buffer.
  // The serial number size is STORAGE_DEVICE_SN_LEN.
  // The return value is the number of bytes written into the buffer.
  static int get_serial_number(char *sn) { return storage_serial_number(sn); }
};

}  // namespace sandook::rt
