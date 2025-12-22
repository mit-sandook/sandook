#include "sandook/base/io.h"

#include <bits/types/struct_iovec.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <span>

#include "sandook/base/error.h"

namespace sandook {

namespace {

constexpr int kStackSlots = 8;

size_t SumIOV(std::span<const iovec> iov) {
  size_t len = 0;
  for (const iovec &e : iov) {
    len += e.iov_len;
  }
  return len;
}

std::span<iovec> PullIOV(std::span<iovec> iov, size_t n) {
  for (auto it = iov.begin(); it < iov.end(); ++it) {
    if (n < it->iov_len) {
      (*it).iov_base = reinterpret_cast<char *>(it->iov_base) + n;  // NOLINT
      (*it).iov_len -= n;
      return {it, iov.end()};
    }
    n -= it->iov_len;
  }

  assert(n == 0);
  return {};
}

template <auto func>
Status<void> DoFull(const VectorIO &io, std::span<const iovec> iov) {
  // first try to send without copying the vector
  Status<size_t> ret = (io.*func)(iov);
  if (!ret) {
    return MakeError(ret);
  }

  // sum total length and check if everything was transfered
  if (*ret == SumIOV(iov)) {
    return {};
  }

  // partial transfer occurred, copy and update vector, then send the rest
  iovec vstack[kStackSlots];
  std::unique_ptr<iovec[]> vheap;
  auto *v = reinterpret_cast<iovec *>(vstack);
  if (iov.size() > kStackSlots) {
    vheap = std::make_unique_for_overwrite<iovec[]>(iov.size());
    v = vheap.get();
  }
  std::copy(iov.begin(), iov.end(), v);
  std::span<iovec> s(v, iov.size());
  while (true) {
    s = PullIOV(s, *ret);
    if (s.empty()) {
      break;
    }
    ret = (io.*func)(s);
    if (!ret) {
      return MakeError(ret);
    }
  }

  return {};
}

}  // namespace

Status<void> WritevFull(const VectorIO &io, std::span<const iovec> iov) {
  return DoFull<&VectorIO::Writev>(io, iov);
}

Status<void> ReadvFull(const VectorIO &io, std::span<const iovec> iov) {
  return DoFull<&VectorIO::Readv>(io, iov);
}

}  // namespace sandook
