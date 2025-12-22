#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "sandook/base/constants.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/time.h"
#include "sandook/base/types.h"
#include "sandook/bindings/timer.h"
#include "sandook/config/config.h"
#include "sandook/scheduler/control_plane/scheduler.h"
#include "sandook/scheduler/control_plane/server_stats_manager.h"
#include "sandook/test/utils/gtest/assertion.h"     // NOLINT
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT

namespace {

constexpr auto kWeightEpsilon = 0.002;
constexpr auto kTestDiskName = "test";

void CreateServers(sandook::schedulers::control_plane::Scheduler *sched,
                   const sandook::DiskModel *model, int n_read, int n_write) {
  sandook::ServerID server_id = sandook::kInvalidServerID + 1;

  for (int i = 0; i < n_read; i++) {
    const auto add = sched->AddServer(server_id, kTestDiskName, model);
    EXPECT_TRUE(add);

    const sandook::ServerStats stats{.server_id = server_id,
                                     .mode = sandook::ServerMode::kMix,
                                     .completed_reads = 70};
    auto update = sched->UpdateServerStats(server_id, stats);
    EXPECT_TRUE(update);
    server_id++;
  }

  for (int i = 0; i < n_write; i++) {
    const auto add = sched->AddServer(server_id, kTestDiskName, model);
    EXPECT_TRUE(add);

    const sandook::ServerStats stats{.server_id = server_id,
                                     .mode = sandook::ServerMode::kMix,
                                     .completed_writes = 10};
    auto update = sched->UpdateServerStats(server_id, stats);
    EXPECT_TRUE(update);
    server_id++;
  }
}

}  // namespace

class CreateControlPlaneSchedulerTests
    : public ::testing::TestWithParam<
          sandook::Config::ControlPlaneSchedulerType> {
 protected:
  static void SetUpTestSuite() {
    model = std::make_shared<sandook::DiskModel>(kTestDiskName);
  }

  static void TearDownTestSuite() { model.reset(); }

  void SetUp() override {}

  void TearDown() override {}

  static std::shared_ptr<sandook::DiskModel> model;  // NOLINT
};

std::shared_ptr<sandook::DiskModel>
    CreateControlPlaneSchedulerTests::model;  // NOLINT

TEST_P(CreateControlPlaneSchedulerTests, TestCreateControlPlaneScheduler) {
  const auto type = GetParam();
  const auto sched = sandook::schedulers::control_plane::Scheduler(type);
}

TEST_P(CreateControlPlaneSchedulerTests, TestAddServer) {
  const auto type = GetParam();
  auto sched = sandook::schedulers::control_plane::Scheduler(type);

  const sandook::ServerID server_id = sandook::kInvalidServerID + 1;

  const auto add = sched.AddServer(server_id, kTestDiskName, model.get());
  EXPECT_TRUE(add);
}

INSTANTIATE_TEST_SUITE_P(
    ControlPlaneSchedulerTypes, CreateControlPlaneSchedulerTests,
    ::testing::Values(
        sandook::Config::ControlPlaneSchedulerType::kRWIsolationStrict,
        sandook::Config::ControlPlaneSchedulerType::kRWIsolationWeak,
        sandook::Config::ControlPlaneSchedulerType::kProfileGuided,
        sandook::Config::ControlPlaneSchedulerType::kProfileGuidedRWIsolation));

class ControlPlaneSchedulerTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    model = std::make_shared<sandook::DiskModel>(kTestDiskName);
  }

  static void TearDownTestSuite() { model.reset(); }

  void SetUp() override {}

  void TearDown() override {}

  static std::shared_ptr<sandook::DiskModel> model;  // NOLINT
};

std::shared_ptr<sandook::DiskModel>
    ControlPlaneSchedulerTests::model;  // NOLINT

TEST_F(ControlPlaneSchedulerTests, TestUpdateServerStats) {
  const auto type = sandook::Config::ControlPlaneSchedulerType::kProfileGuided;
  auto sched = sandook::schedulers::control_plane::Scheduler(type);

  const sandook::ServerID server_id = sandook::kInvalidServerID + 1;
  const auto add = sched.AddServer(server_id, kTestDiskName, model.get());
  EXPECT_TRUE(add);

  const sandook::ServerStats stats{.server_id = server_id,
                                   .mode = sandook::ServerMode::kMix,
                                   .inflight_reads = 10,
                                   .inflight_writes = 20};
  const auto update = sched.UpdateServerStats(server_id, stats);
  EXPECT_TRUE(update);
}

struct ServerModeTestParam {
  int n_read_servers;
  int n_write_servers;
  int expected_n_read_mode;
  int expected_n_write_mode;
};

class ServerModeTests
    : public ::testing::TestWithParam<std::tuple<
          sandook::Config::ControlPlaneSchedulerType, ServerModeTestParam>> {
 protected:
  static void SetUpTestSuite() {
    model = std::make_shared<sandook::DiskModel>(kTestDiskName);
  }

  static void TearDownTestSuite() { model.reset(); }

  void SetUp() override {}

  void TearDown() override {}

  static std::shared_ptr<sandook::DiskModel> model;  // NOLINT
};

