#pragma once

#include <deque>
#include <memory>

#include "sandook/base/queue_info.h"
#include "sandook/bindings/sync.h"
#include "sandook/virtual_disk/virtual_disk.h"
#include "ublksrv.h"
#include "ublksrv_aio.h"

namespace sandook {

constexpr static int kNumEpollEvents = 1;

class RequestDispatcher {
 public:
  explicit RequestDispatcher(int nsectors);
  ~RequestDispatcher() = default;

  /* No copying. */
  RequestDispatcher(const RequestDispatcher &) = delete;
  RequestDispatcher &operator=(const RequestDispatcher &) = delete;

  /* No moving. */
  RequestDispatcher(RequestDispatcher &&) noexcept;
  RequestDispatcher &operator=(RequestDispatcher &&) noexcept;

  int SubmitRequest(const struct ublksrv_queue *q,
                    const struct ublk_io_data *data);

 private:
  std::unique_ptr<VirtualDisk> vdisk_;
};

struct CompletionReq {
  const ublksrv_queue *q;
  unsigned int tag;
  int res;
};

class alignas(kCacheLineSizeBytes) CompletionQueue {
 public:
  void producer_push(CompletionReq req);
  std::deque<CompletionReq> &consumer_acquire();
  bool is_empty();

 private:
  rt::Spin spin_;
  std::deque<CompletionReq> active_q_;
  std::deque<CompletionReq> inactive_q_;
};

extern CompletionQueue completion_queues[kMaxNumCores][kMaxNumCores];

}  // namespace sandook
