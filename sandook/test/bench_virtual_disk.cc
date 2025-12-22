#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/sync.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/test_utils.h"
#include "sandook/test/utils/virtual_disk_utils.h"
#include "sandook/virtual_disk/virtual_disk.h"

inline constexpr auto kMaxSectors = 1ULL << 20;

using Updater = std::function<int()>;

namespace {

struct IOResultInternal {
  Updater updater;
} __attribute__((aligned(4)));

void IOCallback(sandook::CallbackArgs args, sandook::IOResult io_result) {
  switch (io_result.status) {
    case sandook::IOStatus::kOk:
      break;

    case sandook::IOStatus::kFailed:
      throw std::runtime_error("IO failed");
      break;

    default:
      throw std::runtime_error("Unknown IOStatus");
      break;
  }

  auto *cb_res = static_cast<IOResultInternal *>(args);
  [[maybe_unused]] auto successes = cb_res->updater();
}

}  // namespace

class VirtualDiskBenchmarks : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    vdisk = std::make_shared<sandook::VirtualDisk>(kMaxSectors);
    if (!AllocateBlocksInVirtualDisk(vdisk.get(), kPayloadSizeSectors)) {
      throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
    }
  }

  static void TearDownTestSuite() {
    /* This is to ensure that we destroy the VirtualDisk and associated threads
     * before Caladan is teared down to make sure the threads can be cleaned up
     * properly.
     */
    vdisk.reset();
    PrintAllResults(&results);
  }

  void SetUp() override {}
  void TearDown() override {}

  static std::shared_ptr<sandook::VirtualDisk> vdisk;  // NOLINT

  /* All tests add their timing measurements to this data structure. */
  static BenchResults results;  // NOLINT
};

BenchResults VirtualDiskBenchmarks::results({});                     // NOLINT
std::shared_ptr<sandook::VirtualDisk> VirtualDiskBenchmarks::vdisk;  // NOLINT

struct BenchReadArgs {
  /* Number of sectors in the virtual disk. */
  uint64_t n_sectors;

  /* Sectors to access in this benchmark.
   * This should contain measure_rounds entries.
   */
  std::vector<uint64_t> sectors;

  /* Read the payloads into these buffers.
   * This should contain measure_rounds entries.
   */
  std::vector<std::vector<std::byte>> payloads;

  /* Virtual disk to test. */
  std::shared_ptr<sandook::VirtualDisk> vdisk;
};

bool BenchRead(int measure_rounds, void *args_ptr) {
  auto *args = reinterpret_cast<BenchReadArgs *>(args_ptr);

  for (int i = 0; i < measure_rounds; i++) {
    auto r = args->vdisk->Read(args->sectors[i], args->payloads[i]);
    if (unlikely(!r)) {
      std::cerr << "Cannot read: " << r.error() << '\n';
      return false;
    }
  }

  return true;
}

bool BenchReadAsync(int measure_rounds, void *args_ptr) {
  auto *args = reinterpret_cast<BenchReadArgs *>(args_ptr);

  const auto op = sandook::OpType::kRead;
  const auto num_sectors = kPayloadSizeBytes >> sandook::kSectorShift;

  sandook::rt::Mutex notify_lock;
  sandook::rt::CondVar notify_success;
  std::atomic_int success_counter = 0;
  auto updater = [&notify_lock, &success_counter, &notify_success]() {
    sandook::rt::MutexGuard const guard(notify_lock);
    auto success_count_prev = success_counter.fetch_add(1);
    notify_success.Signal();
    return success_count_prev;
  };

  IOResultInternal cb_result{.updater = updater};

  auto done = 0;
  const auto kBatch = 512;
  while (done < measure_rounds) {
    for (int i = 0; i < kBatch; i++) {
      const sandook::IODesc iod{
          .op_flags = static_cast<uint32_t>(op),
          .num_sectors = num_sectors,
          .start_sector = args->sectors[i],
          .addr = reinterpret_cast<uint64_t>(args->payloads[i].data()),
          .callback_args = static_cast<sandook::CallbackArgs>(&cb_result),
          .callback = IOCallback};

      auto r = args->vdisk->SubmitRequest(iod);
      if (unlikely(!r)) {
        std::cerr << "Cannot submit: " << r.error() << '\n';
        return false;
      }
    }

    done += kBatch;

    /* Conservatively set the test timeout to 200us per request. */
    const auto test_timeout_us = kBatch * 200;

    {
      const sandook::rt::MutexGuard guard(notify_lock);
      const bool success = notify_success.WaitFor(
          notify_lock, test_timeout_us,
          [&] { return success_counter.load() == done; });
      if (!success) {
        return false;
      }
    }
  }

  return true;
}

