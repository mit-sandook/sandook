#include <dlfcn.h>
#include <signal.h>
#include <sys/mman.h>

#include "sandook/bindings/runtime.h"
#include "sandook/bindings/sync.h"
#include "sandook/mem/slab.h"

namespace {

constexpr uint64_t kHeapSize = 64ULL << 30;  // 64 GiB

using MallocFn = void *(*)(std::size_t);
using FreeFn = void (*)(void *);
using ReallocFn = void *(*)(void *, std::size_t);

inline MallocFn get_real_malloc() {
  static MallocFn real_malloc;

  if (unlikely(!real_malloc)) {
    real_malloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
  }
  return real_malloc;
}

inline FreeFn get_real_free() {
  static FreeFn real_free;

  if (unlikely(!real_free)) {
    real_free = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
  }
  return real_free;
}

inline ReallocFn get_real_realloc() {
  static ReallocFn real_realloc;

  if (unlikely(!real_realloc)) {
    real_realloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
  }
  return real_realloc;
}

inline sandook::SlabAllocator &get_global_allocator() {
  static auto *mmap_ptr = reinterpret_cast<std::byte *>(
      mmap(nullptr, kHeapSize, PROT_READ | PROT_WRITE,
           MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0));
  static sandook::SlabAllocator slab(std::span(mmap_ptr, kHeapSize));
  return slab;
}

inline void *__new(std::size_t size) {
  auto &slab = get_global_allocator();
  if (likely(sandook::rt::RuntimeInitialized())) {
    return slab.allocate(size);
  } else {
    return get_real_malloc()(size);
  }
}

inline void __delete(void *ptr) {
  auto &slab = get_global_allocator();
  if (likely(slab.if_own(ptr))) {
    if (likely(sandook::rt::RuntimeInitialized())) {
      slab.free(ptr);
    } else {
      // This only occurs when freeing memory of global/static variables, which
      // happens after tearing down the caladan runtime. It's okay to do nothing
      // now as the program is about to finish.
    }
  } else {
    get_real_free()(ptr);
  }
}

inline void *__realloc(void *ptr, std::size_t size) {
  auto &slab = get_global_allocator();
  if (likely(sandook::rt::RuntimeInitialized() && slab.if_own(ptr))) {
    return slab.reallocate(ptr, size);
  } else {
    return get_real_realloc()(ptr, size);
  }
}

inline void *__new_aligned(std::size_t size, std::align_val_t al) {
  size += sizeof(size_t);
  auto align = static_cast<std::size_t>(al);
  auto *ptr = __new(size + align - 1);
  auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  auto aligned_ptr_addr =
      (ptr_addr + sizeof(size_t) + align - 1) & (~(align - 1));
  auto *header = reinterpret_cast<void **>(aligned_ptr_addr) - 1;
  *header = ptr;
  return reinterpret_cast<void *>(aligned_ptr_addr);
}

inline void __delete_aligned(void *ptr) {
  auto *header = reinterpret_cast<void **>(ptr) - 1;
  __delete(*header);
}

}  // namespace

void *operator new(std::size_t size) throw() {
  auto *ptr = __new(size);
  if (size && !ptr) raise(SIGABRT);
  BUG_ON(size && !ptr);
  return ptr;
}

void *operator new(std::size_t size,
                   const std::nothrow_t &nothrow_value) noexcept {
  return __new(size);
}

void *operator new(std::size_t size, std::align_val_t al) {
  return __new_aligned(size, al);
}

void *operator new(std::size_t size, std::align_val_t al,
                   const std::nothrow_t &) {
  return __new_aligned(size, al);
}

void operator delete(void *ptr) noexcept { __delete(ptr); }

void operator delete(void *ptr, std::align_val_t al) noexcept {
  __delete_aligned(ptr);
}

void *malloc(std::size_t size) { return __new(size); }

void free(void *ptr) { __delete(ptr); }

void *realloc(void *ptr, std::size_t size) { return __realloc(ptr, size); }
