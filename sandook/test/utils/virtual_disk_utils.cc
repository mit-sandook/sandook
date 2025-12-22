#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/time.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/virtual_disk/virtual_disk.h"

using StatsUpdater = std::function<int(int, bool)>;

namespace {

struct IOResultInternal {
  StatsUpdater updater;
  int id{};
};

void IOCallback(sandook::CallbackArgs args, sandook::IOResult io_result) {
  bool success = true;

  switch (io_result.status) {
    case sandook::IOStatus::kOk:
      break;

    case sandook::IOStatus::kFailed:
      std::cerr << "IO failed: " << sandook::MakeError(io_result.res).error()
                << '\n';
      success = false;
      break;

    default:
      throw std::runtime_error("Unknown IOStatus");
      break;
  }

  auto *cb_res = static_cast<IOResultInternal *>(args);
  [[maybe_unused]] auto completions = cb_res->updater(cb_res->id, success);
}

}  // namespace

bool AllocateBlocksInVirtualDisk(sandook::VirtualDisk *vdisk,
                                 const uint64_t payload_size_sectors) {
  std::cout << "VirtualDisk allocating " << vdisk->num_sectors()
            << " blocks...\n";

  const uint64_t kBatchSize = 4096;
  auto sectors_to_allocate = vdisk->num_sectors() / payload_size_sectors;
  uint64_t lba = 0;

  while (sectors_to_allocate > 0) {
    const auto batch = std::min(kBatchSize, sectors_to_allocate);
    const auto ret = vdisk->Allocate(lba, batch);
    if (!ret) {
      return false;
    }
    lba += batch;
    sectors_to_allocate -= batch;
  }

  std::cout << "VirtualDisk allocation complete!\n";
  return true;
}