std::shared_ptr<sandook::DiskModel> ServerModeTests::model;  // NOLINT

TEST_P(ServerModeTests, TestIsolatedServerModes) {
  sandook::Config::ControlPlaneSchedulerType type{};
  ServerModeTestParam param;
  std::tie(type, param) = GetParam();

  /* Create the scheduler. */
  auto sched = sandook::schedulers::control_plane::Scheduler(type);

  /* Freeze any load calculation changes so that once we create the servers and
   * set the specific load using UpdateServerStats, it does not get set back to
   * zero (in order to be set again upon the next load calculation iteration).
   */
  sched.FreezeLoad();

  CreateServers(&sched, model.get(), param.n_read_servers,
                param.n_write_servers);

  const auto interval_us = 2 * sandook::kModeSwitchIntervalUs;
  const sandook::Duration interval(interval_us);
  sandook::rt::Sleep(interval);
  sched.Stop();

  const auto load = sched.GetSystemLoad();
  EXPECT_TRUE(load);

  const auto stats = sched.GetServerStats();
  EXPECT_TRUE(stats);

  int n_read_mode = 0;
  int n_write_mode = 0;
  for (const auto &server : *stats) {
    if (server.mode == sandook::ServerMode::kRead) {
      n_read_mode++;
    } else if (server.mode == sandook::ServerMode::kWrite) {
      n_write_mode++;
    }
  }

  EXPECT_EQ(n_read_mode, param.expected_n_read_mode);
  EXPECT_EQ(n_write_mode, param.expected_n_write_mode);
}

INSTANTIATE_TEST_SUITE_P(
    RWIsolationCombinations, ServerModeTests,
    ::testing::Combine(
        ::testing::Values(
            sandook::Config::ControlPlaneSchedulerType::
                kProfileGuidedRWIsolation,
            sandook::Config::ControlPlaneSchedulerType::kRWIsolationWeak,
            sandook::Config::ControlPlaneSchedulerType::kRWIsolationStrict),
        ::testing::Values(ServerModeTestParam(5, 3, 6, 2),
                          ServerModeTestParam(0, 8, 0, 8),
                          ServerModeTestParam(8, 0, 8, 0),
                          ServerModeTestParam(7, 1, 6, 2))));

struct ServerWeightTestParam {
  sandook::OpType op;
  double total_read_mops;
  double total_write_mops;
  /* Mapping of server name to expected weight. */
  std::map<std::string, sandook::ServerWeight> expected_weights;
  /* Mapping of server name to the mode it should be on. */
  std::map<std::string, sandook::ServerMode> server_modes;
};

class ServerWeightTests
    : public ::testing::TestWithParam<ServerWeightTestParam> {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

  void SetUp() override {}
  void TearDown() override {}
};

