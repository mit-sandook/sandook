extern "C" {
#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "sandook/base/error.h"
#include "sandook/base/queue_info.h"
#include "sandook/bindings/runtime.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/blk_dev/blk_dev_agent.h"
#include "sandook/blk_dev/request_dispatcher.h"
#include "ublksrv.h"
#include "ublksrv_aio.h"
#include "ublksrv_utils.h"

constexpr auto kLogIntervalUs = 1 * sandook::kOneSecond;

namespace sandook {

namespace {

constexpr static auto kUBLKSRVTargetType = 0;
constexpr static auto kDevSizeSectors = (1ULL << 27);
constexpr static auto kDevSizeBytes = kDevSizeSectors << kSectorShift;
constexpr static auto kDevNumHWQueues = 6;
constexpr static auto kDevQueueDepth = 256;
constexpr static auto kDevBufSize = 32 << 20;

}  // namespace

static char ublksrv_jbuf_[4096];
static rt::Mutex ublksrv_jbuf_mutex_;
static rt::Spin ublksrv_lock_;

Status<unsigned int> BlkDevAgent::GetCoreForQueue(unsigned int q) {
  const auto it = queue_to_core_.find(q);
  if (it == queue_to_core_.end()) {
    return MakeError(ENOENT);
  }
  return it->second;
}

static unsigned int GetCurrentCore() {
  unsigned int cpu;
  unsigned int node;
  int err = getcpu(&cpu, &node);
  if (err) {
    std::cerr << "getcpu failed: " << err << '\n';
  }
  return cpu;
}

static bool CheckCorePinned(unsigned int cpu) {
  return cpu != GetCurrentCore();
}

static void LogCoreAffinity(unsigned int cpu) {
  cpu_set_t cpuset;
  pthread_t thread = pthread_self();
  int err = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (err != 0) {
    rt::SpinGuard guard(ublksrv_lock_);
    std::cerr << "pthread_getaffinity_np failed: " << err << '\n';
    return;
  }

  std::stringstream ss;
  ss << "CPU: " << cpu << " = ";
  for (int j = 0; j < CPU_SETSIZE; j++) {
    if (CPU_ISSET(j, &cpuset)) {
      ss << j << ", ";
    }
  }
  ss << '\n';
}

static void CheckCoreMoved(unsigned int prev_cpu, unsigned int cur_cpu,
                           unsigned short q_id) {
  if (cur_cpu != prev_cpu) {
    {
      rt::SpinGuard guard(ublksrv_lock_);
      std::cout << "Core moved from: " << prev_cpu << " to " << cur_cpu
                << " for queue " << q_id << '\n';
    }
    LogCoreAffinity(prev_cpu);
    LogCoreAffinity(cur_cpu);
  }
}

void SignalHandler(int sig) {
  BlkDevAgent *sa = BlkDevAgent::GetInstance();
  ublksrv_ctrl_stop_dev(sa->get_ctrl_dev());
}

BlkDevAgent::BlkDevAgent() {
  /* UBLK device parameters. */
  tgt_type_.type = kUBLKSRVTargetType;
  tgt_type_.name = "sandook";
  tgt_type_.init_tgt = BlkDevAgent::InitTarget;
  tgt_type_.handle_io_async = BlkDevAgent::HandleIOAsync;

  dev_data_.dev_id = -1;
  dev_data_.max_io_buf_bytes = kDevBufSize;
  dev_data_.nr_hw_queues = kDevNumHWQueues;
  dev_data_.queue_depth = kDevQueueDepth;
  dev_data_.tgt_type = "sandook";
  dev_data_.tgt_ops = &tgt_type_;
  dev_data_.flags = 0;

  /* Initialize data structure for keeping track of UBLK device queues. */
  queues_.resize(dev_data_.nr_hw_queues);

  /* Setup signal handlers. */
  if (std::signal(SIGTERM, SignalHandler) == SIG_ERR) {
    throw std::runtime_error("Failed to install signal handler");
  }
  if (std::signal(SIGINT, SignalHandler) == SIG_ERR) {
    throw std::runtime_error("Failed to install signal handler");
  }

  /* Initialize a UBLK control device. */
  struct ublksrv_ctrl_dev *ctrl_dev = ublksrv_ctrl_init(&dev_data_);
  if (!ctrl_dev) {
    throw std::runtime_error("Failed to initialize ublksrv ctrl device");
  }
  ctrl_dev_ = ctrl_dev;
  ctrl_dev_initialized_ = true;

  /* Add a UBLK device using the control device. */
  int ret = ublksrv_ctrl_add_dev(ctrl_dev_);
  if (ret < 0) {
    ublksrv_ctrl_deinit(ctrl_dev_);
    throw std::runtime_error("Cannot add device");
  }
  ctrl_dev_added_ = true;
  std::cout << "Device added using ctrl device" << std::endl;
}

BlkDevAgent::~BlkDevAgent() {
  /* Clean up UBLK devices. */
  if (dev_initialized_) ublksrv_dev_deinit(dev_);
  if (ctrl_dev_added_) ublksrv_ctrl_del_dev(ctrl_dev_);
  if (ctrl_dev_initialized_) ublksrv_ctrl_deinit(ctrl_dev_);
}

void BlkDevAgent::ProcessBlockDevRequests(struct queue_info *qinfo, int qcore) {
  const struct ublksrv_dev *dev = qinfo->dev;
  const struct ublksrv_ctrl_dev_info *dinfo =
      ublksrv_ctrl_get_dev_info(ublksrv_get_ctrl_dev(dev));
  unsigned dev_id = dinfo->dev_id;
  unsigned short q_id = qinfo->qid;

  {
    rt::MutexGuard guard(ublksrv_jbuf_mutex_);
    ublksrv_json_write_queue_info(ublksrv_get_ctrl_dev(dev), ublksrv_jbuf_,
                                  sizeof ublksrv_jbuf_, q_id, ublksrv_gettid());
  }

  {
    rt::Preempt p;
    rt::PreemptGuard g(p);

    const struct ublksrv_queue *q = ublksrv_queue_init(dev, q_id, qinfo);
    if (!q) {
      std::cerr << "Failed to initialize queue" << std::endl;
      return;
    }
    qinfo->q = q;

    std::cout << "Device queue started: "
              << "TID = " << ublksrv_gettid() << ", Device ID = " << dev_id
              << ", Queue ID = " << q_id << std::endl;

    int ret = 0;

    if (qcore >= 0) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(static_cast<unsigned int>(qcore), &cpuset);
      pthread_t thread = pthread_self();
      ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
      if (ret) {
        std::cerr << "Cannot set CPU affinity for queue: " << q_id << '\n';
        return;
      }
      std::cout << "Pinned queue " << q_id << " to core " << qcore << '\n';
    }

    uint64_t counter = 0;
    auto start_time = MicroTime();

    uint64_t last_submit_tsc = 0;

    do {
      for (unsigned int core = 0; core < rt::RuntimeMaxCores(); ++core) {
        auto &c_q = completion_queues[core][q_id];
        if (c_q.is_empty()) {
          continue;
        }
        auto &q = c_q.consumer_acquire();
        while (!q.empty()) {
          auto &req = q.front();
          ublksrv_complete_io(req.q, req.tag, req.res);
          q.pop_front();
        }
      }

      auto now_tsc = rdtsc();
      if (now_tsc < last_submit_tsc + 2397 * 10) {
        continue;
      }
      last_submit_tsc = now_tsc;

      // if (q_id == 1)    std::cout << last_submit_tsc << std::endl;
      // const auto prev_cpu = GetCurrentCore();
      ret = ublksrv_process_io(q);
      // const auto cur_cpu = GetCurrentCore();
      // CheckCoreMoved(prev_cpu, cur_cpu, q_id);

      if (ret < 0) {
        std::cerr << "Failed to process ublksrv IO" << std::endl;
        break;
      }

      counter += ret;

      if (ret > 0) {
        const auto elapsed = MicroTime() - start_time;
        if (elapsed > kLogIntervalUs) {
          {
            // rt::SpinGuard guard(ublksrv_lock_);
            std::cout << "Queue: " << q_id << " = " << 1.0 * counter / elapsed
                      << '\n';
          }
          counter = 0;
          start_time = MicroTime();
        }
      }
    } while (true);

    std::cout << "Device queue exited: "
              << "Device ID = " << dev_id << ", Queue ID = " << q->q_id
              << std::endl;

    ublksrv_queue_deinit(q);
  }

  return;
}

std::vector<std::string> split(std::string s, std::string delimiter) {
  size_t pos_start = 0, pos_end, delim_len = delimiter.length();
  std::string token;
  std::vector<std::string> res;

  while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
    token = s.substr(pos_start, pos_end - pos_start);
    pos_start = pos_end + delim_len;
    res.push_back(token);
  }

  res.push_back(s.substr(pos_start));
  return res;
}

