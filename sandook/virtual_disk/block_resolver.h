#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <list>
#include <memory>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/types.h"
#include "sandook/bindings/sync.h"

namespace sandook::virtual_disk {

class BlockResolver {
 public:
  BlockResolver() = delete;

  explicit BlockResolver(uint64_t nsectors) : nsectors_(nsectors) {
    blk_map_ = std::make_unique<
        std::atomic<std::shared_ptr<ServerReplicaBlockInfoList>>[]>(nsectors);
    for (uint64_t i = 0; i < nsectors; i++) {
      blk_map_[i] = std::make_shared<ServerReplicaBlockInfoList>(
          ServerReplicaBlockInfoList{{{{kInvalidServerID}, false}}});
    }
  }

  Status<void> AddMapping(VolumeBlockAddr blk_addr,
                          ServerReplicaBlockInfoList srv_blk) {
    assert(blk_addr < nsectors_);

    DiscardExistingBlocks(blk_addr);
    blk_map_[blk_addr] = std::make_shared<ServerReplicaBlockInfoList>(srv_blk);

    return {};
  }

  [[nodiscard]] Status<ServerReplicaBlockInfoList> ResolveBlock(
      VolumeBlockAddr blk_addr) const {
    assert(blk_addr < nsectors_);

    const auto blocks = blk_map_[blk_addr].load();
    if (blocks->front().first.server_id == kInvalidServerID) {
      return MakeError(ENOENT);
    }

    return *blocks;
  }

  Status<std::list<ServerReplicaBlockInfoList>> GetAndResetDiscardedBlocks() {
    rt::MutexGuard lock(discard_list_lock_);

    if (discard_list_.empty()) {
      return MakeError(ENOENT);
    }

    auto ret_list = std::move(discard_list_);
    discard_list_.clear();

    return ret_list;
  }

 private:
  uint64_t nsectors_;
  std::unique_ptr<std::atomic<std::shared_ptr<ServerReplicaBlockInfoList>>[]>
      blk_map_;

  std::list<ServerReplicaBlockInfoList> discard_list_;
  rt::Mutex discard_list_lock_;

  void DiscardExistingBlocks(VolumeBlockAddr blk_addr) {
    auto blocks = blk_map_[blk_addr].load();
    auto [info, is_dirty] = blocks->front();

    if (is_dirty && info.server_id != kInvalidServerID) {
      /* This VolumeBlockAddr was previously allocated to different set of
       * ServerBlockAddrs; mark those previous allocation as ready to be trimmed
       * (and in the future garbage collected at the controller as well).
       *
       * TODO(girfan): Inform the controller about these trimmed blocks (perhaps
       * from the disk_server when the actual trim command has been issued) so
       * these blocks can be allocated again in the future.
       */
      rt::MutexGuard lock(discard_list_lock_);

      discard_list_.emplace_back(*blocks);
    }
  }
};

}  // namespace sandook::virtual_disk
