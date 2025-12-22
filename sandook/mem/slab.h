#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

#include "sandook/base/compiler.h"
#include "sandook/base/constants.h"
#include "sandook/bindings/sync.h"

class SlabTests;  // For testing.

namespace sandook {

namespace detail {

struct PtrHeader {
  uint64_t size;
  uint64_t core_id;
};

// It's a constraint placed by GCC for enabling vectorization optimizations.
constexpr static uint32_t kMemAlignment = 16;
static_assert(sizeof(PtrHeader) % kMemAlignment == 0);

template <uint32_t kMinObjectSize>
class FreePtrsList {
 public:
  void push(void *ptr);
  void *pop();
  uint64_t size();

 private:
  constexpr static uint32_t kBatchSize =
      (kMinObjectSize + sizeof(PtrHeader)) / sizeof(void *);
  struct Batch {
    void *ptrs[kBatchSize];
  };

  Batch *head_ = nullptr;
  uint64_t size_ = 0;
};
}  // namespace detail

class SlabAllocator {
 public:
  constexpr static uint64_t kMaxSlabClassShift = 35;     // 32 GB.
  constexpr static uint64_t kMinSlabClassShift = 5;      // 32 B.
  constexpr static uint64_t kMaxCacheSizeBytes = 32768;  // 8 pages.
  constexpr static uint64_t kMaxCacheEntries = 64;
  constexpr static bool kEnableTransferCache = false;
  static_assert((1 << kMinSlabClassShift) % detail::kMemAlignment == 0);

  explicit SlabAllocator(std::span<std::byte> mem);
  void *allocate(size_t size);
  void free(const void *ptr);
  void *reallocate(const void *ptr, size_t size);
  bool if_own(const void *ptr);

 private:
  constexpr static uint32_t get_slab_shift(uint64_t data_size);
  constexpr static uint64_t get_slab_size(uint32_t slab_shift);
  constexpr static uint32_t get_max_num_cache_entries(uint32_t slab_shift);
  void do_free(const rt::Preempt &p, detail::PtrHeader *hdr,
               uint32_t slab_shift);
  void free_to_cache_list(const rt::Preempt &p, detail::PtrHeader *hdr,
                          uint32_t slab_shift);
  void free_to_transferred_cache_list(detail::PtrHeader *hdr,
                                      uint32_t slab_shift);
  void drain_transferred_cache(const rt::Preempt &p, uint32_t slab_shift);

  friend class ::SlabTests;
  using List = detail::FreePtrsList<1 << kMinSlabClassShift>;
  struct alignas(kCacheLineSizeBytes) CoreCache {
    List lists[kMaxSlabClassShift + 1];
  };
  struct alignas(kCacheLineSizeBytes) TransferredCoreCache {
    rt::Spin spin;
    List lists[kMaxSlabClassShift + 1];
  };

  const std::byte *start_;
  const std::byte *end_;
  std::byte *cur_;
  List slab_lists_[kMaxSlabClassShift + 1];
  CoreCache cache_lists_[kMaxNumCores];
  TransferredCoreCache transferred_caches_[kMaxNumCores];
  rt::Spin spin_;
};

constexpr uint32_t SlabAllocator::get_slab_shift(uint64_t data_size) {
  return data_size <= (1ULL << kMinSlabClassShift) ? kMinSlabClassShift
                                                   : bsr_64(data_size - 1) + 1;
}

constexpr uint64_t SlabAllocator::get_slab_size(uint32_t slab_shift) {
  return (1ULL << slab_shift) + sizeof(detail::PtrHeader);
}

constexpr uint32_t SlabAllocator::get_max_num_cache_entries(
    uint32_t slab_shift) {
  return std::min(kMaxCacheEntries, kMaxCacheSizeBytes / (1 << slab_shift));
}

}  // namespace sandook
