#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "sandook/base/server_stats.h"
#include "sandook/telemetry/disk_server_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/test_utils.h"

class TelemetryTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}
};

TEST_F(TelemetryTests, TestDiskServerTelemetryTraceBuffered) {
  auto telemetry = std::make_shared<
      sandook::TelemetryStream<sandook::DiskServerTelemetry>>();

  /* Write telemetry. */
  for (auto i = 0; i < kMeasureRounds; i++) {
    telemetry->TraceBuffered(sandook::DiskServerTelemetry(
        sandook::ServerStats{.inflight_reads = static_cast<uint32_t>(i),
                             .inflight_writes = static_cast<uint32_t>(i)}));
  }

  const auto stream_path = telemetry->get_path();
  std::cout << "Telemetry stream: " << stream_path << '\n';

  /* Close telemetry stream; ensures the output is flushed. */
  telemetry.reset();

  /* Read the output and count the number of trace lines written. */
  std::ifstream t_file(stream_path);
  int number_of_lines = 0;
  std::string line;
  while (std::getline(t_file, line)) {
    ++number_of_lines;
  }
  t_file.close();

  /* Ensure all traces have been written (including the header line). */
  EXPECT_EQ(number_of_lines, kMeasureRounds + 1);
}
