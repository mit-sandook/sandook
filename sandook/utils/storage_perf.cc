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

constexpr uint32_t kNumThreads = 64;
constexpr uint32_t kIntervalUs = 100 * 1000;
constexpr uint64_t kRangeBytes = 0;
constexpr uint64_t kBlockOffset = 0;
constexpr double kTargetMOPS = 0.15;
constexpr uint64_t kCachelineSize = 64;
constexpr uint64_t kBlockBytes = 4096;
constexpr uint64_t kReadPercentage = 66;
static_assert(kReadPercentage <= 100);
constexpr bool kExitOnCollapse = false;
constexpr double kEWMAAlpha = 0.15;
constexpr uint64_t kInitialWinSeconds = 2;
constexpr double kMaxErrorRatio = 0.25;
constexpr bool kDumpTraces = true;
constexpr bool kDumpOnlyReadTraces = true;
constexpr uint64_t kDumpWinIntervalUs = 5 * 1000;
constexpr uint64_t kDumpLatencyPercentile = 99;

struct alignas(kCachelineSize) {
  uint64_t c;
} cnts[kNumThreads];

struct alignas(kCachelineSize) {
  std::vector<std::pair<uint64_t, uint64_t>> t;
} traces[kNumThreads];

std::atomic<bool> done;

void dump_traces() {
  done = true;
  barrier();

  std::cout << "Dump traces to the file log..." << std::endl;
  std::ofstream ofs("log");

  std::vector<std::pair<uint64_t, uint64_t>> all_traces;
  std::vector<uint64_t> lats;
  for (auto &per_core : traces) {
    all_traces.insert(all_traces.end(), per_core.t.begin(), per_core.t.end());
  }
  std::sort(all_traces.begin(), all_traces.end());

  auto win_start_us = all_traces[0].first;
  lats.emplace_back(all_traces[0].second);
  for (auto [start_us, duration_us] : all_traces) {
    if (start_us > win_start_us + kDumpWinIntervalUs) {
      std::sort(lats.begin(), lats.end());
      ofs << win_start_us << " "
          << lats[lats.size() * kDumpLatencyPercentile / 100.0] << std::endl;
      win_start_us = start_us;
      lats.clear();
    }
    lats.emplace_back(duration_us);
  }

  exit(0);
}

void run() {
  BUG_ON(storage_block_size() != kBlockBytes);
  uint64_t total_num_blocks = storage_num_blocks() - kBlockOffset;
  if (kRangeBytes) {
    total_num_blocks = std::min(total_num_blocks, kRangeBytes / kBlockBytes);
  }
  std::cout << "total_num_blocks = " << total_num_blocks << std::endl;
  std::cout << "offset = " << kBlockOffset << std::endl;

  uint64_t per_req_us = 1.0 / (kTargetMOPS / kNumThreads);
  std::vector<rt::Thread> ths;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    ths.emplace_back([tid = i, total_num_blocks, per_req_us]() mutable {
      std::byte bytes[kBlockBytes];
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<uint64_t> block_dist(
          0 + kBlockOffset, (total_num_blocks - 1) + kBlockOffset);
      std::uniform_int_distribution<uint32_t> percent_dist(0, 99);

      while (!done) {
        barrier();
        auto start_us = microtime();
        barrier();

        bool do_read = percent_dist(gen) < kReadPercentage;
        if (do_read) {
          storage_read(bytes, block_dist(gen), 1);
        } else {
          storage_write(bytes, block_dist(gen), 1);
        }
        cnts[tid].c++;

        barrier();
        auto duration = microtime() - start_us;
        barrier();

        if constexpr (kDumpTraces) {
          if constexpr (kDumpOnlyReadTraces) {
            traces[tid].t.emplace_back(start_us, duration);
          }
        }

        if (duration < per_req_us) {
          timer_sleep(per_req_us - duration);
        }
      }
    });
  }

  uint64_t start_us = microtime();
  uint64_t last_us = 0;
  uint64_t last_sum = 0;
  double initial_mops = 0.0, ewma_mops = 0.0, cur_mops;
  while (!done) {
    uint64_t cur_us = microtime();
    uint64_t cur_sum = 0;
    for (auto &cnt : cnts) {
      cur_sum += cnt.c;
    }
    auto diff_us = cur_us - last_us;
    auto diff_sum = cur_sum - last_sum;
    cur_mops = 1.0 * diff_sum / diff_us;
    ewma_mops = (ewma_mops == 0.0)
                    ? cur_mops
                    : (1 - kEWMAAlpha) * ewma_mops + kEWMAAlpha * cur_mops;
    std::cout << diff_us << " " << diff_sum << " " << cur_mops << " "
              << ewma_mops << std::endl;

    if constexpr (kExitOnCollapse) {
      if (initial_mops == 0.0) {
        if (cur_us - start_us >= kInitialWinSeconds * 1000'000) {
          initial_mops = ewma_mops;
          std::cout << "initial_mops = " << initial_mops << std::endl;
        }
      } else if (std::abs(ewma_mops - initial_mops) / initial_mops >
                 kMaxErrorRatio) {
        std::cout << "Collapse detected!" << std::endl;
        done = true;
        break;
      }
    }

    last_us = cur_us;
    last_sum = cur_sum;
    timer_sleep(kIntervalUs);
  }

  for (auto &th : ths) {
    th.Join();
  }

  dump_traces();
}

inline void handler(int unused) { dump_traces(); }

void setup_sigint_handler() {
  struct sigaction sigint_handler;
  sigint_handler.sa_handler = handler;
  sigemptyset(&sigint_handler.sa_mask);
  sigint_handler.sa_flags = 0;
  sigaction(SIGINT, &sigint_handler, NULL);
}

int main(int argc, char **argv) {
  if constexpr (kDumpTraces) {
    setup_sigint_handler();
  }

  if (argc != 2) {
    std::cerr << "Usage: [conf]" << std::endl;
    exit(-EINVAL);
  }
  return rt::RuntimeInit(std::string(argv[1]), []() { return run(); });
}
