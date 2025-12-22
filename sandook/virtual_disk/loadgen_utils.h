#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "sandook/base/constants.h"
#include "sandook/base/core_local_cache.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/virtual_disk/virtual_disk.h"

namespace sandook {

constexpr auto kPayloadSize = 1 << kSectorShift;

namespace detail {

using Completer = std::function<int(int)>;

struct FillIOResult {
  Completer complete;
  int id{};
};

struct IOResult {
  Callback user_cb;
  void *user_cb_args;
};

struct IOReqContext {
  IOReqContext()
      : payloads(std::make_unique_for_overwrite<std::byte[]>(kPayloadSize)) {}
  std::unique_ptr<std::byte[]> payloads;
  IOResult io_result;
  CoreLocalCache<IOReqContext> *owner;
};

}  // namespace detail

class LoadGenUtils {
 public:
  explicit LoadGenUtils(int n_sectors);
  ~LoadGenUtils() = default;

  LoadGenUtils(const LoadGenUtils &) = delete;
  LoadGenUtils &operator=(const LoadGenUtils &) = delete;
  LoadGenUtils(LoadGenUtils &&) = delete;
  LoadGenUtils &operator=(LoadGenUtils &&) = delete;

  Status<void> SubmitRead(uint64_t sector, Callback cb, void *cb_args);
  Status<void> SubmitWrite(uint64_t sector, Callback cb, void *cb_args);

 private:
  std::unique_ptr<VirtualDisk> vdisk_;

  CoreLocalCache<detail::IOReqContext> read_req_ctxs_;
  CoreLocalCache<detail::IOReqContext> write_req_ctxs_;

  Status<void> AllocateBlocksInVirtualDisk();
  Status<void> FillVirtualDisk();
};

}  // namespace sandook
