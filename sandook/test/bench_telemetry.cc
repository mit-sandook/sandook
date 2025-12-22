#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include "sandook/base/server_stats.h"
#include "sandook/telemetry/disk_server_telemetry.h"
#include "sandook/telemetry/telemetry_stream.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT
#include "sandook/test/utils/test_utils.h"

inline constexpr auto kTelemetryPath = "/dev/shm/sandook/disk_server_default";

class TelemetryTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {}
  static void TearDownTestSuite() { PrintAllResults(&results); }

  /* All tests add their timing measurements to this data structure. */
  static BenchResults results;  // NOLINT
};

BenchResults TelemetryTests::results({});  // NOLINT

struct BenchDiskServerTelemetryTraceArgs {
  std::shared_ptr<sandook::TelemetryStream<sandook::DiskServerTelemetry>>
      telemetry;
};

bool BenchDiskServerTelemetryTrace(int measure_rounds,
                                   [[maybe_unused]] void *args_ptr) {
  auto *args = reinterpret_cast<BenchDiskServerTelemetryTraceArgs *>(args_ptr);

  for (auto i = 0; i < measure_rounds; i++) {
    args->telemetry->Trace(
        sandook::DiskServerTelemetry(sandook::ServerStats{}));
  }

  return true;
}

bool BenchDiskServerTelemetryTraceBuffered(int measure_rounds,
                                           [[maybe_unused]] void *args_ptr) {
  auto *args = reinterpret_cast<BenchDiskServerTelemetryTraceArgs *>(args_ptr);

  for (auto i = 0; i < measure_rounds; i++) {
    args->telemetry->TraceBuffered(
        sandook::DiskServerTelemetry(sandook::ServerStats{}));
  }

  return true;
}

TEST_F(TelemetryTests, BenchDiskServerTelemetryTrace) {
  auto telemetry = std::make_shared<
      sandook::TelemetryStream<sandook::DiskServerTelemetry>>();
  BenchDiskServerTelemetryTraceArgs args{.telemetry = telemetry};

  EXPECT_TRUE(Bench("BenchDiskServerTelemetryTrace",
                    BenchDiskServerTelemetryTrace,
                    reinterpret_cast<void *>(&args), &results));
}

TEST_F(TelemetryTests, BenchDiskServerTelemetryTraceBuffered) {
  auto telemetry = std::make_shared<
      sandook::TelemetryStream<sandook::DiskServerTelemetry>>();
  BenchDiskServerTelemetryTraceArgs args{.telemetry = telemetry};

  EXPECT_TRUE(Bench("BenchDiskServerTelemetryTraceBuffered",
                    BenchDiskServerTelemetryTraceBuffered,
                    reinterpret_cast<void *>(&args), &results));
}

TEST_F(TelemetryTests, TestDiskServerTelemetryTraceBuffered) {
  auto telemetry = std::make_shared<
      sandook::TelemetryStream<sandook::DiskServerTelemetry>>();

  /* Write telemetry. */
  for (auto i = 0; i < kMeasureRounds; i++) {
    telemetry->TraceBuffered(sandook::DiskServerTelemetry(
        sandook::ServerStats{.inflight_reads = static_cast<uint32_t>(i),
                             .inflight_writes = static_cast<uint32_t>(i)}));
  }

  /* Close telemetry stream; ensures the output is flushed. */
  telemetry.reset();

  /* Read the output and count the number of trace lines written. */
  std::ifstream t_file(kTelemetryPath);
  int number_of_lines = 0;
  std::string line;
  while (std::getline(t_file, line)) {
    ++number_of_lines;
  }
  t_file.close();

  /* Ensure all traces have been written (including the header line). */
  EXPECT_EQ(number_of_lines, kMeasureRounds + 1);
}
