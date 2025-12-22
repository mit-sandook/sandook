#include "sandook/virtual_disk/virtual_disk_base.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sandook/bindings/runtime.h"

extern "C" {
#include <base/compiler.h>
}

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/log.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"

constexpr static uint32_t kMaxPerRequestConcurrency = 4;
constexpr static uint32_t kMaxPerWriteRequestConcurrency = 1;

namespace sandook {

namespace {

struct IOResultInternal {
  rt::ThreadWaker *waker;
  int *res;
} __attribute__((aligned(4)));

void IOCallback(CallbackArgs args, IOResult io_result) {
  switch (io_result.status) {
    case IOStatus::kOk:
      break;

    case IOStatus::kFailed:
      LOG(ERR) << "IO failed";
      break;

    default:
      LOG(ERR) << "Unknown IOStatus";
      break;
  }

  auto *cb_res = static_cast<IOResultInternal *>(args);
  *cb_res->res = static_cast<int>(io_result.res);

  // Wake up the caller thread as the last step.
  cb_res->waker->Wake();
}

}  // namespace

Status<void> VirtualDiskBase::SubmitRequest(IODesc iod) {
  static const auto waker = nullptr;
  SubmitRequestAndPark(iod, waker);

  return {};
}

Status<void> VirtualDiskBase::ProcessRequestAsync(IODesc iod) {
  rt::Spawn([this, iod] {
    const auto ret = ProcessShardedRequests(iod);
    ProcessCompletion(iod, ret);
  });

  return {};
}

Status<void> VirtualDiskBase::Allocate(uint64_t sector, uint32_t n_sectors) {
  rt::ThreadWaker waker;
  int res = 0;
  IOResultInternal cb_res{.waker = &waker, .res = &res};
  const IODesc iod{.op_flags = static_cast<unsigned>(OpType::kAllocate),
                   .num_sectors = n_sectors,
                   .start_sector = sector,
                   .addr = 0,
                   .callback_args = static_cast<CallbackArgs>(&cb_res),
                   .callback = IOCallback};

  SubmitRequestAndPark(iod, &waker);

  return {};
}

Status<int> VirtualDiskBase::Read(uint64_t sector, std::span<std::byte> buf) {
  rt::ThreadWaker waker;
  int res = 0;
  IOResultInternal cb_res{.waker = &waker, .res = &res};
  const IODesc iod{
      .op_flags = static_cast<unsigned>(OpType::kRead),
      .num_sectors = static_cast<uint32_t>(buf.size() >> kSectorShift),
      .start_sector = sector,
      .addr = reinterpret_cast<uint64_t>(buf.data()),
      .callback_args = static_cast<CallbackArgs>(&cb_res),
      .callback = IOCallback};

  SubmitRequestAndPark(iod, &waker);

  return res;
}

Status<int> VirtualDiskBase::Write(uint64_t sector,
                                   std::span<const std::byte> buf) {
  rt::ThreadWaker waker;
  int res = 0;
  IOResultInternal cb_res{.waker = &waker, .res = &res};
  const IODesc iod{
      .op_flags = static_cast<unsigned>(OpType::kWrite),
      .num_sectors = static_cast<uint32_t>(buf.size() >> kSectorShift),
      .start_sector = sector,
      .addr = reinterpret_cast<uint64_t>(buf.data()),
      .callback_args = static_cast<CallbackArgs>(&cb_res),
      .callback = IOCallback};

  SubmitRequestAndPark(iod, &waker);

  return res;
}

void VirtualDiskBase::SubmitRequestAndPark(IODesc iod,
                                           rt::ThreadWaker *requestor_waker) {
  rt::Preempt p;
  rt::PreemptGuard g(p);
  auto cpu_id = rt::Preempt::get_cpu();
  auto &[_, waker, reqs, lock] = work_queue_ths_.at(cpu_id);

  {
    const rt::SpinGuard guard(lock);

    reqs.emplace(iod);
    waker.Wake();
  }

  if (requestor_waker != nullptr) {
    g.Park(*requestor_waker);
  }
}

void VirtualDiskBase::RequestWorker(WorkQueueThread *work_queue_th) {
  auto &[_, waker, reqs, lock] = *work_queue_th;

  while (true) {
    bool stop = false;

    {
      rt::SpinGuard guard(lock);

      while (!stop_ && reqs.empty()) {
        guard.Park(waker);
      }

      while (!reqs.empty()) {
        auto iod = reqs.front();
        reqs.pop();

        ProcessRequestAsync(iod);
      }

      stop = stop_ && reqs.empty();
    }

    if (unlikely(stop)) {
      break;
    }
  }
}

IOResult VirtualDiskBase::ProcessShardedRequests(IODesc iod) {
  std::atomic_int res = 0;
  std::atomic_int err_code = 0;
  IOStatus status = IOStatus::kOk;

  const OpType op = IODesc::get_op(&iod);

  const auto max_concurrency = op == OpType::kWrite
                                   ? kMaxPerWriteRequestConcurrency
                                   : kMaxPerRequestConcurrency;
  const uint32_t nthreads = std::min(iod.num_sectors, max_concurrency);
  const uint32_t sectors_per_thread =
      nthreads == iod.num_sectors ? 1 : iod.num_sectors / nthreads;
  const uint32_t remaining = iod.num_sectors - (sectors_per_thread * nthreads);
  BUG_ON((sectors_per_thread * nthreads) + remaining != iod.num_sectors);

  std::vector<rt::Thread> threads(nthreads);
  for (uint32_t tid = 0; tid < nthreads; tid++) {
    threads.at(tid) = [this, tid, sectors_per_thread, remaining, nthreads, iod,
                       err_code = &err_code, res = &res] mutable {
      const uint32_t cur_thread_start_sector = tid * sectors_per_thread;
      uint32_t cur_thread_nsectors = sectors_per_thread;
      if (tid == nthreads - 1) {
        cur_thread_nsectors += remaining;
      }

      for (uint32_t i = 0; i < cur_thread_nsectors; i++) {
        const uint64_t cur_sector_offset = cur_thread_start_sector + i;
        auto iod_cur = iod;
        iod_cur.num_sectors = 1;
        iod_cur.start_sector = iod.start_sector + cur_sector_offset;
        iod_cur.addr = iod.addr + (static_cast<uint64_t>(kDeviceAlignment) *
                                   cur_sector_offset);

        auto ret = ProcessRequest(iod_cur);
        if (!ret) {
          err_code->exchange(ret.error().code());
        } else {
          res->fetch_add(*ret);
        }
      }
    };
  }

  for (auto &t : threads) {
    if (t.Joinable()) {
      t.Join();
    }
  }

  const auto err = err_code.load();
  if (err) {
    status = IOStatus::kFailed;
    return {.status = status, .res = err};
  }

  return {.status = status, .res = res.load()};
}

void VirtualDiskBase::ProcessCompletion(IODesc iod, IOResult io_result) {
  iod.callback(iod.callback_args, io_result);
}

void VirtualDiskBase::Stop() {
  for (uint64_t i = 0; i < rt::RuntimeMaxCores(); i++) {
    auto &[th, waker, _, lock] = work_queue_ths_.at(i);
    {
      const rt::SpinGuard guard(lock);
      stop_ = true;
      waker.Wake();
    }
    th.Join();
  }
}

}  // namespace sandook
