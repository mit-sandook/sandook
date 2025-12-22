#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <span>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/runtime.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"

namespace sandook {

class VirtualDiskBase {
 public:
  virtual ~VirtualDiskBase() { Stop(); }

  /* No copying. */
  VirtualDiskBase(const VirtualDiskBase &) = delete;
  VirtualDiskBase &operator=(const VirtualDiskBase &) = delete;

  /* No moving. */
  VirtualDiskBase(VirtualDiskBase &&) noexcept;
  VirtualDiskBase &operator=(VirtualDiskBase &&) noexcept;

  /* Submit an IO request for asynchronous completion. */
  Status<void> SubmitRequest(IODesc iod);

  /* Spawn a thread to process the request. */
  Status<void> ProcessRequestAsync(IODesc iod);

  /* Perform a block allocation. */
  Status<void> Allocate(uint64_t sector, uint32_t n_sectors);

  /* Perform a read request (synchronous). */
  Status<int> Read(uint64_t sector, std::span<std::byte> buf);

  /* Perform a write request (synchronous). */
  Status<int> Write(uint64_t sector, std::span<const std::byte> buf);

  /* Get the number of sectors in this virtual disk. */
  [[nodiscard]] uint64_t num_sectors() const { return n_sectors_; }

  /* Get the number of disk server blocks garbage collected. */
  [[nodiscard]] uint64_t num_gc_blocks() const { return n_disk_blocks_gc_; }

 protected:
  explicit VirtualDiskBase(uint64_t n_sectors) : n_sectors_(n_sectors) {
    for (uint64_t i = 0; i < rt::RuntimeMaxCores(); i++) {
      // NOLINTBEGIN
      work_queue_ths_.at(i).th =
          rt::Thread([this, work_queue_th = &work_queue_ths_.at(i)] {
            RequestWorker(work_queue_th);
          });
      // NOLINTEND
    }
  }

  /* Process a given IO request. */
  virtual Status<int> ProcessRequest(IODesc iod) = 0;

  void inc_num_gc_blocks(size_t delta) { n_disk_blocks_gc_ += delta; }

 private:
  // TODO(zainryan): Wrap code logic into a neat class abstraction.
  struct alignas(kCacheLineSizeBytes) WorkQueueThread {
    rt::Thread th;
    rt::ThreadWaker waker;
    std::queue<IODesc> reqs;
    rt::Spin lock;
  };

  /* Thread to process IO requests. */
  std::array<WorkQueueThread, kMaxNumCores> work_queue_ths_;

  /* Indicate if IO threads need to stop. */
  bool stop_{false};

  /* Number of sectors in this virtual disk. */
  uint64_t n_sectors_{0};

  /* Number of disk server blocks (NOT virtual disk) garbage collected. */
  uint64_t n_disk_blocks_gc_{0};

  /* Submit an IO request into the queue, and optionally park the calling
   * thread if waker is not nullptr. */
  void SubmitRequestAndPark(IODesc iod, rt::ThreadWaker *waker);

  /* Event loop for processing IO requests. */
  void RequestWorker(WorkQueueThread *work_queue_th);

  /* Shard a request into individual sectors and process them all. */
  IOResult ProcessShardedRequests(IODesc iod);

  /* Invoke callback on process completion. */
  static void ProcessCompletion(IODesc iod, IOResult io_result);

  /* Process IO request failure. */
  static void ProcessFailure(IODesc iod, int err);

  /* Event loop for processing IO completion events. */
  void CompletionsWorker();

  /* Stop all worker threads processing IO requests and completions. */
  void Stop();
};

}  // namespace sandook
