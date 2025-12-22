#include <runtime.h>
#include <storage.h>
#include <thread.h>
#include <timer.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <utility>

constexpr double kFillPercent = 0.8;
constexpr uint32_t kNumThreads = 64;
constexpr uint32_t kIntervalUs = 5 * 1000 * 1000;
constexpr double kTargetMOPS = 0.25;
constexpr uint64_t kCachelineSize = 64;
constexpr uint64_t kBlockBytes = 4096;
constexpr uint64_t kReadPercentage = 0;
static_assert(kReadPercentage <= 100);
constexpr uint64_t kInitialWinSeconds = 2;
constexpr double kMaxErrorRatio = 0.25;

struct alignas(kCachelineSize) {
  uint64_t c;
} cnts[kNumThreads];

std::atomic<uint64_t> done;

void run() {
  BUG_ON(storage_block_size() != kBlockBytes);
  uint64_t total_num_blocks = storage_num_blocks();
  uint64_t blocks_to_write = kFillPercent * total_num_blocks;

  std::cout << "total_num_blocks = " << total_num_blocks << std::endl;

  uint64_t per_req_us = 1.0 / (kTargetMOPS / kNumThreads);
  uint64_t per_th_blocks = blocks_to_write / kNumThreads;

  // Ignore any blocks lost while rounding.
  blocks_to_write = per_th_blocks * kNumThreads;

  std::vector<rt::Thread> ths;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    uint64_t start_lba = i * per_th_blocks;
    uint64_t end_lba = start_lba + per_th_blocks;

    ths.emplace_back([tid = i, total_num_blocks, per_req_us, start_lba, end_lba]() mutable {
      std::byte bytes[kBlockBytes];
      uint64_t lba = start_lba;

      while (lba < end_lba) {
        barrier();
        auto start_us = microtime();
        barrier();

        storage_write(bytes, lba, 1);
        cnts[tid].c++;

        barrier();
        auto duration = microtime() - start_us;
        barrier();

        if (duration < per_req_us) {
          timer_sleep(per_req_us - duration);
        }

        lba++;
      }

      done.fetch_add(1);
    });
  }

  uint64_t last_us = 0;
  uint64_t last_sum = 0;
  double cur_mops;
  while (done.load() < kNumThreads) {
    uint64_t cur_us = microtime();
    uint64_t cur_sum = 0;
    for (auto &cnt : cnts) {
      cur_sum += cnt.c;
    }
    auto cur_pct = cur_sum * 100.0 / blocks_to_write;
    auto diff_us = cur_us - last_us;
    auto diff_sum = cur_sum - last_sum;
    cur_mops = 1.0 * diff_sum / diff_us;
    std::cout << cur_sum << "/" << blocks_to_write << " = " << cur_pct << "% at " << cur_mops << " MOPS" << std::endl;

    last_us = cur_us;
    last_sum = cur_sum;
    timer_sleep(kIntervalUs);
  }

  for (auto &th : ths) {
    th.Join();
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: [conf]" << std::endl;
    exit(-EINVAL);
  }
  return rt::RuntimeInit(std::string(argv[1]), []() { return run(); });
}
