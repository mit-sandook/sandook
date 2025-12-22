#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/io_desc.h"
#include "sandook/bindings/sync.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/virtual_disk_utils.h"
#include "sandook/virtual_disk/virtual_disk.h"

inline constexpr auto kMaxSectors = 1ULL << 20;

using Updater = std::function<int()>;
using RandomBytesEngine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT,
                                 unsigned char>;

namespace {

struct IOWriteResultInternal {
  Updater updater;
} __attribute__((aligned(4)));

void IOWriteCallback(sandook::CallbackArgs args, sandook::IOResult io_result) {
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

  auto *cb_res = static_cast<IOWriteResultInternal *>(args);
  [[maybe_unused]] auto successes = cb_res->updater();
}

struct IOReadResultInternal {
  Updater updater;
  uint64_t sector;
  uint64_t payload_size_bytes;
  void *written_payload;
  void *read_payload;
} __attribute__((aligned(4)));

void IOReadCallback(sandook::CallbackArgs args, sandook::IOResult io_result) {
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

  auto *cb_res = static_cast<IOReadResultInternal *>(args);
  const auto cmp = std::memcmp(cb_res->read_payload, cb_res->written_payload,
                               cb_res->payload_size_bytes);
  if (cmp != 0) {
    throw std::runtime_error("Read payload does not match written payload");
  }

  [[maybe_unused]] auto successes = cb_res->updater();
}

std::vector<std::byte> GetRandomBytes(uint64_t n_bytes) {
  RandomBytesEngine rbe;
  std::vector<unsigned char> random_data(n_bytes);
  std::generate(std::begin(random_data), std::end(random_data), std::ref(rbe));
  std::vector<std::byte> ret;
  std::transform(std::begin(random_data), std::end(random_data),
                 std::back_inserter(ret),
                 [](unsigned char c) { return static_cast<std::byte>(c); });
  return ret;
}

}  // namespace

struct VirtualDiskTestParam {
  uint32_t payload_size_sectors;
  uint64_t n_sectors;
  uint64_t sector;
};

class VirtualDiskTests : public ::testing::TestWithParam<VirtualDiskTestParam> {
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
  }

  void SetUp() override {}
  void TearDown() override {}

  static std::shared_ptr<sandook::VirtualDisk> vdisk;  // NOLINT
};

std::shared_ptr<sandook::VirtualDisk> VirtualDiskTests::vdisk;  // NOLINT

TEST_P(VirtualDiskTests, TestReadContent) {
  const auto param = GetParam();
  const auto payload_size_sectors = param.payload_size_sectors;
  const auto n_sectors = param.n_sectors;
  const auto start_sector = param.sector;
  EXPECT_TRUE(payload_size_sectors >= 1);
  EXPECT_TRUE(payload_size_sectors <= n_sectors);

  const auto payload_size_bytes = payload_size_sectors << sandook::kSectorShift;
  const auto n_ops = n_sectors / payload_size_sectors;

  /* sector -> written_payload */
  std::map<uint64_t, std::vector<std::byte>> written_payloads;

  for (uint64_t i = 0; i < n_ops; i++) {
    const auto sector = start_sector + (i * payload_size_sectors);
    std::vector<std::byte> payload = GetRandomBytes(payload_size_bytes);
    auto ret = vdisk->Write(sector, payload);
    EXPECT_TRUE(ret);
    written_payloads[sector] = std::move(payload);
  }

  for (const auto &[sector, written_payload] : written_payloads) {
    std::vector<std::byte> read_payload(payload_size_bytes);
    auto ret = vdisk->Read(sector, read_payload);
    EXPECT_TRUE(ret);
    EXPECT_EQ(*ret, payload_size_bytes);

    const auto cmp = std::memcmp(read_payload.data(), written_payload.data(),
                                 payload_size_bytes);
    EXPECT_EQ(cmp, 0);
  }
}

