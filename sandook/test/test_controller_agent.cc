#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <memory>
#include <string>

#include "sandook/base/server_stats.h"
#include "sandook/controller/controller_agent.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT

inline constexpr auto kMockIP = "192.168.127.3";
inline constexpr auto kMockPort = 7777;
inline constexpr auto kMockSectors = 1ULL << 20;
inline constexpr auto kMockName = "mock_server";

class ControllerAgentTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ControllerAgentTests, TestRegisterServer) {
  auto agent = std::make_unique<sandook::ControllerAgent>();

  auto ret = agent->RegisterServer(kMockIP, kMockPort, kMockName, kMockSectors);
  EXPECT_TRUE(ret);
  EXPECT_EQ(1, *ret);
}

TEST_F(ControllerAgentTests, TestRegisterVolume) {
  auto agent = std::make_unique<sandook::ControllerAgent>();

  auto ret = agent->RegisterVolume(kMockIP, kMockPort, kMockSectors);
  EXPECT_TRUE(ret);
  EXPECT_EQ(1, *ret);
}

TEST_F(ControllerAgentTests, TestUpdateServerStats) {
  auto agent = std::make_unique<sandook::ControllerAgent>();

  auto reg = agent->RegisterServer(kMockIP, kMockPort, kMockName, kMockSectors);
  auto server_id = *reg;

  const sandook::ServerStats stats{.mode = sandook::ServerMode::kMix,
                                   .inflight_reads = 10,
                                   .inflight_writes = 20};
  auto update = agent->UpdateServerStats(server_id, stats);
  EXPECT_TRUE(update);
}