bool FillVirtualDisk(sandook::VirtualDisk *vdisk, int payload_size_bytes) {
  const auto kBatchSize = 32;
  const auto kReportingIntervalUs = 1 * 1000 * 1000;  // 2s
  const auto kReportingInterval = sandook::Duration(kReportingIntervalUs);

  const uint32_t payload_sectors = payload_size_bytes >> sandook::kSectorShift;
  const auto sectors_to_write = vdisk->num_sectors() / payload_sectors;
  const auto timeout_us = sectors_to_write * 200;  // 200us per sector

  /* Create a list of payload buffers to write. */
  std::vector<std::vector<std::byte>> payloads(
      kBatchSize, std::vector<std::byte>(payload_size_bytes));

  /* Fill the write buffers with random bytes. */
  std::random_device rand_payload;
  for (auto &payload : payloads) {
    for (auto &i : payload) {
      i = static_cast<std::byte>(rand_payload());
    }
  }

  /* Create a queue of indices into the list of payload buffers. Each index
   * corresponds to a payload buffer which is available to read responses
   * into (and the same index corresponds to an IOResultInternal struct from
   * io_res to write the IO response into). */
  std::queue<int> reqs;
  sandook::rt::Spin reqs_lock;
  for (int i = 0; i < kBatchSize; i++) {
    reqs.emplace(i);
  }

  /* Waker to use with the request sender thread and IO completion callback. */
  sandook::rt::ThreadWaker sender_waker;

  /* Callback when each request is completed successfully. */
  sandook::rt::Mutex notify_lock;
  sandook::rt::CondVar notify_completion;
  std::atomic_int completion_counter = 0;
  std::atomic_int failure_counter = 0;
  auto updater = [&reqs_lock, &reqs, &sender_waker, &notify_lock,
                  &failure_counter, &completion_counter,
                  &notify_completion](int i, bool success) {
    {
      sandook::rt::SpinGuard const reqs_guard(reqs_lock);
      reqs.emplace(i);
      sender_waker.Wake();
    }
    {
      sandook::rt::MutexGuard const guard(notify_lock);
      if (!success) {
        failure_counter++;
      }
      auto completion_count_prev = completion_counter.fetch_add(1);
      notify_completion.Signal();
      return completion_count_prev;
    }
  };

  /* Create a list of IO result structs to reuse when sending requests.
   * Note:
   * There is a 1:1 correspondence between each element in io_res and each
   * element in payloads.
   */
  std::vector<IOResultInternal> io_res;
  for (int i = 0; i < kBatchSize; i++) {
    IOResultInternal io_result{.updater = updater, .id = i};
    io_res.emplace_back(std::move(io_result));
  }

  /* Run this thread until stopped. */
  uint64_t next_sector = 0;
  sandook::rt::Thread th_sender([&]() -> void {
    while (true) {
      sandook::rt::SpinGuard guard(reqs_lock);

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
        const sandook::IODesc iod{
            .op_flags = static_cast<uint32_t>(sandook::OpType::kWrite),
            .num_sectors = payload_sectors,
            .start_sector = next_sector++,
            .addr = reinterpret_cast<uint64_t>(payloads[id].data()),
            .callback_args = static_cast<sandook::CallbackArgs>(&io_res[id]),
            .callback = IOCallback};

        /* Submit the request. */
        auto r = vdisk->SubmitRequest(iod);
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
  sandook::rt::Thread th_reporter([&]() -> void {
    while (!stop_reporter) {
      sandook::rt::Sleep(kReportingInterval);
      std::cout << next_sector << "/" << sectors_to_write << " written" << '\n';
    }
  });

  /* Wait for all sectors to be written. */
  bool completion = false;
  {
    const sandook::rt::MutexGuard guard(notify_lock);
    completion = notify_completion.WaitFor(notify_lock, timeout_us, [&] {
      return static_cast<uint64_t>(completion_counter.load()) >=
             sectors_to_write;
    });
  }

  /* Wait for sender thread to tear-down. */
  th_sender.Join();

  /* Wait for progress reporter thread to tear-down. */
  stop_reporter = true;
  th_reporter.Join();

  return completion;
}

bool RandReadsTask(sandook::VirtualDisk *vdisk, sandook::Duration task_duration,
                   int max_num_inflight, int payload_size_bytes) {
  std::cout << "Task starting..." << '\n';

  /* Create a list of payload buffers to read responses. */
  std::vector<std::vector<std::byte>> payloads(
      max_num_inflight, std::vector<std::byte>(payload_size_bytes));

  /* Create a queue of indices into the list of payload buffers. Each index
   * corresponds to a payload buffer which is available to read responses
   * into (and the same index corresponds to an IOResultInternal struct from
   * io_res to write the IO response into). */
  std::queue<int> reqs;
  sandook::rt::Spin reqs_lock;
  for (int i = 0; i < max_num_inflight; i++) {
    reqs.emplace(i);
  }

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, vdisk->num_sectors() - 1);

  /* Waker to use with the request sender thread and IO completion callback. */
  sandook::rt::ThreadWaker sender_waker;

  /* Callback when each request is completed successfully. */
  std::atomic_int completion_counter = 0;
  std::atomic_int failure_counter = 0;
  auto updater = [&](int i, bool success) {
    {
      sandook::rt::SpinGuard const reqs_guard(reqs_lock);
      reqs.emplace(i);
      sender_waker.Wake();
      if (!success) {
        failure_counter++;
      }
      return completion_counter.fetch_add(1);
    }
  };

  /* Create a list of IO result structs to reuse when sending requests.
   * Note:
   * There is a 1:1 correspondence between each element in io_res and each
   * element in payloads.
   */
  std::vector<IOResultInternal> io_res;
  for (int i = 0; i < max_num_inflight; i++) {
    IOResultInternal io_result{.updater = updater, .id = i};
    io_res.emplace_back(std::move(io_result));
  }

  /* Run this thread until stopped. */
  bool stop_sender = false;
  int total_sent = 0;
  sandook::rt::Thread th_sender([&]() -> void {
    std::cout << "Task started..." << '\n';

    const uint32_t payload_sectors =
        payload_size_bytes >> sandook::kSectorShift;

    while (true) {
      sandook::rt::SpinGuard guard(reqs_lock);

      /* Park until there are more requests to send. */
      guard.Park(sender_waker, [&stop_sender, &reqs]() {
        return stop_sender || !reqs.empty();
      });

      if (unlikely(stop_sender)) {
        /* Park until all outstanding requests have completed. */
        guard.Park(sender_waker, [&reqs, &max_num_inflight]() {
          return reqs.size() == static_cast<size_t>(max_num_inflight);
        });
        break;
      }

      while (!reqs.empty()) {
        /* Find the next payload buffer to read the response into. */
        const auto id = reqs.front();

        /* Create a request descriptor. */
        const sandook::IODesc iod{
            .op_flags = static_cast<uint32_t>(sandook::OpType::kRead),
            .num_sectors = payload_sectors,
            .start_sector = dist(rng),
            .addr = reinterpret_cast<uint64_t>(payloads[id].data()),
            .callback_args = static_cast<sandook::CallbackArgs>(&io_res[id]),
            .callback = IOCallback};

        /* Submit the request. */
        auto r = vdisk->SubmitRequest(iod);
        if (unlikely(!r)) {
          std::cerr << "Cannot submit: " << r.error() << '\n';
          continue;
        }

        /* Mark this payload buffer as in-use. */
        reqs.pop();
        total_sent++;
      }
    }
  });

  /* Stop the sender after a set duration. */
  sandook::rt::Thread th_stopper([&]() {
    /* Sleep for the duration we want to send requests. */
    sandook::rt::Sleep(task_duration);

    std::cout << "Task complete, waiting for outstanding reqs..." << '\n';

    const auto completed = completion_counter.load();
    std::cout << completed / task_duration.Seconds() << " RPS" << '\n';
    const auto failed = failure_counter.load();
    if (failed > 0) {
      std::cout << failed << "/" << completed << " failed" << '\n';
    }

    /* Wake up and signal the sender to stop. */
    {
      sandook::rt::SpinGuard const guard(reqs_lock);
      stop_sender = true;
      sender_waker.Wake();
    }
  });

  /* Wait for the sender to tear down. */
  th_sender.Join();

  /* Wait for completion. */
  th_stopper.Join();

  return completion_counter.load() == total_sent;
}

bool RandWritesTask(sandook::VirtualDisk *vdisk,
                    sandook::Duration task_duration, int max_num_inflight,
                    int payload_size_bytes) {
  std::cout << "Task starting..." << '\n';

  /* Create a list of payload buffers to write. */
  std::vector<std::vector<std::byte>> payloads(
      max_num_inflight, std::vector<std::byte>(payload_size_bytes));

  /* Fill the write buffers with random bytes. */
  std::random_device rand_payload;
  for (auto &payload : payloads) {
    for (auto &i : payload) {
      i = static_cast<std::byte>(rand_payload());
    }
  }

  /* Create a queue of indices into the list of payload buffers. Each index
   * corresponds to a payload buffer which is available to read responses
   * into (and the same index corresponds to an IOResultInternal struct from
   * io_res to write the IO response into). */
  std::queue<int> reqs;
  sandook::rt::Spin reqs_lock;
  for (int i = 0; i < max_num_inflight; i++) {
    reqs.emplace(i);
  }

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(static_cast<int>(rand_sectors()));
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, vdisk->num_sectors() - 1);

  /* Waker to use with the request sender thread and IO completion callback. */
  sandook::rt::ThreadWaker sender_waker;

  /* Callback when each request is completed successfully. */
  std::atomic_int completion_counter = 0;
  std::atomic_int failure_counter = 0;
  auto updater = [&](int i, bool success) {
    sandook::rt::SpinGuard const reqs_guard(reqs_lock);
    reqs.emplace(i);
    sender_waker.Wake();
    if (!success) {
      failure_counter++;
    }
    return completion_counter.fetch_add(1);
  };

  /* Create a list of IO result structs to reuse when sending requests.
   * Note:
   * There is a 1:1 correspondence between each element in io_res and each
   * element in payloads.
   */
  std::vector<IOResultInternal> io_res;
  for (int i = 0; i < max_num_inflight; i++) {
    IOResultInternal io_result{.updater = updater, .id = i};
    io_res.emplace_back(std::move(io_result));
  }

  /* Run this thread until stopped. */
  bool stop_sender = false;
  int total_sent = 0;
  sandook::rt::Thread th_sender([&]() -> void {
    std::cout << "Task started..." << '\n';

    const uint32_t payload_sectors =
        payload_size_bytes >> sandook::kSectorShift;

    while (true) {
      sandook::rt::SpinGuard guard(reqs_lock);

      /* Park until there are more requests to send. */
      guard.Park(sender_waker, [&stop_sender, &reqs]() {
        return stop_sender || !reqs.empty();
      });

      if (unlikely(stop_sender)) {
        /* Park until all outstanding requests have completed. */
        guard.Park(sender_waker, [&reqs, &max_num_inflight]() {
          return reqs.size() == static_cast<size_t>(max_num_inflight);
        });
        break;
      }

      while (!reqs.empty()) {
        /* Find the next payload buffer to read the response into. */
        const auto id = reqs.front();

        /* Create a request descriptor. */
        const sandook::IODesc iod{
            .op_flags = static_cast<uint32_t>(sandook::OpType::kWrite),
            .num_sectors = payload_sectors,
            .start_sector = dist(rng),
            .addr = reinterpret_cast<uint64_t>(payloads[id].data()),
            .callback_args = static_cast<sandook::CallbackArgs>(&io_res[id]),
            .callback = IOCallback};

        /* Submit the request. */
        auto r = vdisk->SubmitRequest(iod);
        if (unlikely(!r)) {
          std::cerr << "Cannot submit: " << r.error() << '\n';
          continue;
        }

        /* Mark this payload buffer as in-use. */
        reqs.pop();
        total_sent++;
      }
    }
  });

  /* Stop the sender after a set duration. */
  sandook::rt::Thread th_stopper([&]() {
    /* Sleep for the duration we want to send requests. */
    sandook::rt::Sleep(task_duration);

    std::cout << "Task complete, waiting for outstanding reqs..." << '\n';

    const auto completed = completion_counter.load();
    std::cout << completed / task_duration.Seconds() << " RPS" << '\n';
    const auto failed = failure_counter.load();
    if (failed > 0) {
      std::cout << failed << "/" << completed << " failed" << '\n';
    }

    /* Wake up and signal the sender to stop. */
    {
      sandook::rt::SpinGuard const guard(reqs_lock);
      stop_sender = true;
      sender_waker.Wake();
    }
  });

  /* Wait for the sender to tear down. */
  th_sender.Join();

  /* Wait for completion. */
  th_stopper.Join();

  return completion_counter.load() == total_sent;
}

bool RandReadsWritesTask(sandook::VirtualDisk *vdisk,
                         sandook::Duration task_duration, int max_num_inflight,
                         int payload_size_bytes, double read_ratio) {
  std::cout << "Task starting..." << '\n';

  if (read_ratio < 0 || read_ratio > 1) {
    throw std::runtime_error("Invalid read_ratio. Must be between 0 and 1.");
  }

  /* Create a list of payload buffers to write. */
  std::vector<std::vector<std::byte>> write_payloads(
      max_num_inflight, std::vector<std::byte>(payload_size_bytes));

  /* Fill the write buffers with random bytes. */
  std::random_device rand_payload;
  for (auto &payload : write_payloads) {
    for (auto &i : payload) {
      i = static_cast<std::byte>(rand_payload());
    }
  }

  /* Create a list of payload buffers to read responses. */
  std::vector<std::vector<std::byte>> read_payloads(
      max_num_inflight, std::vector<std::byte>(payload_size_bytes));

  /* Create a queue of indices into the list of payload buffers. Each index
   * corresponds to a payload buffer which is available to read/write responses/
   * requests into/from (and the same index corresponds to an IOResultInternal
   * struct from io_res to write the IO response into). */
  std::queue<int> reqs;
  sandook::rt::Spin reqs_lock;
  for (int i = 0; i < max_num_inflight; i++) {
    reqs.emplace(i);
  }

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(static_cast<int>(rand_sectors()));
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, vdisk->num_sectors() - 1);

  /* Waker to use with the request sender thread and IO completion callback. */
  sandook::rt::ThreadWaker sender_waker;

  /* Callback when each request is completed successfully. */
  std::atomic_int completion_counter = 0;
  std::atomic_int failure_counter = 0;
  auto updater = [&](int i, bool success) {
    sandook::rt::SpinGuard const reqs_guard(reqs_lock);
    reqs.emplace(i);
    sender_waker.Wake();
    if (!success) {
      failure_counter++;
    }
    return completion_counter.fetch_add(1);
  };

  /* Create a list of IO result structs to reuse when sending requests.
   * Note:
   * There is a 1:1 correspondence between each element in io_res and each
   * element in payloads.
   */
  std::vector<IOResultInternal> io_res;
  for (int i = 0; i < max_num_inflight; i++) {
    IOResultInternal io_result{.updater = updater, .id = i};
    io_res.emplace_back(std::move(io_result));
  }

  /* Random number generation for choosing between read/write requests. */
  std::random_device rw_req;
  std::mt19937 rw_rng(rw_req());
  std::uniform_real_distribution<> rw_dist(0, 1);

  /* Run this thread until stopped. */
  bool stop_sender = false;
  int total_sent = 0;
  sandook::rt::Thread th_sender([&]() -> void {
    std::cout << "Task started..." << '\n';

    const uint32_t payload_sectors =
        payload_size_bytes >> sandook::kSectorShift;

    while (true) {
      sandook::rt::SpinGuard guard(reqs_lock);

      /* Park until there are more requests to send. */
      guard.Park(sender_waker, [&stop_sender, &reqs]() {
        return stop_sender || !reqs.empty();
      });

      if (unlikely(stop_sender)) {
        /* Park until all outstanding requests have completed. */
        guard.Park(sender_waker, [&reqs, &max_num_inflight]() {
          return reqs.size() == static_cast<size_t>(max_num_inflight);
        });
        break;
      }

      while (!reqs.empty()) {
        const auto id = reqs.front();

        sandook::IODesc iod{};

        const auto rw = rw_dist(rw_rng);
        if (rw <= read_ratio) {
          /* Create a read request descriptor. */
          iod.op_flags = static_cast<uint32_t>(sandook::OpType::kRead);
          iod.num_sectors = payload_sectors;
          iod.start_sector = dist(rng);
          iod.addr = reinterpret_cast<uint64_t>(read_payloads[id].data());
          iod.callback_args = static_cast<sandook::CallbackArgs>(&io_res[id]);
          iod.callback = IOCallback;
        } else {
          /* Create a write request descriptor. */
          iod.op_flags = static_cast<uint32_t>(sandook::OpType::kWrite);
          iod.num_sectors = payload_sectors;
          iod.start_sector = dist(rng);
          iod.addr = reinterpret_cast<uint64_t>(write_payloads[id].data());
          iod.callback_args = static_cast<sandook::CallbackArgs>(&io_res[id]);
          iod.callback = IOCallback;
        }

        /* Submit the request. */
        auto r = vdisk->SubmitRequest(iod);
        if (unlikely(!r)) {
          std::cerr << "Cannot submit: " << r.error() << '\n';
          continue;
        }

        /* Mark this payload buffer as in-use. */
        reqs.pop();
        total_sent++;
      }
    }
  });

  /* Stop the sender after a set duration. */
  sandook::rt::Thread th_stopper([&]() {
    /* Sleep for the duration we want to send requests. */
    sandook::rt::Sleep(task_duration);

    std::cout << "Task complete, waiting for outstanding reqs..." << '\n';

    const auto completed = completion_counter.load();
    std::cout << completed / task_duration.Seconds() << " RPS" << '\n';
    const auto failed = failure_counter.load();
    if (failed > 0) {
      std::cout << failed << "/" << completed << " failed" << '\n';
    }

    /* Wake up and signal the sender to stop. */
    {
      sandook::rt::SpinGuard const guard(reqs_lock);
      stop_sender = true;
      sender_waker.Wake();
    }
  });

  /* Wait for the sender to tear down. */
  th_sender.Join();

  /* Wait for completion. */
  th_stopper.Join();

  return completion_counter.load() == total_sent;
}
