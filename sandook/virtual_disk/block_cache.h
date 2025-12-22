#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <vector>

#include "sandook/base/core_local_cache.h"
#include "sandook/base/error.h"
#include "sandook/base/types.h"
#include "sandook/bindings/sync.h"

namespace sandook::virtual_disk {

constexpr auto kPerCoreCachedBlocks = kAllocationBatch;

class BlockCache {
 public:
  BlockCache() : cache_(kPerCoreCachedBlocks) {
    cache_.reserve(rt::RuntimeMaxCores() * kPerCoreCachedBlocks);
  }
  ~BlockCache() = default;

 private:
  CoreLocalCache<ServerBlockInfo> cache_;
};

}  // namespace sandook::virtual_disk
