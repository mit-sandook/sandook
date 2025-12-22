#include <gtest/gtest.h>
#include <time.h>  // NOLINT

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "sandook/base/io.h"
#include "sandook/base/msg.h"
#include "sandook/base/types.h"
#include "sandook/config/config.h"
#include "sandook/rpc/rpc.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/test_utils.h"

inline constexpr auto kMaxSectors = 1ULL << 20;

struct BenchArgs {
  sandook::VolumeID vol_id;
  sandook::ServerID server_id;
  sandook::RPCClient* client;
};

bool BenchAllocateBlocks(int measure_rounds, void* args_ptr) {
  auto* args = reinterpret_cast<BenchArgs*>(args_ptr);
  const auto vol_id = args->vol_id;
  const auto server_id = args->server_id;

  auto msg = sandook::CreateAllocateBlocksMsg(vol_id, server_id);
  const auto payload_size = sandook::GetMsgSize(msg.get());

  for (int i = 0; i < measure_rounds; ++i) {
    const auto resp =
        args->client->Call(sandook::writable_span(msg.get(), payload_size));
    if (resp.get_buf().size() < sizeof(sandook::AllocateBlocksReplyMsg)) {
      return false;
    }
  }

  return true;
}

class ControllerTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    client = sandook::RPCClient::Connect(sandook::Config::kControllerIP.c_str(),
                                         sandook::Config::kControllerPort);
    std::cout << "Connected to controller" << '\n';

    /* Test script starts controller and disk server already, we just need to
     * mock the virtual disk registration.
     */
    constexpr auto kMyIp = "192.168.127.7";
    auto vol_reg_req = sandook::CreateRegisterVolumeMsg(
        kMyIp, sandook::Config::kStorageServerPort, kMaxSectors);
    auto payload_size = sandook::GetMsgSize(vol_reg_req.get());
    auto reg =
        client->Call(sandook::writable_span(vol_reg_req.get(), payload_size));
    if (!reg) {
      throw std::runtime_error("Cannot register volume");
    }
    auto payload = reg.get_buf();
    if (payload.size() < sizeof(sandook::RegisterVolumeReplyMsg)) {
      throw std::runtime_error("Invalid registration response");
    }
    const auto* msg = reinterpret_cast<const sandook::RegisterVolumeReplyMsg*>(
        payload.data() + sizeof(sandook::MsgHeader));
    if (msg->num_servers < 1) {
      throw std::runtime_error("No disk servers running for this test");
    }

    vol_id = msg->vol_id;

    /* Take the first server as the test server. */
    server_id = msg->servers.at(0).id;
  }

  static void TearDownTestSuite() {
    /* This is to ensure that we destroy the RPCClient and associated threads
     * before Caladan is teared down to make sure the threads can be cleaned up
     * properly.
     */
    client.reset();
    PrintAllResults(&results);
  }

  void SetUp() override {}
  void TearDown() override {}

  // NOLINTBEGIN
  static BenchResults results;
  static sandook::VolumeID vol_id;
  static sandook::ServerID server_id;
  static std::unique_ptr<sandook::RPCClient> client;
  // NOLINTEND
};

// NOLINTBEGIN
sandook::VolumeID ControllerTests::vol_id{sandook::kInvalidVolumeID};
sandook::ServerID ControllerTests::server_id{sandook::kInvalidServerID};
BenchResults ControllerTests::results({});
std::unique_ptr<sandook::RPCClient> ControllerTests::client;
// NOLINTEND

TEST_F(ControllerTests, BenchAllocateBlocks) {
  BenchArgs args{
      .vol_id = vol_id, .server_id = server_id, .client = client.get()};
  EXPECT_TRUE(Bench("BenchAllocateBlocks", BenchAllocateBlocks,
                    reinterpret_cast<void*>(&args), &results));
}
