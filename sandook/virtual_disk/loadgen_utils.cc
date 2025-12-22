#include "sandook/virtual_disk/loadgen_utils.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

#include "base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/time.h"
#include "sandook/bindings/runtime.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/virtual_disk/virtual_disk.h"

namespace sandook {

constexpr auto kPayloadSectors = kPayloadSize >> kSectorShift;
constexpr auto kPerCoreCacheCapacity = 128;
constexpr auto kPostInitializationDelayUs = 10 * kOneSecond;

namespace {

void FillIOCallback(CallbackArgs args, IOResult io_result) {
  switch (io_result.status) {
    case IOStatus::kOk:
      break;
    case IOStatus::kFailed:
      throw std::runtime_error("IO failed");
    default:
      throw std::runtime_error("Unknown IOStatus");
  }

  auto *cb_res = static_cast<detail::FillIOResult *>(args);
  [[maybe_unused]] auto successes = cb_res->complete(cb_res->id);
}

void IOCallback(CallbackArgs args, IOResult io_result) {
  auto *ctx = static_cast<detail::IOReqContext *>(args);
  auto *res = &ctx->io_result;
  res->user_cb(res->user_cb_args, io_result);
  ctx->owner->put(ctx);
}

}  // namespace

LoadGenUtils::LoadGenUtils(int n_sectors)
    : read_req_ctxs_(kPerCoreCacheCapacity),
      write_req_ctxs_(kPerCoreCacheCapacity) {
  read_req_ctxs_.reserve(static_cast<size_t>(rt::RuntimeMaxCores()) *
                         kPerCoreCacheCapacity);
  write_req_ctxs_.reserve(static_cast<size_t>(rt::RuntimeMaxCores()) *
                          kPerCoreCacheCapacity);

  vdisk_ = std::make_unique<VirtualDisk>(n_sectors);
  if (!FillVirtualDisk()) {
    throw std::runtime_error("Cannot fill virtual disk");
  }
  /*
  if (!AllocateBlocksInVirtualDisk()) {
    throw std::runtime_error("Cannot allocate virtual disk");
  }
  */
  std::cout << "Virtual disk created, sleeping for "
            << kPostInitializationDelayUs << "us\n";
  rt::Sleep(Duration(kPostInitializationDelayUs));
  std::cout << "Load generator utils prepared!" << '\n';
}

Status<void> LoadGenUtils::AllocateBlocksInVirtualDisk() {
  std::cout << "Allocating " << vdisk_->num_sectors() << " blocks...\n";

  const uint64_t kBatchSize = 4096;
  auto sectors_to_allocate = vdisk_->num_sectors() / kPayloadSectors;
  uint64_t lba = 0;

  while (sectors_to_allocate > 0) {
    const auto batch = std::min(kBatchSize, sectors_to_allocate);
    const auto ret = vdisk_->Allocate(lba, batch);
    if (!ret) {
      throw std::runtime_error("Cannot allocate blocks");
    }
    lba += batch;
    sectors_to_allocate -= batch;
  }

  std::cout << "VirtualDisk allocation complete!\n";

  return {};
}

Status<void> LoadGenUtils::FillVirtualDisk() {
  const auto kBatchSize = 32;
  const auto kReportingIntervalUs = 1 * kOneSecond;
  const auto kReportingInterval = Duration(kReportingIntervalUs);

  const auto sectors_to_write = vdisk_->num_sectors() / kPayloadSectors;
  const auto timeout_us = sectors_to_write * 200;  // 200us per sector

  /* Create a list of payload buffers to write. */
  std::vector<std::vector<std::byte>> payloads(
      kBatchSize, std::vector<std::byte>(kPayloadSize));

  /* Create a queue of indices into the list of payload buffers. Each index
   * corresponds to a payload buffer which is available to read responses
   * into (and the same index corresponds to an IOResult struct from
   * io_res to write the IO response into). */
  std::queue<int> reqs;
  rt::Spin reqs_lock;
  for (int i = 0; i < kBatchSize; i++) {
    reqs.emplace(i);
  }

  /* Waker to use with the request sender thread and IO completion callback. */
  rt::ThreadWaker sender_waker;

  /* Callback when each request is completed successfully. */
  rt::Mutex notify_lock;
  rt::CondVar notify_success;
  std::atomic_int success_counter = 0;
  auto complete = [&reqs_lock, &reqs, &sender_waker, &notify_lock,
                   &success_counter, &notify_success](int i) -> int {
    {
      rt::SpinGuard const reqs_guard(reqs_lock);
      reqs.emplace(i);
      sender_waker.Wake();
    }
    {
      rt::MutexGuard const guard(notify_lock);
      auto success_count_prev = success_counter.fetch_add(1);
      notify_success.Signal();
      return success_count_prev;
    }
  };

  /* Create a list of IO result structs to reuse when sending requests.
   * Note:
   * There is a 1:1 correspondence between each element in io_res and each
   * element in payloads.
   */
  std::vector<detail::FillIOResult> io_res;
  for (int i = 0; i < kBatchSize; i++) {
    detail::FillIOResult io_result{.complete = complete, .id = i};
    io_res.emplace_back(std::move(io_result));
  }

  /* Run this thread until stopped. */
  uint64_t next_sector = 0;
  rt::Thread th_sender([&]() -> void {
    while (true) {
      rt::SpinGuard guard(reqs_lock);

      /* Park until there are more requests to send. */
      guard.Park(sender_waker, [&reqs]() { return !reqs.empty(); });

      if (unlikely(next_sector == sectors_to_write)) {
        /* Park until all outstanding requests have completed. */
        guard.Park(sender_waker,
                   [&reqs]() { return reqs.size() == kBatchSize; });
        break;
      }

      while ((next_sector < sectors_to_write) && !reqs.empty()) {
        /* Find the next payload buffer to read the response into. */
        const auto id = reqs.front();

        /* Create a request descriptor. */
        const IODesc iod{
            .op_flags = static_cast<uint32_t>(OpType::kWrite),
            .num_sectors = kPayloadSectors,
            .start_sector = next_sector++,
            .addr = reinterpret_cast<uint64_t>(payloads[id].data()),
            .callback_args = static_cast<CallbackArgs>(&io_res[id]),
            .callback = FillIOCallback};

        /* Submit the request. */
        auto r = vdisk_->SubmitRequest(iod);
        if (unlikely(!r)) {
          std::cerr << "Cannot submit: " << r.error() << '\n';
          continue;
        }

        /* Mark this payload buffer as in-use. */
        reqs.pop();
      }
    }
  });

  /* Progress reporter thread. */
  bool stop_reporter = false;
  rt::Thread th_reporter([&]() -> void {
    while (!stop_reporter) {
      rt::Sleep(kReportingInterval);
      std::cout << next_sector << "/" << sectors_to_write << " written" << '\n';
    }
  });

  /* Wait for all sectors to be written. */
  bool success = false;
  {
    const rt::MutexGuard guard(notify_lock);
    success = notify_success.WaitFor(notify_lock, timeout_us, [&] {
      return static_cast<uint64_t>(success_counter.load()) == sectors_to_write;
    });
  }

  /* Wait for sender thread to tear-down. */
  th_sender.Join();

  /* Wait for progress reporter thread to tear-down. */
  stop_reporter = true;
  th_reporter.Join();

  if (!success) {
    return MakeError(EINVAL);
  }

  return {};
}

Status<void> LoadGenUtils::SubmitRead(uint64_t sector, Callback cb,
                                      void *cb_args) {
  auto *ctx = read_req_ctxs_.get();

  ctx->io_result.user_cb = cb;
  ctx->io_result.user_cb_args = cb_args;
  ctx->owner = &read_req_ctxs_;

  /* Create a read request descriptor. */
  const IODesc iod{
      .op_flags = static_cast<uint32_t>(OpType::kRead),
      .num_sectors = kPayloadSectors,
      .start_sector = sector,
      .addr = reinterpret_cast<uint64_t>(ctx->payloads.get()),
      .callback_args = static_cast<CallbackArgs>(ctx),
      .callback = IOCallback,
  };

  /* Submit the request. */
  return vdisk_->SubmitRequest(iod);
}

Status<void> LoadGenUtils::SubmitWrite(uint64_t sector, Callback cb,
                                       void *cb_args) {
  auto *ctx = write_req_ctxs_.get();

  ctx->io_result.user_cb = cb;
  ctx->io_result.user_cb_args = cb_args;
  ctx->owner = &write_req_ctxs_;

  /* Create a write request descriptor. */
  const IODesc iod{
      .op_flags = static_cast<uint32_t>(OpType::kWrite),
      .num_sectors = kPayloadSectors,
      .start_sector = sector,
      .addr = reinterpret_cast<uint64_t>(ctx->payloads.get()),
      .callback_args = static_cast<CallbackArgs>(ctx),
      .callback = IOCallback,
  };

  /* Submit the request. */
  return vdisk_->SubmitRequest(iod);
}

}  // namespace sandook