Status<void> BlkDevAgent::SetQueueToCoreMapping(
    const std::string queue_to_core_mapping) {
  const auto mappings = split(queue_to_core_mapping, ",");
  if (mappings.empty()) {
    return MakeError(ENOENT);
  }

  for (const auto &mapping : mappings) {
    unsigned int q, c;
    std::stringstream ss(mapping);
    /* First item is queue. */
    ss >> q;
    /* Ignore the colon. */
    ss.ignore();
    /* Second item is core. */
    ss >> c;
    /* Add to mapping. */
    queue_to_core_[q] = c;
  }

  return {};
}

int BlkDevAgent::Run(const std::string queue_to_core_mapping) {
  if (!queue_to_core_mapping.empty()) {
    const auto q2c = SetQueueToCoreMapping(queue_to_core_mapping);
    if (!q2c) {
      return -EINVAL;
    }
  }

  if (ublksrv_ctrl_get_affinity(ctrl_dev_) < 0) return -1;

  /* Get the device ID. */
  const struct ublksrv_ctrl_dev_info *dinfo =
      ublksrv_ctrl_get_dev_info(ctrl_dev_);

  /* Initialize a UBLK sd device. */
  const struct ublksrv_dev *dev = ublksrv_dev_init(ctrl_dev_);
  if (!dev) {
    std::cerr << "Failed to initialize device" << std::endl;
    return -ENOMEM;
  }
  dev_ = dev;
  dev_initialized_ = true;
  std::cout << "Ctrl device initialized" << std::endl;

  /* Initialize the dispatcher. */
  dispatcher_ = std::make_unique<RequestDispatcher>(kDevSizeSectors);

  /* Setup each queue handler. */
  for (int i = 0; i < dinfo->nr_hw_queues; i++) {
    queues_[i].dev = dev_;
    queues_[i].qid = i;

    /* Start thread to handle interactions with the block device exposed from
     * the kernel.
     */
    const auto qcore_ret = GetCoreForQueue(i);
    int qcore = -1;
    if (qcore_ret) {
      qcore = *qcore_ret;
    } else {
      std::cerr << "No core specified to pin queue on: " << i << '\n';
    }

    queues_[i].blk_dev_thread = rt::Thread(
        [q = &queues_[i], qcore]() { ProcessBlockDevRequests(q, qcore); });
  }
  std::cout << "Launched per-queue threads" << std::endl;

  struct ublk_params params = {
      .types = UBLK_PARAM_TYPE_BASIC,
      .basic = {.logical_bs_shift = kSectorShift,
                .physical_bs_shift = kSectorShift,
                .io_opt_shift = kSectorShift,
                .io_min_shift = kSectorShift,
                .max_sectors = dinfo->max_io_buf_bytes >> kLinuxSectorShift,
                .dev_sectors = dev_->tgt.dev_size >> kLinuxSectorShift},
  };

  {
    rt::MutexGuard guard(ublksrv_jbuf_mutex_);
    ublksrv_json_write_params(&params, ublksrv_jbuf_, sizeof ublksrv_jbuf_);
  }

  int ret = ublksrv_ctrl_set_params(ctrl_dev_, &params);
  if (ret) {
    std::cerr << "Cannot set parameters for device: " << dinfo->dev_id
              << " (Error = " << ret << ")" << std::endl;
    return ret;
  }

  /* Everything is set up, start the device. */
  ret = ublksrv_ctrl_start_dev(ctrl_dev_, getpid());
  if (ret < 0) {
    std::cerr << "Failed to start device: " << std::strerror(ret) << std::endl;
    return ret;
  }
  std::cout << "Device started" << std::endl;

  {
    /* Dump the device info. */
    rt::MutexGuard guard(ublksrv_jbuf_mutex_);
    ublksrv_ctrl_get_info(ctrl_dev_);
    ublksrv_ctrl_dump(ctrl_dev_, ublksrv_jbuf_);
  }

  /* Wait until we are terminated. */
  for (int i = 0; i < dinfo->nr_hw_queues; i++) {
    queues_[i].blk_dev_thread.Join();
  }

  return ret;
}

