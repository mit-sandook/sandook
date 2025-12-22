#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <stdexcept>

#include "sandook/base/constants.h"
#include "sandook/base/time.h"
#include "sandook/bindings/thread.h"
#include "sandook/bindings/timer.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/virtual_disk_utils.h"
#include "sandook/virtual_disk/virtual_disk.h"

inline constexpr auto kMaxSectors = 1ULL << 21;
inline constexpr auto kMaxInFlightRequests = 32;

/* Run a single virtual disk application. */
class SingleAppTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    vdisk = std::make_unique<sandook::VirtualDisk>(kMaxSectors);
    if (!AllocateBlocksInVirtualDisk(vdisk.get(), kPayloadSizeSectors)) {
      throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
    }
  }

  static void TearDownTestSuite() { vdisk.reset(); }

  static std::unique_ptr<sandook::VirtualDisk> vdisk;  // NOLINT
};

std::unique_ptr<sandook::VirtualDisk> SingleAppTest::vdisk;  // NOLINT

TEST_F(SingleAppTest, RandReadsUnloadedTest) {
  const auto max_inflight_reqs = 1;
  constexpr static auto task_duration_us = 2 * sandook::kOneSecond;
  const sandook::Duration task_duration(task_duration_us);

  EXPECT_TRUE(RandReadsTask(vdisk.get(), task_duration, max_inflight_reqs,
                            kPayloadSizeBytes));
}

TEST_F(SingleAppTest, RandReadsTest) {
  constexpr static auto task_duration_us = 5 * sandook::kOneSecond;
  const sandook::Duration task_duration(task_duration_us);

  EXPECT_TRUE(RandReadsTask(vdisk.get(), task_duration, kMaxInFlightRequests,
                            kPayloadSizeBytes));
}

TEST_F(SingleAppTest, RandWritesTest) {
  constexpr static auto task_duration_us = 5 * sandook::kOneSecond;
  const sandook::Duration task_duration(task_duration_us);

  EXPECT_TRUE(RandWritesTask(vdisk.get(), task_duration, kMaxInFlightRequests,
                             kPayloadSizeBytes));
}

TEST_F(SingleAppTest, RandReadsWritesTest) {
  constexpr static auto task_duration_us = 5 * sandook::kOneSecond;
  constexpr static auto read_ratio = 0.8;
  const sandook::Duration task_duration(task_duration_us);

  std::cout << "Read ratio: " << read_ratio << '\n';
  EXPECT_TRUE(RandReadsWritesTask(vdisk.get(), task_duration,
                                  kMaxInFlightRequests, kPayloadSizeBytes,
                                  read_ratio));
}

/* Run two virtual disk applications in parallel. */
class TwoAppsTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    vdisk_1 = std::make_unique<sandook::VirtualDisk>(kMaxSectors);
    if (!AllocateBlocksInVirtualDisk(vdisk_1.get(), kPayloadSizeSectors)) {
      throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
    }

    vdisk_2 = std::make_unique<sandook::VirtualDisk>(kMaxSectors);
    if (!AllocateBlocksInVirtualDisk(vdisk_2.get(), kPayloadSizeSectors)) {
      throw std::runtime_error("Cannot allocate blocks in VirtualDisk");
    }
  }

  static void TearDownTestSuite() {
    vdisk_1.reset();
    vdisk_2.reset();
  }

  static std::unique_ptr<sandook::VirtualDisk> vdisk_1;  // NOLINT
  static std::unique_ptr<sandook::VirtualDisk> vdisk_2;  // NOLINT
};

std::unique_ptr<sandook::VirtualDisk> TwoAppsTest::vdisk_1;  // NOLINT
std::unique_ptr<sandook::VirtualDisk> TwoAppsTest::vdisk_2;  // NOLINT

TEST_F(TwoAppsTest, RandReadsInPhaseTest) {
  constexpr static auto task_duration_us = 2 * sandook::kOneSecond;
  const sandook::Duration task_duration(task_duration_us);

  auto app_1 = sandook::rt::Thread([&task_duration]() {
    EXPECT_TRUE(RandReadsTask(vdisk_1.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
  });

  auto app_2 = sandook::rt::Thread([&task_duration]() {
    EXPECT_TRUE(RandReadsTask(vdisk_2.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
  });

  app_1.Join();
  app_2.Join();
}

TEST_F(TwoAppsTest, RandReadsOutOfPhaseTest) {
  constexpr static auto task_duration_us = 2 * sandook::kOneSecond;
  const sandook::Duration task_duration(task_duration_us);
  constexpr static auto sleep_duration_us = 2 * sandook::kOneSecond;
  const sandook::Duration sleep_duration(sleep_duration_us);

  auto app_1 = sandook::rt::Thread([&task_duration, &sleep_duration]() {
    EXPECT_TRUE(RandReadsTask(vdisk_1.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
    sandook::rt::Sleep(sleep_duration);
    EXPECT_TRUE(RandReadsTask(vdisk_1.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
    sandook::rt::Sleep(sleep_duration);
  });

  auto app_2 = sandook::rt::Thread([&task_duration, &sleep_duration]() {
    sandook::rt::Sleep(sleep_duration);
    EXPECT_TRUE(RandReadsTask(vdisk_2.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
    sandook::rt::Sleep(sleep_duration);
    EXPECT_TRUE(RandReadsTask(vdisk_2.get(), task_duration,
                              kMaxInFlightRequests, kPayloadSizeBytes));
  });

  app_1.Join();
  app_2.Join();
}
