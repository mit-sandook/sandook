#include "sandook/virtual_disk/virtual_disk_local.h"

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/storage.h"

namespace sandook {

Status<int> VirtualDiskLocal::ProcessRequest(IODesc iod) {
  auto op = IODesc::get_op(&iod);
  const auto lba = iod.start_sector;
  switch (op) {
    case OpType::kRead: {
      void *payload = reinterpret_cast<void *>(iod.addr);
      auto ret =
          rt::Storage::Read(readable_span(payload, kDeviceAlignment), lba);
      if (!ret) {
        return MakeError(ret);
      }
      break;
    }
    case OpType::kWrite: {
      const void *payload = reinterpret_cast<const void *>(iod.addr);
      auto ret =
          rt::Storage::Write(writable_span(payload, kDeviceAlignment), lba);
      if (!ret) {
        return MakeError(ret);
      }
      break;
    }
    case OpType::kAllocate: {
      break;
    }
    default:
      LOG(ERR) << "Unknown operation: " << static_cast<int>(op);
      break;
  }

  return {kDeviceAlignment};
}

}  // namespace sandook
