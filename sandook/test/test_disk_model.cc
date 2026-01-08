#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/disk_model/disk_model.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT

constexpr auto kTestModelName = "test";

struct DiskModelTestParam {
  uint64_t load;
  sandook::OpType op;
  sandook::ServerMode mode;
  double write_ratio;
  uint64_t expected_latency;
};

class DiskModelTests : public ::testing::TestWithParam<DiskModelTestParam> {
 protected:
  static void SetUpTestSuite() {
    model = std::make_shared<sandook::DiskModel>(kTestModelName);
  }

  static void TearDownTestSuite() { model.reset(); }

  void SetUp() override {}

  void TearDown() override {}

  static std::shared_ptr<sandook::DiskModel> model;  // NOLINT
};

std::shared_ptr<sandook::DiskModel> DiskModelTests::model;  // NOLINT

TEST_P(DiskModelTests, TestLatencySelection) {
  const auto param = GetParam();
  const auto load = param.load;
  const auto op = param.op;
  const auto mode = param.mode;
  const auto write_ratio = param.write_ratio;
  const auto expected_latency = param.expected_latency;

  const auto latency = model->GetLatency(load, op, mode, write_ratio);

  EXPECT_EQ(latency, expected_latency);
}

// NOLINTBEGIN
INSTANTIATE_TEST_SUITE_P(
    LatencySelection, DiskModelTests,
    ::testing::Values(
        /* Pure read. Exact. */
        DiskModelTestParam{850087, sandook::OpType::kRead,
                           sandook::ServerMode::kRead, 0.0, 275},
        /* Pure read. Extrapolate. */
        DiskModelTestParam{940000, sandook::OpType::kRead,
                           sandook::ServerMode::kRead, 0.0, 311},
        /* Pure read. Saturation. */
        DiskModelTestParam{2000000, sandook::OpType::kRead,
                           sandook::ServerMode::kRead, 0.0, 1050},
        /* Pure write. Extrapolate. */
        DiskModelTestParam{420000, sandook::OpType::kWrite,
                           sandook::ServerMode::kWrite, 1.0, 687},
        /* < 25% write. Exact. */
        DiskModelTestParam{124764, sandook::OpType::kWrite,
                           sandook::ServerMode::kMix, 0.13, 286},
        /* < 50% write. Exact. */
        DiskModelTestParam{504464, sandook::OpType::kWrite,
                           sandook::ServerMode::kMix, 0.33, 734},
        /* < 75% write. Exact. */
        DiskModelTestParam{373401, sandook::OpType::kWrite,
                           sandook::ServerMode::kMix, 0.63, 546}));
// NOLINTEND
