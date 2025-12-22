#pragma once

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
}

#include <bits/types/struct_iovec.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

#include "sandook/base/error.h"

namespace sandook {

// Reader is a concept for the basic UNIX-style Read method
template <typename T>
concept Reader = requires(T t) {
  {
    t.Read(std::declval<std::span<std::byte>>())
  } -> std::same_as<Status<size_t>>;
};

// Writer is a concept for the basic UNIX-style Write method
template <typename T>
concept Writer = requires(T t) {
  {
    t.Write(std::declval<std::span<const std::byte>>())
  } -> std::same_as<Status<size_t>>;
};

// Cast an object as a const byte span (for use with Write())
template <typename T>
std::span<const std::byte, sizeof(T)> byte_view(const T &t) {
  return std::as_bytes(std::span<const T, 1>{std::addressof(t), 1});
}

// Cast an object as a mutable byte span (for use with Read())
template <typename T>
std::span<std::byte, sizeof(T)> writable_byte_view(T &t) {
  return std::as_writable_bytes(std::span<T, 1>{std::addressof(t), 1});
}

// Cast a legacy UNIX read buffer as a span.
inline std::span<std::byte> readable_span(void *buf, size_t len) {
  return {reinterpret_cast<std::byte *>(buf), len};
}

// Cast a legacy UNIX write buffer as a span.
inline std::span<const std::byte> writable_span(const void *buf, size_t len) {
  return {reinterpret_cast<const std::byte *>(buf), len};
}

inline Status<void> ReadFull(const int fd, std::span<std::byte> buf) {
  const ssize_t size = buf.size();  // NOLINT
  ssize_t n = 0;

  while (n < size) {
    const ssize_t ret = read(fd, reinterpret_cast<void *>(&buf[n]), size - n);
    if (ret == 0) {
      break;
    }
    if (ret == -1) {
      return MakeError(errno);
    }
    n += ret;
  }
  if (n != size) {
    return MakeError(EINVAL);
  }

  return {};
}

inline Status<void> WriteFull(const int fd, std::span<const std::byte> buf) {
  const ssize_t size = buf.size();  // NOLINT
  ssize_t n = 0;

  while (n < size) {
    const ssize_t ret =
        write(fd, reinterpret_cast<const void *>(&buf[n]), size - n);
    if (ret == 0) {
      break;
    }
    if (ret == -1) {
      return MakeError(errno);
    }
    n += ret;
  }
  if (n != size) {
    return MakeError(EINVAL);
  }

  return {};
}

// VectorIO is an interface for vector reads and writes.
class VectorIO {
 public:
  virtual ~VectorIO() = default;
  [[nodiscard]] virtual Status<size_t> Readv(
      std::span<const iovec> iov) const = 0;
  [[nodiscard]] virtual Status<size_t> Writev(
      std::span<const iovec> iov) const = 0;
};

Status<void> ReadvFull(const VectorIO &io, std::span<const iovec> iov);
Status<void> WritevFull(const VectorIO &io, std::span<const iovec> iov);

}  // namespace sandook