TEST_P(VirtualDiskTests, TestReadAsyncContent) {
  const auto param = GetParam();
  const auto payload_size_sectors = param.payload_size_sectors;
  const auto n_sectors = param.n_sectors;
  const auto start_sector = param.sector;
  EXPECT_TRUE(payload_size_sectors >= 1);
  EXPECT_TRUE(payload_size_sectors <= n_sectors);

  const auto payload_size_bytes = payload_size_sectors << sandook::kSectorShift;
  const auto n_ops = n_sectors / payload_size_sectors;

  sandook::rt::Mutex notify_lock;
  sandook::rt::CondVar notify_success;
  std::atomic<uint64_t> success_counter = 0;
  auto updater = [&notify_lock, &success_counter, &notify_success]() {
    sandook::rt::MutexGuard const guard(notify_lock);
    auto success_count_prev = success_counter.fetch_add(1);
    notify_success.Signal();
    return success_count_prev;
  };

  /* sector -> written_payload. */
  std::map<uint64_t, std::vector<std::byte>> written_payloads;

  /* Perform writes. */
  IOWriteResultInternal write_cb_result{.updater = updater};
  const auto kBatch = 512;
  uint64_t done = 0;
  while (done < n_ops) {
    for (int i = 0; i < kBatch; i++) {
      if (done == n_ops) {
        break;
      }

      const auto sector = start_sector + (done * payload_size_sectors);
      written_payloads[sector] = GetRandomBytes(payload_size_bytes);

      const sandook::IODesc iod{
          .op_flags = static_cast<uint32_t>(sandook::OpType::kWrite),
          .num_sectors = payload_size_sectors,
          .start_sector = sector,
          .addr = reinterpret_cast<uint64_t>(written_payloads[sector].data()),
          .callback_args = static_cast<sandook::CallbackArgs>(&write_cb_result),
          .callback = IOWriteCallback};

      const auto submit = vdisk->SubmitRequest(iod);
      EXPECT_TRUE(submit);

      done++;
    }
  }

  /* Conservatively set the test timeout to 200us per request. */
  const auto test_timeout_us = kBatch * 200 * sandook::kOneMicroSecond;

  {
    const sandook::rt::MutexGuard guard(notify_lock);
    const bool success =
        notify_success.WaitFor(notify_lock, test_timeout_us,
                               [&] { return success_counter.load() == done; });
    EXPECT_TRUE(success);
  }

  /* sector -> IOReadResultInternal. */
  std::map<uint64_t, IOReadResultInternal> read_io_results;

  /* sector -> read_payload. */
  std::map<uint64_t, std::vector<std::byte>> read_payloads;

  /* Perform reads. */
  success_counter.exchange(0);
  done = 0;

  while (done < n_ops) {
    for (int i = 0; i < kBatch; i++) {
      if (done == n_ops) {
        break;
      }
      const auto sector = start_sector + (done * payload_size_sectors);
      read_payloads[sector] = std::vector<std::byte>(payload_size_bytes);
      read_io_results[sector] = IOReadResultInternal{
          .updater = updater,
          .sector = sector,
          .payload_size_bytes = payload_size_bytes,
          .written_payload = written_payloads[sector].data(),
          .read_payload = read_payloads[sector].data()};

      const sandook::IODesc iod{
          .op_flags = static_cast<uint32_t>(sandook::OpType::kRead),
          .num_sectors = payload_size_sectors,
          .start_sector = sector,
          .addr = reinterpret_cast<uint64_t>(read_payloads[sector].data()),
          .callback_args =
              static_cast<sandook::CallbackArgs>(&read_io_results[sector]),
          .callback = IOReadCallback};

      const auto submit = vdisk->SubmitRequest(iod);
      EXPECT_TRUE(submit);

      done++;
    }
  }

  {
    const sandook::rt::MutexGuard guard(notify_lock);
    const bool success =
        notify_success.WaitFor(notify_lock, test_timeout_us,
                               [&] { return success_counter.load() == done; });
    EXPECT_TRUE(success);
  }
}

INSTANTIATE_TEST_SUITE_P(ContentCorrectness, VirtualDiskTests,
                         ::testing::Values(VirtualDiskTestParam{1, 1, 0},
                                           VirtualDiskTestParam{1, 10000, 1025},
                                           VirtualDiskTestParam{1, 3, 4096},
                                           VirtualDiskTestParam{5, 5, 0},
                                           VirtualDiskTestParam{5, 50, 1025}));

class VirtualDiskGCTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

  void SetUp() override {
    vdisk_ = std::make_unique<sandook::VirtualDisk>(kMaxSectors);
    if (!AllocateBlocksInVirtualDisk(vdisk_.get(), kPayloadSizeSectors)) {
      throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
    }
  }

  void TearDown() override { vdisk_.reset(); }

  std::unique_ptr<sandook::VirtualDisk> vdisk_;  // NOLINT
};

TEST_F(VirtualDiskGCTests, TestNoGC) {
  const sandook::VolumeBlockAddr sector_1 = 1;
  const sandook::VolumeBlockAddr sector_2 = 2;
  const auto payload_size_sectors = 1;
  const auto payload_size_bytes = payload_size_sectors << sandook::kSectorShift;
  std::vector<std::byte> payload = GetRandomBytes(payload_size_bytes);

  /* Write to a sector. */
  auto ret = vdisk_->Write(sector_1, payload);
  EXPECT_TRUE(ret);

  /* Wait long enough so that GC has occured. */
  const auto interval_us =
      static_cast<int64_t>(sandook::kGarbageCollectionIntervalUs) * 2;
  const sandook::Duration interval(interval_us);
  sandook::rt::Sleep(interval);

  /* Nothing to GC yet. */
  EXPECT_EQ(0, vdisk_->num_gc_blocks());

  /* Write to the same sector again. */
  ret = vdisk_->Write(sector_2, payload);
  EXPECT_TRUE(ret);

  /* Wait long enough so that GC has occured. */
  sandook::rt::Sleep(interval);

  /* GC should not have discarded any blocks because both the write operations
   * were on different sectors so there is nothing to GC.
   */
  EXPECT_EQ(0, vdisk_->num_gc_blocks());
}

TEST_F(VirtualDiskGCTests, TestGCSingleBlock) {
  if (sandook::kGarbageCollectionIntervalUs == 0) {
    GTEST_SKIP() << "Garbage collection disabled";
  }

  const sandook::VolumeBlockAddr sector = 1;
  const auto payload_size_sectors = 1;
  const auto payload_size_bytes = payload_size_sectors << sandook::kSectorShift;
  std::vector<std::byte> payload = GetRandomBytes(payload_size_bytes);

  /* Write to a sector. */
  auto ret = vdisk_->Write(sector, payload);
  EXPECT_TRUE(ret);

  /* Wait long enough so that GC has occured. */
  const auto interval_us =
      static_cast<int64_t>(sandook::kGarbageCollectionIntervalUs) * 2;
  const sandook::Duration interval(interval_us);
  sandook::rt::Sleep(interval);

  /* Nothing to GC yet. */
  EXPECT_EQ(0, vdisk_->num_gc_blocks());

  /* Write to the same sector again. */
  ret = vdisk_->Write(sector, payload);
  EXPECT_TRUE(ret);

  /* Wait long enough so that GC has occured. */
  sandook::rt::Sleep(interval);

  /* GC should have occured on that sector. */
  EXPECT_EQ(payload_size_sectors * sandook::kNumReplicas,
            vdisk_->num_gc_blocks());
}
