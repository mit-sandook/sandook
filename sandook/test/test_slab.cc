#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/mem/slab.h"
#include "sandook/test/utils/gtest/main_wrapper.h"  // NOLINT

class SlabTests : public ::testing::Test {
 protected:
  constexpr static uint64_t kBufSize = 64ULL << 20;
  constexpr static uint64_t kObjectSize = sandook::SlabAllocator::get_slab_size(
      sandook::SlabAllocator::kMinSlabClassShift);
  constexpr static uint64_t kDataSize =
      kObjectSize - sizeof(sandook::detail::PtrHeader);

  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}
};

TEST_F(SlabTests, TestSlabSingleThread) {
  sandook::rt::Preempt p;
  sandook::rt::PreemptGuard const g(p);

  auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
  auto slab = std::make_unique<sandook::SlabAllocator>(*buf);

  void *ptr = nullptr;
  std::set<void *> ptrs;
  while ((ptr = slab->allocate(kDataSize)) != nullptr) {
    ptrs.emplace(ptr);
  }

  std::set<void *> expected_ptrs;
  for (auto *ptr = std::addressof(*buf.get()->begin());
       ptr + kObjectSize <= std::addressof(*buf.get()->end());
       ptr += kObjectSize) {
    expected_ptrs.emplace(ptr + sizeof(sandook::detail::PtrHeader));
  }
  EXPECT_EQ(ptrs, expected_ptrs);

  for (auto iter = ptrs.begin(); iter != ptrs.end();) {
    slab->free(*iter);
    iter = ptrs.erase(iter);
  }
  while ((ptr = slab->allocate(kDataSize)) != nullptr) {
    ptrs.emplace(ptr);
  }
  EXPECT_EQ(ptrs, expected_ptrs);
}

TEST_F(SlabTests, TestSlabMultiThreads) {
  constexpr uint32_t kNumThreads = 8;
  std::set<void *> ptrs[kNumThreads];
  std::set<void *> all_ptrs;

  auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
  auto slab = std::make_unique<sandook::SlabAllocator>(*buf);

  auto multithread_alloc_fn = [&] {
    std::vector<sandook::rt::Thread> threads;

    for (uint32_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back([slab = slab.get(), ptrs = &ptrs[i]] {
        void *ptr = nullptr;
        while ((ptr = slab->allocate(kDataSize)) != nullptr) {
          ptrs->emplace(ptr);
        }
      });
    }

    for (auto &thread : threads) {
      thread.Join();
    }

    all_ptrs.clear();
    for (auto &p : ptrs) {
      all_ptrs.insert(p.begin(), p.end());
    }
  };

  auto multithread_free_fn = [&] {
    std::vector<sandook::rt::Thread> threads;

    for (uint32_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back([slab = slab.get(), ptrs = &ptrs[i]] {
        for (auto iter = ptrs->begin(); iter != ptrs->end();) {
          slab->free(*iter);
          iter = ptrs->erase(iter);
        }
      });
    }

    for (auto &thread : threads) {
      thread.Join();
    }
  };

  multithread_alloc_fn();

  std::set<void *> expected_ptrs;
  for (auto *ptr = std::addressof(*buf.get()->begin());
       ptr + kObjectSize <= std::addressof(*buf.get()->end());
       ptr += kObjectSize) {
    expected_ptrs.emplace(ptr + sizeof(sandook::detail::PtrHeader));
  }
  EXPECT_EQ(all_ptrs, expected_ptrs);

  multithread_free_fn();
  multithread_alloc_fn();
  EXPECT_EQ(all_ptrs, expected_ptrs);
}
