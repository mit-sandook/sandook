#pragma once

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/log.h"
#include "sandook/virtual_disk/virtual_disk_base.h"

namespace sandook {

class VirtualDiskLocal : public VirtualDiskBase {
 public:
  explicit VirtualDiskLocal(int n_sectors) : VirtualDiskBase(n_sectors) {
    LOG(INFO) << "Running VirtualDisk locally...";
  }

 protected:
  Status<int> ProcessRequest(IODesc iod) override;
};

}  // namespace sandook
