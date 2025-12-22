#include "sandook/mem/slab.h"

extern "C" {
#include <base/assert.h>
}

namespace sandook::detail {

template <uint32_t kMinObjectSize>
uint64_t FreePtrsList<kMinObjectSize>::size() {
  return size_;
}

template <uint32_t kMinObjectSize>
void *FreePtrsList<kMinObjectSize>::pop() {
  void *ret;

  size_--;
  BUG_ON(!head_);

  for (uint32_t i = kBatchSize - 1; i > 0; i--) {
    if ((ret = head_->ptrs[i])) {
      head_->ptrs[i] = nullptr;
      return ret;
    }
  }
  ret = head_;
  head_ = reinterpret_cast<Batch *>(head_->ptrs[0]);
  return ret;
}

template <uint32_t kMinObjectSize>
void FreePtrsList<kMinObjectSize>::push(void *ptr) {
  size_++;

  if (unlikely(!head_)) {
    head_ = reinterpret_cast<Batch *>(ptr);
    std::fill(std::begin(head_->ptrs), std::end(head_->ptrs), nullptr);
    return;
  }

  for (uint32_t i = 1; i < kBatchSize; i++) {
    if (!head_->ptrs[i]) {
      head_->ptrs[i] = ptr;
      return;
    }
  }

  auto *old_head = head_;
  head_ = reinterpret_cast<Batch *>(ptr);
  head_->ptrs[0] = old_head;
  std::fill(std::begin(head_->ptrs) + 1, std::end(head_->ptrs), nullptr);
}
}  // namespace sandook::detail

