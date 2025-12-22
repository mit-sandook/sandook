#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stack>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/bindings/sync.h"

namespace sandook {

template <typename T>
class CoreLocalCache {
 public:
  CoreLocalCache(
      std::size_t per_core_capacity,
      std::function<std::vector<T *>(void)> new_fn =
          []() { return std::vector<T *>(1, new T()); },
      std::function<void(T *)> delete_fn = [](T *t) { delete t; });
  ~CoreLocalCache();
  T *get();
  void put(T *item);
  void reserve(size_t global_size);

 private:
  using ItemStack = std::stack<T *, std::vector<T *>>;

  struct alignas(kCacheLineSizeBytes) LocalCache {
    ItemStack items;
  };

  std::size_t per_core_capacity_;
  std::function<std::vector<T *>(void)> new_fn_;
  std::function<void(T *)> delete_fn_;
  LocalCache locals_[kMaxNumCores];
  ItemStack global_;
  rt::Spin global_spin_;

  T *get_slow_path();
  void put_slow_path(LocalCache *local);
};

template <typename T>
CoreLocalCache<T>::CoreLocalCache(std::size_t per_core_capacity,
                                  std::function<std::vector<T *>(void)> new_fn,
                                  std::function<void(T *)> delete_fn)
    : per_core_capacity_(per_core_capacity),
      new_fn_(std::move(new_fn)),
      delete_fn_(std::move(delete_fn)) {}

template <typename T>
CoreLocalCache<T>::~CoreLocalCache() {
  for (auto &local : locals_) {
    while (!local.items.empty()) {
      delete_fn_(local.items.top());
      local.items.pop();
    }
  }

  while (!global_.empty()) {
    delete_fn_(global_.top());
    global_.pop();
  }
}

template <typename T>
T *CoreLocalCache<T>::get() {
  {
    rt::Preempt p;
    rt::PreemptGuard g(p);

    auto cpu = p.get_cpu();
    auto &local = locals_[cpu];

    if (likely(!local.items.empty())) {
      auto *ret = local.items.top();
      local.items.pop();
      return ret;
    }
  }

  return get_slow_path();
}

template <typename T>
T *CoreLocalCache<T>::get_slow_path() {
again:
  {
    rt::Preempt p;
    rt::PreemptGuard g(p);
    auto cpu = p.get_cpu();
    auto &local = locals_[cpu];

    {
      rt::SpinGuard g(global_spin_);

      while (!global_.empty() && local.items.size() < per_core_capacity_) {
        local.items.push(global_.top());
        global_.pop();
      }
    }

    if (likely(!local.items.empty())) {
      auto *ret = local.items.top();
      local.items.pop();
      return ret;
    }
  }

  reserve(per_core_capacity_);
  goto again;
}

template <typename T>
void CoreLocalCache<T>::put(T *item) {
  rt::Preempt p;
  rt::PreemptGuard g(p);

  auto cpu = p.get_cpu();
  auto &local = locals_[cpu];
  local.items.push(item);

  if (unlikely(local.items.size() > per_core_capacity_)) {
    put_slow_path(&local);
  }
}

template <typename T>
void CoreLocalCache<T>::put_slow_path(LocalCache *local) {
  rt::SpinGuard g(global_spin_);

  while (local->items.size() > std::max(per_core_capacity_ / 2, 1UZ)) {
    global_.push(local->items.top());
    local->items.pop();
  }
}

template <typename T>
void CoreLocalCache<T>::reserve(std::size_t global_size) {
  std::vector<T *> items;
  items.reserve(global_size);
  while (items.size() < global_size) {
    auto new_items = new_fn_();
    items.insert(items.end(), new_items.begin(), new_items.end());
  }

  {
    rt::SpinGuard g(global_spin_);

    for (auto *item : items) {
      global_.push(item);
    }
  }
}

}  // namespace sandook
