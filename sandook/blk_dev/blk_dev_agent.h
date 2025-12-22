#pragma once

#include <memory>
#include <queue>
#include <vector>

#include "sandook/base/error.h"
#include "sandook/base/queue_info.h"
#include "sandook/blk_dev/request_dispatcher.h"
#include "ublksrv.h"
#include "ublksrv_aio.h"

namespace sandook {

class BlkDevAgent {
 public:
  int Run(const std::string queue_to_core_mapping);

  [[nodiscard]] inline const struct ublksrv_dev *get_dev() const {
    return dev_;
  }
  [[nodiscard]] inline struct ublksrv_ctrl_dev *get_ctrl_dev() const {
    return ctrl_dev_;
  }
  [[nodiscard]] struct ublksrv_aio_ctx *get_queue_aio_ctx(int i) const {
    return queues_[i].aio_ctx;
  }

  void set_dev(struct ublksrv_dev *dev) { dev_ = dev; }
  void set_ctrl_dev(struct ublksrv_ctrl_dev *ctrl_dev) { ctrl_dev_ = ctrl_dev; }

  static void ProcessBlockDevRequests(struct queue_info *qinfo, int qcore);
  static int HandleIOAsync(const struct ublksrv_queue *q,
                           const struct ublk_io_data *data);
  static void HandleEvent(const struct ublksrv_queue *q);
  static int InitTarget(struct ublksrv_dev *dev, int type, int argc,
                        char *argv[]);

  BlkDevAgent();
  ~BlkDevAgent();

  BlkDevAgent(const BlkDevAgent &) = delete;
  BlkDevAgent &operator=(const BlkDevAgent &) = delete;

  static BlkDevAgent *GetInstance() {
    static BlkDevAgent instance{};
    return &instance;
  }

  RequestDispatcher *dispatcher() { return dispatcher_.get(); }

 private:
  bool ctrl_dev_initialized_{false};
  bool dev_initialized_{false};
  bool ctrl_dev_added_{false};

  struct ublksrv_tgt_type tgt_type_;
  struct ublksrv_dev_data dev_data_;
  struct ublksrv_ctrl_dev *ctrl_dev_;
  const struct ublksrv_dev *dev_;

  std::vector<struct queue_info> queues_;
  std::unique_ptr<RequestDispatcher> dispatcher_;
  std::map<unsigned int, unsigned int> queue_to_core_;

  Status<void> SetQueueToCoreMapping(const std::string queue_to_core_mapping);
  Status<unsigned int> GetCoreForQueue(unsigned int q);
};

}  // namespace sandook
