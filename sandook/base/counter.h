#pragma once

#include <cstdint>

#include "sandook/base/constants.h"

namespace sandook {

class ThreadSafeCounter {
 public:
  ThreadSafeCounter();
  void inc_local(int64_t delta = 1);
  void dec_local(int64_t delta = 1);
  int64_t get_sum();
  int64_t get_sum_and_reset();

 private:
  struct alignas(kCacheLineSizeBytes) {
    int64_t c;
  } cnts_[kMaxNumCores];

  int64_t delta_;
};

}  // namespace sandook