int BlkDevAgent::InitTarget(struct ublksrv_dev *dev, int type, int argc,
                            char *argv[]) {
  if (type != kUBLKSRVTargetType) {
    std::cerr << "Unkown target type: " << type << std::endl;
    return -1;
  }

  struct ublksrv_tgt_info *tgt = &dev->tgt;
  tgt->dev_size = kDevSizeBytes;

  const struct ublksrv_ctrl_dev_info *info =
      ublksrv_ctrl_get_dev_info(ublksrv_get_ctrl_dev(dev));
  struct ublksrv_tgt_base_json tgt_json = {
      .name = "sandook",
      .type = type,
      .dev_size = tgt->dev_size,
  };
  tgt->tgt_ring_depth = info->queue_depth;
  tgt->nr_fds = 0;

  {
    rt::MutexGuard guard(ublksrv_jbuf_mutex_);
    ublksrv_json_write_dev_info(ublksrv_get_ctrl_dev(dev), ublksrv_jbuf_,
                                sizeof ublksrv_jbuf_);
    ublksrv_json_write_target_base_info(ublksrv_jbuf_, sizeof ublksrv_jbuf_,
                                        &tgt_json);
  }

  return 0;
}

int BlkDevAgent::HandleIOAsync(const struct ublksrv_queue *q,
                               const struct ublk_io_data *data) {
  BlkDevAgent *agent = BlkDevAgent::GetInstance();
  return agent->dispatcher()->SubmitRequest(q, data);
  /*
  int res = data->iod->nr_sectors << kLinuxSectorShift;
  ublksrv_complete_io(q, data->tag, res);
  return 0;
  */
}

}  // namespace sandook
