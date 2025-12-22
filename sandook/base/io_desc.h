#pragma once

#include <cstdint>
#include <type_traits>

#include "sandook/base/io_callback.h"

namespace sandook {

constexpr static auto kOpMask = 0xff;
constexpr static auto kFlagShift = 8;

using CallbackArgs = void *;
using Callback = void (*)(CallbackArgs, IOResult);

enum class OpType : unsigned {
  kRead = 0,
  kWrite = 1,
  kFlush = 2,
  kDiscard = 3,
  kWriteSame = 4,
  kWriteZeroes = 5,
  kAllocate = 6
};

struct IODesc {
  /* op: bit 0-7, flags: bit 8-31 (access using helpers below) */
  uint32_t op_flags;
  uint32_t num_sectors;
  uint64_t start_sector;
  uint64_t addr;

  CallbackArgs callback_args;
  Callback callback;

  static OpType get_op(const IODesc *iod) {
    return static_cast<OpType>(iod->op_flags & kOpMask);
  }

  static uint32_t get_flags(const IODesc *iod) {
    return iod->op_flags >> kFlagShift;
  }
} __attribute__((aligned(4)));
static_assert(std::is_standard_layout_v<IODesc> && std::is_trivial_v<IODesc>);

}  // namespace sandook
