#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <iostream>
#include <map>
#include <string>

extern "C" {
#include "base/assert.h"  // NOLINT
}

#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/base/types.h"
#include "sandook/config/config.h"
#include "sandook/scheduler/data_plane/scheduler.h"
#include "sandook/test/utils/gtest/assertion.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT

constexpr auto kSelectionRatioEpsilon = 0.005;

class CreateDataPlaneSchedulerTests
    : public ::testing::TestWithParam<sandook::Config::DataPlaneSchedulerType> {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

  void SetUp() override {}
  void TearDown() override {}
};

TEST_P(CreateDataPlaneSchedulerTests, TestCreateDataPlaneScheduler) {
  const auto type = GetParam();
  const auto sched = sandook::schedulers::data_plane::Scheduler(type);
}

INSTANTIATE_TEST_SUITE_P(
    DataPlaneSchedulerTypes, CreateDataPlaneSchedulerTests,
    ::testing::Values(
        sandook::Config::DataPlaneSchedulerType::kWeightedReadWrite,
        sandook::Config::DataPlaneSchedulerType::kRandomReadWrite,
        sandook::Config::DataPlaneSchedulerType::kWeightedReadHashWrite,
        sandook::Config::DataPlaneSchedulerType::kRandomReadHashWrite));

struct SelectReadServerTestParam {
  sandook::Config::DataPlaneSchedulerType scheduler_type;
  /* Mapping of server name to assigned read weight. */
  std::map<std::string, sandook::ServerWeight> server_weights;
  /* Mapping of server name to expected ratio of it being selected. */
  std::map<std::string, double> expected_selection_ratios;
  /* Mapping of server name to assigned moder. */
  std::map<std::string, sandook::ServerMode> server_modes;
};

class SelectReadServerTests
    : public ::testing::TestWithParam<SelectReadServerTestParam> {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

  void SetUp() override {}
  void TearDown() override {}
};

TEST_P(SelectReadServerTests, TestSelectReadServer) {
  const auto kIterations = 1000000;

  const auto param = GetParam();
  const auto type = param.scheduler_type;
  const auto server_weights = param.server_weights;
  const auto expected_selection_ratios = param.expected_selection_ratios;
  const auto server_modes = param.server_modes;
  EXPECT_EQ(server_weights.size(), server_modes.size());

  auto sched = sandook::schedulers::data_plane::Scheduler(type);

  sandook::ServerID server_id = sandook::kInvalidServerID + 1;

  std::map<sandook::ServerID, std::string> server_names;
  sandook::ServerStatsList server_stats_list;
  sandook::ServerSet subset;

  for (const auto &[server_name, server_weight] : server_weights) {
    const auto mode_it = server_modes.find(server_name);
    EXPECT_NE(mode_it, server_modes.end());
    const auto mode = mode_it->second;

    const sandook::ServerStats server_stats{
        .server_id = server_id, .mode = mode, .read_weight = server_weight};
    server_stats_list.emplace_back(server_stats);

    const auto &[it, ok] = server_names.try_emplace(server_id, server_name);
    EXPECT_TRUE(ok);
    EXPECT_NE(it, server_names.end());

    subset.insert(server_id);

    server_id++;
  }

  const auto set_servers = sched.SetServerStats(server_stats_list);
  EXPECT_TRUE(set_servers);

  const sandook::VolumeID vol_id = sandook::kInvalidVolumeID;
  const sandook::IODesc *iod = nullptr;

  std::map<sandook::ServerID, int> selection_counts;
  for (int i = 0; i < kIterations; i++) {
    const auto selection = sched.SelectReadServer(&subset, vol_id, iod);
    EXPECT_TRUE(selection);
    selection_counts[*selection]++;
  }

  std::map<sandook::ServerID, double> selection_ratios;
  for (const auto &[server_id, counter] : selection_counts) {
    selection_ratios[server_id] = static_cast<double>(counter) / kIterations;
  }

  for (const auto &[server_id, selection_ratio] : selection_ratios) {
    const auto name_it = server_names.find(server_id);
    EXPECT_NE(name_it, server_names.end());
    const auto name = name_it->second;

    const auto expected_ratio_it = expected_selection_ratios.find(name);
    EXPECT_NE(expected_ratio_it, expected_selection_ratios.end());
    const auto expected_ratio = expected_ratio_it->second;

    EXPECT_TRUE(sandook::test::IsWithinRange(selection_ratio, expected_ratio,
                                             kSelectionRatioEpsilon));
  }
}

// NOLINTBEGIN
INSTANTIATE_TEST_SUITE_P(
    ReadWeightCombinations, SelectReadServerTests,
    ::testing::Values(
        SelectReadServerTestParam(
            sandook::Config::DataPlaneSchedulerType::kWeightedReadWrite,
            {{"dev1", 0.167234},
             {"dev2", 0.172023},
             {"dev3", 0.161345},
             {"dev4", 0.163725},
             {"dev5", 0.166080},
             {"dev6", 0.169589}},
            {{"dev1", 0.167234},
             {"dev2", 0.172023},
             {"dev3", 0.161345},
             {"dev4", 0.163725},
             {"dev5", 0.166080},
             {"dev6", 0.169589}},
            {{"dev1", sandook::ServerMode::kRead},
             {"dev2", sandook::ServerMode::kRead},
             {"dev3", sandook::ServerMode::kRead},
             {"dev4", sandook::ServerMode::kRead},
             {"dev5", sandook::ServerMode::kRead},
             {"dev6", sandook::ServerMode::kRead}}),
        SelectReadServerTestParam(
            sandook::Config::DataPlaneSchedulerType::kRandomReadWrite,
            {{"dev1", 0.167234},
             {"dev2", 0.172023},
             {"dev3", 0.161345},
             {"dev4", 0.163725},
             {"dev5", 0.166080},
             {"dev6", 0.169589}},
            {{"dev1", 0.167777},
             {"dev2", 0.167777},
             {"dev3", 0.167777},
             {"dev4", 0.167777},
             {"dev5", 0.167777},
             {"dev6", 0.167777}},
            {{"dev1", sandook::ServerMode::kRead},
             {"dev2", sandook::ServerMode::kRead},
             {"dev3", sandook::ServerMode::kRead},
             {"dev4", sandook::ServerMode::kRead},
             {"dev5", sandook::ServerMode::kRead},
             {"dev6", sandook::ServerMode::kRead}})));
// NOLINTEND
