// thread.h - Support for creating and managing threads

#pragma once

#include <atomic>
#include <tuple>
#include <type_traits>
#include <utility>

extern "C" {
#include <base/assert.h>
#include <base/compiler.h>
#include <runtime/thread.h>
}

#include "sandook/bindings/sync.h"

namespace sandook::rt {
namespace thread_internal {

struct basic_data {
  virtual ~basic_data() = default;
  virtual void Run() = 0;
};

struct join_data {
  join_data() noexcept = default;
  virtual ~join_data() = default;
  virtual void Run() = 0;
  rt::Spin lock;
  rt::ThreadWaker waker;
  std::atomic_bool done{false};
};

template <typename Data, typename Callable, typename... Args>
class Wrapper : public Data {
 public:
  explicit Wrapper(Callable&& func, Args&&... args) noexcept  // NOLINT
      : func_(std::forward<Callable>(func)),
        args_{std::forward<Args>(args)...} {}
  ~Wrapper() override = default;

  void Run() override { std::apply(func_, args_); }

 private:
  std::decay_t<Callable> func_;
  std::tuple<std::decay_t<Args>...> args_;
};

extern "C" void ThreadTrampoline(void* arg);
extern "C" void ThreadTrampolineWithJoin(void* arg);

template <typename Callable, typename... Args>
using ret_t =
    std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>;

}  // namespace thread_internal

// Spawns a new thread.
template <typename Callable, typename... Args>
void Spawn(Callable&& func, Args&&... args)
  requires std::invocable<Callable, Args...>
{
  void* buf = nullptr;
  using Data = thread_internal::basic_data;
  using Wrapper = thread_internal::Wrapper<Data, Callable, Args...>;
  thread_t* th = thread_create_with_buf(thread_internal::ThreadTrampoline, &buf,
                                        sizeof(Wrapper));
  if (unlikely(!th)) {
    BUG();
  }
  new (buf) Wrapper(std::forward<Callable>(func), std::forward<Args>(args)...);
  thread_ready(th);
}

// A RAII thread object, similar to a std::jthread.
class Thread {
 public:
  // boilerplate constructors.
  Thread() noexcept = default;
  ~Thread() {
    if (Joinable()) {
      Join();
    }
  }

  // disable copy.
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  // Move support.
  Thread(Thread&& t) noexcept : join_data_(t.join_data_) {
    t.join_data_ = nullptr;
  }
  Thread& operator=(Thread&& t) noexcept {
    join_data_ = t.join_data_;
    t.join_data_ = nullptr;
    return *this;
  }

  // Spawns a thread that runs the callable with the supplied arguments.
  template <typename Callable, typename... Args>
  Thread(Callable&& func, Args&&... args)  // NOLINT
    requires std::invocable<Callable, Args...>;

  // Can the thread be joined?
  [[nodiscard]] bool Joinable() const { return join_data_ != nullptr; }

  // Waits for the thread to exit.
  void Join();

  // Detaches the thread, indicating it won't be joined in the future.
  void Detach();

 private:
  thread_internal::join_data* join_data_{nullptr};
};

template <typename Callable, typename... Args>
inline Thread::Thread(Callable&& func, Args&&... args)
  requires std::invocable<Callable, Args...>
{
  using Data = thread_internal::join_data;
  using Wrapper = thread_internal::Wrapper<Data, Callable, Args...>;
  Wrapper* buf = nullptr;
  thread_t* th =
      thread_create_with_buf(thread_internal::ThreadTrampolineWithJoin,
                             reinterpret_cast<void**>(&buf), sizeof(*buf));
  if (unlikely(!th)) {
    BUG();
  }
  new (buf) Wrapper(std::forward<Callable>(func), std::forward<Args>(args)...);
  join_data_ = buf;
  thread_ready(th);
}

}  // namespace sandook::rt