TEST_P(ServerWeightTests, TestProfileGuidedRWIsolationServerReadWeights) {
  const auto param = GetParam();
  const auto total_read_mops = param.total_read_mops;
  const auto total_write_mops = param.total_write_mops;
  const auto server_weights = param.expected_weights;
  const auto server_modes = param.server_modes;
  EXPECT_EQ(server_weights.size(), server_modes.size());

  // TODO(girfan): Make this a parameter.
  const auto type =
      sandook::Config::ControlPlaneSchedulerType::kProfileGuidedRWIsolation;
  auto sched = sandook::schedulers::control_plane::Scheduler(type);

  auto n_read_servers = 0;
  auto n_write_servers = 0;
  for (const auto &[_, mode] : server_modes) {
    switch (mode) {
      case sandook::ServerMode::kRead:
        n_read_servers++;
        break;

      case sandook::ServerMode::kWrite:
        n_write_servers++;
        break;

      default:
        throw std::runtime_error("Unexpected server mode in test params");
        std::unreachable();
    }
  }

  const auto initial_per_server_read_ops =
      total_read_mops * sandook::kMillion / n_read_servers;
  const auto initial_per_server_reads =
      initial_per_server_read_ops /
      (static_cast<double>(sandook::kOneSecond) /
       sandook::schedulers::control_plane::kLoadCalculationIntervalUs);
  const auto initial_per_server_write_ops =
      total_write_mops * sandook::kMillion / n_write_servers;
  const auto initial_per_server_writes =
      initial_per_server_write_ops /
      (static_cast<double>(sandook::kOneSecond) /
       sandook::schedulers::control_plane::kLoadCalculationIntervalUs);

  sandook::ServerID server_id = sandook::kInvalidServerID + 1;
  std::map<sandook::ServerID, std::string> disk_names;

  for (const auto &[disk_name, _] : server_weights) {
    const auto add = sched.AddServer(server_id, disk_name);
    EXPECT_TRUE(add);

    const auto &[it, ok] = disk_names.try_emplace(server_id, disk_name);
    EXPECT_TRUE(ok);
    EXPECT_NE(it, disk_names.end());

    server_id++;
  }

  sched.FreezeModes();
  sched.FreezeLoad();

  const auto is_override = true;
  const auto is_update_load = true;

  for (const auto &[server_id, disk_name] : disk_names) {
    const auto mode_it = server_modes.find(disk_name);
    EXPECT_NE(mode_it, server_modes.end());
    const auto mode = mode_it->second;

    sandook::ServerStats server_stats{.server_id = server_id, .mode = mode};
    if (mode == sandook::ServerMode::kRead) {
      server_stats.completed_reads =
          static_cast<uint32_t>(initial_per_server_reads);
    }
    if (mode == sandook::ServerMode::kWrite) {
      server_stats.completed_writes =
          static_cast<uint32_t>(initial_per_server_writes);
    }

    const auto update = sched.UpdateServerStats(server_id, server_stats,
                                                is_override, is_update_load);
    EXPECT_TRUE(update);
  }

  sandook::rt::Sleep(sandook::Duration(
      sandook::schedulers::control_plane::kLoadCalculationIntervalUs));

  sched.Update();

  const auto stats = sched.GetServerStats();
  EXPECT_TRUE(stats);

  for (const auto &server : *stats) {
    const auto it_name = disk_names.find(server.server_id);
    EXPECT_NE(it_name, disk_names.end());
    const auto disk_name = it_name->second;

    const auto it_weight = server_weights.find(disk_name);
    EXPECT_NE(it_weight, server_weights.end());
    const auto expected_weight = it_weight->second;

    double server_weight = 0.0;
    if (param.op == sandook::OpType::kRead) {
      server_weight = server.read_weight;
    } else {
      server_weight = server.write_weight;
    }

    EXPECT_TRUE(sandook::test::IsWithinRange(server_weight, expected_weight,
                                             kWeightEpsilon));
  }
}

// NOLINTBEGIN
INSTANTIATE_TEST_SUITE_P(
    ServerModeCombinations, ServerWeightTests,
    ::testing::Values(
        /* Read from all disks. */
        ServerWeightTestParam(sandook::OpType::kRead, 1.5, 0.0,
                              {{"S39WNA0KB01161", 0.223414},
                               {"S39WNA0KC01659", 0.384478},
                               {"S39WNA0KC02074", 0.392109}},
                              {{"S39WNA0KB01161", sandook::ServerMode::kRead},
                               {"S39WNA0KC01659", sandook::ServerMode::kRead},
                               {"S39WNA0KC02074", sandook::ServerMode::kRead}}),
        /* Read from all disks. */
        ServerWeightTestParam(sandook::OpType::kRead, 4.0, 0.0,
                              {{"S39WNA0KB01161", 0.157924},
                               {"S39WNA0KC01659", 0.423664},
                               {"S39WNA0KC02074", 0.423411}},
                              {{"S39WNA0KB01161", sandook::ServerMode::kRead},
                               {"S39WNA0KC01659", sandook::ServerMode::kRead},
                               {"S39WNA0KC02074", sandook::ServerMode::kRead}}),
        /* Write to any of the disks. */
        ServerWeightTestParam(sandook::OpType::kWrite, 0.0, 1.0,
                              {{"S39WNA0KB01161", 0.291466},
                               {"S39WNA0KC01659", 0.375018},
                               {"S39WNA0KC02074", 0.338516}},
                              {{"S39WNA0KB01161", sandook::ServerMode::kWrite},
                               {"S39WNA0KC01659", sandook::ServerMode::kWrite},
                               {"S39WNA0KC02074",
                                sandook::ServerMode::kWrite}}),
        /* Read from read/write disks; higher load from read mode disks. */
        ServerWeightTestParam(sandook::OpType::kRead, 1.1, 0.0,
                              {{"S39WNA0KB01161", 0.309271},
                               {"S39WNA0KC01659", 0.669657},
                               {"S39WNA0KC02074", 0.021072}},
                              {{"S39WNA0KB01161", sandook::ServerMode::kRead},
                               {"S39WNA0KC01659", sandook::ServerMode::kRead},
                               {"S39WNA0KC02074",
                                sandook::ServerMode::kWrite}}),
        /* Read from read/write disks; writes are also going on. */
        ServerWeightTestParam(sandook::OpType::kRead, 1.0, 0.3,
                              {{"S39WNA0KB01161", 0.267310},
                               {"S39WNA0KC01659", 0.020951},
                               {"S39WNA0KC02074", 0.711739}},
                              {{"S39WNA0KB01161", sandook::ServerMode::kRead},
                               {"S39WNA0KC01659", sandook::ServerMode::kWrite},
                               {"S39WNA0KC02074",
                                sandook::ServerMode::kRead}})));
// NOLINTEND
