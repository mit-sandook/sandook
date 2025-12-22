#pragma once

#include "sandook/bindings/thread.h"
#include "ublksrv.h"
#include "ublksrv_aio.h"

namespace sandook {

struct queue_info {
  /* UBLK related handles. */
  struct ublksrv_aio_ctx *aio_ctx;
  const struct ublksrv_dev *dev;
  const struct ublksrv_queue *q;

  /* Queue ID. */
  int qid;

  /* Threads for handling interaction with the storage layer and the UBLK block
   * device exposed to the kernel. */
  rt::Thread storage_thread;
  rt::Thread blk_dev_thread;
};

}  // namespace sandook
