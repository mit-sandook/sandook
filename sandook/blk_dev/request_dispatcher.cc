#include "sandook/blk_dev/request_dispatcher.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <experimental/scope>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/queue_info.h"
#include "sandook/bindings/sync.h"
#include "sandook/virtual_disk/virtual_disk.h"
#include "ublk_cmd.h"
#include "ublksrv.h"
#include "ublksrv_aio.h"

namespace sandook {

CompletionQueue completion_queues[kMaxNumCores][kMaxNumCores];  // NOLINT

namespace {

struct IOResultInternal {
  const struct ublksrv_queue *q;
  const struct ublk_io_data *data;
};

constexpr auto kPayloadSizeBytes = 1 << kSectorShift;
constexpr auto kPayloadSizeSectors = kPayloadSizeBytes >> kSectorShift;
constexpr auto kLogAllocationProgressIntervalUs = 2 * kOneSecond;

Status<void> AllocateBlocks(VirtualDisk *vdisk) {
  std::cout << "Allocating " << vdisk->num_sectors() << " blocks...\n";

  const uint64_t kBatchSize = 4096;
  const auto sectors_to_allocate = vdisk->num_sectors() / kPayloadSizeSectors;

  auto last_log_time_us = MicroTime();
  auto rem_sectors_to_allocate = sectors_to_allocate;
  uint64_t lba = 0;

  while (rem_sectors_to_allocate > 0) {
    const auto batch = std::min(kBatchSize, rem_sectors_to_allocate);
    const auto ret = vdisk->Allocate(lba, batch);
    if (!ret) {
      throw std::runtime_error("Cannot allocate blocks");
    }
    lba += batch;
    rem_sectors_to_allocate -= batch;

    if (MicroTime() - last_log_time_us > kLogAllocationProgressIntervalUs) {
      last_log_time_us = MicroTime();
      const auto allocated = sectors_to_allocate - rem_sectors_to_allocate;
      std::cout << allocated << "/" << sectors_to_allocate << " = "
                << 100.0 * allocated / sectors_to_allocate << "%\n";
    }
  }

  std::cout << "VirtualDisk allocation complete!\n";

  return {};
}

}  // namespace

void CompletionQueue::producer_push(CompletionReq req) {
  rt::SpinGuard g(spin_);

  active_q_.push_back(req);
}

std::deque<CompletionReq> &CompletionQueue::consumer_acquire() {
  inactive_q_.clear();

  {
    rt::SpinGuard g(spin_);

    active_q_.swap(inactive_q_);
  }

  return inactive_q_;
}

bool CompletionQueue::is_empty() { return active_q_.empty(); }

RequestDispatcher::RequestDispatcher(int nsectors) {
  vdisk_ = std::make_unique<VirtualDisk>(nsectors);
  if (!AllocateBlocks(vdisk_.get())) {
    throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
  }
}

int RequestDispatcher::SubmitRequest(const struct ublksrv_queue *q,
                                     const struct ublk_io_data *data) {
  rt::Spawn([this, data = data, q = q]() {
    const auto *iod = data->iod;
    const auto nsectors = iod->nr_sectors / kNumLinuxSectorsPerSandookSector;
    const auto sector = iod->start_sector / kNumLinuxSectorsPerSandookSector;
    const auto nbytes = nsectors << kSectorShift;
    auto *const buf = reinterpret_cast<void *>(iod->addr);

    Status<int> io_ret;
    const auto op = ublksrv_get_op(iod);
    switch (op) {
      case UBLK_IO_OP_READ:
        io_ret = vdisk_->Read(sector, readable_span(buf, nbytes));
        break;
      case UBLK_IO_OP_WRITE:
        io_ret = vdisk_->Write(sector, writable_span(buf, nbytes));
        break;
      default:
        std::cerr << "Unknown IO operation: " << op << '\n';
        break;
    }

    int res = 0;
    if (!io_ret) {
      std::cerr << "IO Failed" << '\n';
      res = -EINVAL;
    } else {
      res = static_cast<int>(data->iod->nr_sectors) << kLinuxSectorShift;
    }

    {
      rt::Preempt p;
      rt::PreemptGuard g(p);

      completion_queues[sandook::rt::Preempt::get_cpu()][q->q_id].producer_push(
          CompletionReq{q, static_cast<unsigned int>(data->tag), res});
    }
  });

  return 0;
}

}  // namespace sandook
