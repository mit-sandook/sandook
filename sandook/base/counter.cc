#include "sandook/base/counter.h"

#include "sandook/bindings/sync.h"

namespace sandook {

ThreadSafeCounter::ThreadSafeCounter() {
  for (auto &cnt : cnts_) {
    cnt.c = 0;
  }
  delta_ = 0;
}

void ThreadSafeCounter::inc_local(int64_t delta) {
  rt::Preempt p;
  rt::PreemptGuard g(p);

  cnts_[p.get_cpu()].c += delta;
}

void ThreadSafeCounter::dec_local(int64_t delta) {
  rt::Preempt p;
  rt::PreemptGuard g(p);

  cnts_[p.get_cpu()].c -= delta;
}

int64_t ThreadSafeCounter::get_sum() {
  int64_t sum = 0;
  for (auto &cnt : cnts_) {
    sum += cnt.c;
  }

  return sum + delta_;
}

int64_t ThreadSafeCounter::get_sum_and_reset() {
  auto sum = get_sum();
  delta_ += -sum;

  return sum;
}

}  // namespace sandook