struct BenchWriteArgs {
  /* Number of sectors in the virtual disk. */
  uint64_t n_sectors;

  /* Sectors to access in this benchmark.
   * This should contain measure_rounds entries.
   */
  std::vector<uint64_t> sectors;

  /* Payload to write. */
  std::vector<std::byte> payload;

  /* Virtual disk to test. */
  std::shared_ptr<sandook::VirtualDisk> vdisk;
};

bool BenchWrite(int measure_rounds, const void *args_ptr) {
  const auto *args = reinterpret_cast<const BenchWriteArgs *>(args_ptr);

  for (int i = 0; i < measure_rounds; i++) {
    auto r = args->vdisk->Write(args->sectors[i], args->payload);
    if (unlikely(!r)) {
      std::cerr << "Cannot write: " << r.error() << '\n';
      return false;
    }
  }

  return true;
}

TEST_F(VirtualDiskBenchmarks, BenchRead) {
  const int measure_rounds = GetMeasureRounds();

  BenchReadArgs args{.n_sectors = kMaxSectors};
  args.sectors.resize(measure_rounds);
  args.vdisk = vdisk;

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, args.n_sectors - 1);
  for (uint64_t &sector : args.sectors) {
    sector = dist(rng);
  }

  /* Allocate payload buffers to perform the read operations into. */
  args.payloads.resize(measure_rounds);
  for (auto &payload : args.payloads) {
    payload = std::vector<std::byte>(kPayloadSizeBytes);
  }

  EXPECT_TRUE(
      Bench("BenchRead", BenchRead, reinterpret_cast<void *>(&args), &results));
}

TEST_F(VirtualDiskBenchmarks, BenchReadAsync) {
  const int measure_rounds = GetMeasureRounds();

  BenchReadArgs args{.n_sectors = kMaxSectors};
  args.sectors.resize(measure_rounds);
  args.vdisk = vdisk;

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, args.n_sectors - 1);
  for (uint64_t &sector : args.sectors) {
    sector = dist(rng);
  }

  /* Allocate payload buffers to perform the read operations into. */
  args.payloads.resize(measure_rounds);
  for (auto &payload : args.payloads) {
    payload = std::vector<std::byte>(kPayloadSizeBytes);
  }

  EXPECT_TRUE(Bench("BenchReadAsync", BenchReadAsync,
                    reinterpret_cast<void *>(&args), &results));
}

TEST_F(VirtualDiskBenchmarks, BenchWrite) {
  const int measure_rounds = GetMeasureRounds();

  BenchWriteArgs args{.n_sectors = kMaxSectors};
  args.sectors.resize(measure_rounds);
  args.payload.resize(kPayloadSizeBytes);
  args.vdisk = vdisk;

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, args.n_sectors - 1);
  for (uint64_t &sector : args.sectors) {
    sector = dist(rng);
  }

  /* Fill the buffer to write. */
  std::random_device rand_payload;
  for (auto &i : args.payload) {
    i = static_cast<std::byte>(rand_payload());
  }

  EXPECT_TRUE(Bench("BenchWrite", BenchWrite, reinterpret_cast<void *>(&args),
                    &results));
}
