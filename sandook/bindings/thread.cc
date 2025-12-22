#include "sandook/bindings/thread.h"

#include <atomic>
#include <cassert>
#include <memory>

#include "sandook/base/finally.h"
#include "sandook/bindings/sync.h"

namespace sandook::rt {
namespace thread_internal {

// A helper to jump from extern C back to C++ for rt::Spawn().
void ThreadTrampoline(void* arg) {
  auto* d = static_cast<basic_data*>(arg);
  d->Run();
  std::destroy_at(d);
}

// A helper to jump from extern C back to C++ for Thread::Thread().
void ThreadTrampolineWithJoin(void* arg) {
  auto* d = static_cast<join_data*>(arg);
  auto f = finally([d] { std::destroy_at(d); });
  d->Run();

  d->lock.Lock();
  if (d->done.load(std::memory_order_relaxed)) {
    d->waker.Wake();
    d->lock.Unlock();
    return;
  }
  d->waker.Arm();
  d->done.store(true, std::memory_order_release);
  d->lock.UnlockAndPark();
}

}  // namespace thread_internal

void Thread::Detach() {
  assert(join_data_ != nullptr);
  auto* d = join_data_;
  auto f = finally([this] { join_data_ = nullptr; });

  {
    const rt::SpinGuard g(d->lock);
    if (d->done.load(std::memory_order_relaxed)) {
      d->waker.Wake();
      return;
    }
    d->done.store(true, std::memory_order_release);
  }
}

void Thread::Join() {
  assert(join_data_ != nullptr);
  auto* d = join_data_;
  auto f = finally([this] { join_data_ = nullptr; });

  d->lock.Lock();
  if (d->done.load(std::memory_order_relaxed)) {
    d->lock.Unlock();
    d->waker.Wake();
    return;
  }
  d->waker.Arm();
  d->done.store(true, std::memory_order_release);
  d->lock.UnlockAndPark();
}

}  // namespace sandook::rt
