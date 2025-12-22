#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/config/config.h"
#include "sandook/virtual_disk/virtual_disk_base.h"
#include "sandook/virtual_disk/virtual_disk_local.h"
#include "sandook/virtual_disk/virtual_disk_remote.h"

namespace sandook {

class VirtualDisk {
 public:
  explicit VirtualDisk(int n_sectors)
      : VirtualDisk(n_sectors, Config::kVirtualDiskType) {}

  explicit VirtualDisk(int n_sectors, Config::VirtualDiskType vdisk_type) {
    switch (vdisk_type) {
      case Config::VirtualDiskType::kRemote:
        vdisk_ = std::make_unique<VirtualDiskRemote>(n_sectors);
        break;

      case Config::VirtualDiskType::kLocal:
        vdisk_ = std::make_unique<VirtualDiskLocal>(n_sectors);
        break;

      default:
        throw std::runtime_error("Unknown virtual disk");
    }
  }

  ~VirtualDisk() = default;

  /* No copying. */
  VirtualDisk(const VirtualDisk &) = delete;
  VirtualDisk &operator=(const VirtualDisk &) = delete;

  /* No moving. */
  VirtualDisk(VirtualDisk &&other) = delete;
  VirtualDisk &operator=(VirtualDisk &&other) = delete;

  Status<void> SubmitRequest(IODesc iod) { return vdisk_->SubmitRequest(iod); }

  Status<void> ProcessRequestAsync(IODesc iod) {
    return vdisk_->ProcessRequestAsync(iod);
  }

  Status<void> Allocate(uint64_t sector, uint32_t n_sectors) {
    return vdisk_->Allocate(sector, n_sectors);
  }

  Status<int> Read(uint64_t sector, std::span<std::byte> buf) {
    return vdisk_->Read(sector, buf);
  }

  Status<int> Write(uint64_t sector, std::span<const std::byte> buf) {
    return vdisk_->Write(sector, buf);
  }

  [[nodiscard]] uint64_t num_sectors() const { return vdisk_->num_sectors(); }

  [[nodiscard]] uint64_t num_gc_blocks() const {
    return vdisk_->num_gc_blocks();
  }

 private:
  std::unique_ptr<VirtualDiskBase> vdisk_;
};

}  // namespace sandook
