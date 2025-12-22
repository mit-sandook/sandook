
#include <stdexcept>
#include <string>

extern "C" {}

#include <gtest/gtest.h>

#include "sandook/test/utils/blk_dev_utils.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/test_utils.h"

inline constexpr auto kSandookBlkDev = "/dev/ublkb0";

class DISABLED_BlkDevTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    if (!FillBlockDevice(kSandookBlkDev)) {
      throw std::runtime_error("Failed to fill block device");
    }
  }

  static void TearDownTestSuite() { PrintAllResults(&results); }

  /* All tests add their timing measurements to this data structure. */
  static BenchResults results;  // NOLINT
};

BenchResults DISABLED_BlkDevTests::results({});  // NOLINT

TEST_F(DISABLED_BlkDevTests, BenchRead) {
  EXPECT_TRUE(BenchBlockDeviceRead(kSandookBlkDev, &results));
}

TEST_F(DISABLED_BlkDevTests, BenchWrite) {
  EXPECT_TRUE(BenchBlockDeviceWrite(kSandookBlkDev, &results));
}