namespace sandook {

SlabAllocator::SlabAllocator(std::span<std::byte> mem) {
  start_ = cur_ = std::addressof(*mem.begin());
  end_ = std::addressof(*mem.end());
}

void *SlabAllocator::allocate(size_t size) {
  void *ret = nullptr;
  auto slab_shift = get_slab_shift(size);

  // Handle malloc(0) and the oversized requests.
  if (unlikely(!size || slab_shift > kMaxSlabClassShift)) {
    return nullptr;
  }

  rt::Preempt p;
  rt::PreemptGuard g(p);

  // Check the transferred cache.
  drain_transferred_cache(p, slab_shift);
  auto &cache_list = cache_lists_[p.get_cpu()].lists[slab_shift];

  if (likely(cache_list.size())) {
    // Fast path -- pop from the per-core cache pool directly.
    ret = cache_list.pop();
  } else {
    // Slow path -- pop from the global pool.
    rt::ScopedLock lock(spin_);

    auto &slab_list = slab_lists_[slab_shift];
    auto max_num_cache_entries = get_max_num_cache_entries(slab_shift);
    // Need at least one entry to serve the allocation.
    max_num_cache_entries =
        std::max(max_num_cache_entries, static_cast<uint32_t>(1));

    while (slab_list.size() && cache_list.size() < max_num_cache_entries) {
      cache_list.push(slab_list.pop());
    }

    auto remaining = max_num_cache_entries - cache_list.size();
    if (remaining) {
      // The global pool is empty, but we still need more entries.
      // Bump the break in this case.
      auto slab_size = get_slab_size(slab_shift);
      for (uint32_t i = 0; i < remaining && cur_ + slab_size <= end_; i++) {
        cache_list.push(cur_);
        cur_ += slab_size;
      }
    }

    if (likely(cache_list.size())) {
      ret = cache_list.pop();
    }
  }

  if (ret) {
    // Take care of the pointer metadata.
    auto *hdr = reinterpret_cast<detail::PtrHeader *>(ret);
    hdr->size = size;
    hdr->core_id = p.get_cpu();
    auto addr = reinterpret_cast<uintptr_t>(ret);
    addr += sizeof(detail::PtrHeader);
    BUG_ON(addr % detail::kMemAlignment != 0);
    ret = reinterpret_cast<void *>(addr);
  }

  return ret;
}

void SlabAllocator::free(const void *ptr) {
  // Handle free(malloc(0)).
  if (unlikely(!ptr)) {
    return;
  }

  auto *byte_ptr = reinterpret_cast<const std::byte *>(ptr);
  BUG_ON(byte_ptr < start_);
  BUG_ON(byte_ptr >= cur_);

  auto *hdr = reinterpret_cast<detail::PtrHeader *>(
      reinterpret_cast<uintptr_t>(ptr) - sizeof(detail::PtrHeader));
  auto size = hdr->size;
  auto slab_shift = get_slab_shift(size);
  BUG_ON(slab_shift < kMinSlabClassShift || slab_shift > kMaxSlabClassShift);

  rt::Preempt p;
  rt::PreemptGuard g(p);

  do_free(p, hdr, slab_shift);
}

void SlabAllocator::do_free(const rt::Preempt &p, detail::PtrHeader *hdr,
                            uint32_t slab_shift) {
  // Check the transferred cache.
  drain_transferred_cache(p, slab_shift);

  if constexpr (kEnableTransferCache) {
    if (likely(p.get_cpu() == hdr->core_id)) {
      // Free to the local cache pool.
      free_to_cache_list(p, hdr, slab_shift);
    } else {
      // Free to the remote cache pool.
      free_to_transferred_cache_list(hdr, slab_shift);
    }
  } else {
    // Directly free to the local cache pool if the transfer cache is disabled.
    free_to_cache_list(p, hdr, slab_shift);
  }
}

void *SlabAllocator::reallocate(const void *ptr, size_t new_size) {
  auto *byte_ptr = reinterpret_cast<const std::byte *>(ptr);
  BUG_ON(byte_ptr < start_);
  BUG_ON(byte_ptr >= cur_);

  auto *hdr = reinterpret_cast<detail::PtrHeader *>(
      reinterpret_cast<uintptr_t>(ptr) - sizeof(detail::PtrHeader));
  auto size = hdr->size;
  auto slab_shift = get_slab_shift(size);

  auto *new_ptr = allocate(new_size);
  if (unlikely(!new_ptr)) {
    return nullptr;
  }
  memcpy(new_ptr, ptr, std::min(size, new_size));

  rt::Preempt p;
  rt::PreemptGuard g(p);

  do_free(p, hdr, slab_shift);

  return new_ptr;
}

bool SlabAllocator::if_own(const void *ptr) {
  return start_ <= ptr && ptr < end_;
}

void SlabAllocator::drain_transferred_cache(const rt::Preempt &p,
                                            uint32_t slab_shift) {
  if constexpr (kEnableTransferCache) {
    auto &transferred_cache = transferred_caches_[p.get_cpu()];
    auto &list = transferred_cache.lists[slab_shift];

    if (list.size()) {
      rt::ScopedLock l(transferred_cache.spin);

      while (list.size()) {
        auto *hdr = reinterpret_cast<detail::PtrHeader *>(list.pop());
        free_to_cache_list(p, hdr, slab_shift);
      }
    }
  }
}

void SlabAllocator::free_to_cache_list(const rt::Preempt &p,
                                       detail::PtrHeader *hdr,
                                       uint32_t slab_shift) {
  auto max_num_cache_entries = get_max_num_cache_entries(slab_shift);
  auto &cache_list = cache_lists_[p.get_cpu()].lists[slab_shift];
  cache_list.push(hdr);

  if (unlikely(cache_list.size() > max_num_cache_entries)) {
    // Slow path -- move half of the entries into the global pool.
    auto &slab_list = slab_lists_[slab_shift];
    rt::ScopedLock lock(spin_);

    while (cache_list.size() > max_num_cache_entries / 2) {
      slab_list.push(cache_list.pop());
    }
  }
}

void SlabAllocator::free_to_transferred_cache_list(detail::PtrHeader *hdr,
                                                   uint32_t slab_shift) {
  auto max_num_cache_entries = get_max_num_cache_entries(slab_shift);
  auto &transferred_cache = transferred_caches_[hdr->core_id];
  auto &transferred_cache_list = transferred_cache.lists[slab_shift];
  auto &cache_list = cache_lists_[hdr->core_id].lists[slab_shift];

  rt::ScopedLock lock(transferred_cache.spin);
  transferred_cache.lists[slab_shift].push(hdr);

  auto total_num = transferred_cache_list.size() + cache_list.size();
  if (unlikely(total_num > max_num_cache_entries)) {
    //  Slow path -- move half of the entries into the global pool.
    auto num_to_turn_in = std::min(transferred_cache_list.size(),
                                   total_num - max_num_cache_entries / 2);
    auto &slab_list = slab_lists_[slab_shift];
    rt::ScopedLock lock(spin_);

    while (num_to_turn_in--) {
      slab_list.push(transferred_cache_list.pop());
    }
  }
}

}  // namespace sandook
