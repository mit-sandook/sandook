#pragma once

#include <bits/types/struct_iovec.h>

#include <cerrno>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

extern "C" {
#include <base/types.h>
#include <runtime/net.h>
#include <runtime/tcp.h>
}

#include "sandook/base/error.h"
#include "sandook/base/io.h"

namespace sandook::rt {

class TCPConn : public VectorIO {
  friend class TCPQueue;

 public:
  TCPConn() = default;
  ~TCPConn() override {
    if (is_valid()) {
      tcp_close(c_);
    }
  }

  // No copying.
  TCPConn(const TCPConn &) = delete;
  TCPConn &operator=(const TCPConn &) = delete;

  // Explicit move definition.
  TCPConn(TCPConn &&c) noexcept : c_(std::exchange(c.c_, nullptr)) {}
  TCPConn &operator=(TCPConn &&c) noexcept {
    if (is_valid()) {
      tcp_close(c_);
    }
    c_ = std::exchange(c.c_, nullptr);
    return *this;
  }

  // Creates a TCP connection between a local and remote address.
  static Status<std::unique_ptr<TCPConn>> Dial(netaddr laddr, netaddr raddr) {
    tcpconn_t *c = nullptr;
    const int ret = tcp_dial(laddr, raddr, &c);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPConn>(new TCPConn(c));
  }

  // Creates a TCP connection between a local and remote address.
  static Status<std::unique_ptr<TCPConn>> DialNonBlocking(netaddr laddr,
                                                          netaddr raddr) {
    tcpconn_t *c = nullptr;
    const int ret = tcp_dial_nonblocking(laddr, raddr, &c);
    if ((ret != 0) && ret != -EINPROGRESS) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPConn>(new TCPConn(c));
  }

  // Creates a TCP connection with affinity to a CPU index.
  static Status<std::unique_ptr<TCPConn>> DialAffinity(unsigned int cpu,
                                                       netaddr raddr) {
    tcpconn_t *c = nullptr;
    const int ret = tcp_dial_affinity(cpu, raddr, &c);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPConn>(new TCPConn(c));
  }

  // Creates a new TCP connection with affinity to another TCP connection.
  static Status<std::unique_ptr<TCPConn>> DialAffinity(const TCPConn &cin,
                                                       netaddr raddr) {
    tcpconn_t *c = nullptr;
    const int ret = tcp_dial_conn_affinity(cin.c_, raddr, &c);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPConn>(new TCPConn(c));
  }

  // Does this hold a valid TCP connection?
  [[nodiscard]] bool is_valid() const { return c_ != nullptr; }

  // Gets the local TCP address.
  [[nodiscard]] netaddr LocalAddr() const { return tcp_local_addr(c_); }
  // Gets the remote TCP address.
  [[nodiscard]] netaddr RemoteAddr() const { return tcp_remote_addr(c_); }
  // Checks status of TCP connection (intended for non-blocking dial)
  [[nodiscard]] Status<void> GetStatus() const {
    const int ret = tcp_get_status(c_);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return {};
  }

  // Reads from the TCP stream.
  [[nodiscard]] Status<size_t> Read(std::span<std::byte> buf) const {
    const ssize_t ret = tcp_read(c_, buf.data(), buf.size_bytes());
    if (ret == 0) {
      return MakeError(EEOF);
    }
    if (ret < 0) {
      return MakeError(static_cast<int>(-ret));
    }
    return ret;
  }

  // Reads the entire buffer from the TCP stream.
  [[nodiscard]] Status<void> ReadFull(std::span<std::byte> buf) const {
    ssize_t n = 0;
    const auto size = static_cast<ssize_t>(buf.size_bytes());
    while (n < size) {
      const ssize_t ret = tcp_read(c_, buf.data() + n, size - n);
      if (ret == 0) {
        break;
      }
      if (ret < 0) {
        return MakeError(static_cast<int>(-ret));
      }
      n += ret;
    }
    if (n != size) {
      if (n == 0) {
        return MakeError(EEOF);
      }
      return MakeError(EINVAL);
    }
    return {};
  }

  // Writes to the TCP stream.
  [[nodiscard]] Status<size_t> Write(std::span<const std::byte> buf) const {
    const ssize_t ret = tcp_write(c_, buf.data(), buf.size_bytes());
    if (ret < 0) {
      return MakeError(static_cast<int>(-ret));
    }
    return ret;
  }

  // Writes the entire buffer to the TCP stream.
  [[nodiscard]] Status<void> WriteFull(std::span<const std::byte> buf) const {
    ssize_t n = 0;
    const auto size = static_cast<ssize_t>(buf.size_bytes());
    while (n < size) {
      const ssize_t ret = tcp_write(c_, buf.data() + n, size - n);
      if (ret == 0) {
        break;
      }
      if (ret < 0) {
        return MakeError(static_cast<int>(-ret));
      }
      n += ret;
    }
    if (n != size) {
      return MakeError(EINVAL);
    }
    return {};
  }

  // Reads a vector from the TCP stream.
  [[nodiscard]] Status<size_t> Readv(
      std::span<const iovec> iov) const override {
    const ssize_t ret = tcp_readv(c_, iov.data(), static_cast<int>(iov.size()));
    if (ret == 0) {
      return MakeError(EEOF);
    }
    if (ret < 0) {
      return MakeError(static_cast<int>(-ret));
    }
    return ret;
  }

  // Writes a vector to the TCP stream.
  [[nodiscard]] Status<size_t> Writev(
      std::span<const iovec> iov) const override {
    const ssize_t ret =
        tcp_writev(c_, iov.data(), static_cast<int>(iov.size()));
    if (ret < 0) {
      return MakeError(static_cast<int>(-ret));
    }
    return ret;
  }

  // Reads exactly a vector of bytes from the TCP stream.
  template <std::size_t Extent>
  [[nodiscard]] Status<void> ReadvFull(std::span<const iovec> iov) const {
    if constexpr (Extent == 1) {
      return ReadFull(readable_span(iov[0].iov_base, iov[0].iov_len));
    }
    return sandook::ReadvFull(*this, iov);
  }

  // Writes a vector to the TCP stream.
  template <std::size_t Extent>
  [[nodiscard]] Status<void> WritevFull(
      std::span<const iovec, Extent> iov) const {
    if constexpr (Extent == 1) {
      return WriteFull(writable_span(iov[0].iov_base, iov[0].iov_len));
    }
    return sandook::WritevFull(*this, iov);
  }

  // Gracefully shutdown the TCP connection.
  Status<void> Abort(int how) {
    const int ret = tcp_shutdown(c_, how);
    if (ret < 0) {
      return MakeError(-ret);
    }
    return {};
  }

  // Ungracefully force the TCP connection to shutdown.
  void Abort() { tcp_abort(c_); }

 private:
  explicit TCPConn(tcpconn_t *c) noexcept : c_(c) {}

  tcpconn_t *c_{nullptr};
};

// TCP listener queues.
class TCPQueue {
 public:
  TCPQueue() = default;
  ~TCPQueue() {
    if (is_valid()) {
      tcp_qclose(q_);
    }
  }

  // No copying.
  TCPQueue(const TCPQueue &) = delete;
  TCPQueue &operator=(const TCPQueue &) = delete;

  // Explicit move definition.
  TCPQueue(TCPQueue &&q) noexcept : q_(std::exchange(q.q_, nullptr)) {}
  TCPQueue &operator=(TCPQueue &&q) noexcept {
    if (is_valid()) {
      tcp_qclose(q_);
    }
    q_ = std::exchange(q.q_, nullptr);
    return *this;
  }

  // Creates a TCP listener queue.
  [[nodiscard]] static Status<std::unique_ptr<TCPQueue>> Listen(netaddr laddr,
                                                                int backlog) {
    tcpqueue_t *q = nullptr;
    const int ret = tcp_listen(laddr, backlog, &q);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPQueue>(new TCPQueue(q));
  }

  // Accept a connection from the listener queue.
  [[nodiscard]] Status<std::unique_ptr<TCPConn>> Accept() const {
    tcpconn_t *c = nullptr;
    const int ret = tcp_accept(q_, &c);
    if (ret != 0) {
      return MakeError(-ret);
    }
    return std::unique_ptr<TCPConn>(new TCPConn(c));
  }

  // Does this hold a valid TCP listener queue?
  [[nodiscard]] bool is_valid() const { return q_ != nullptr; }

  // Gets the local TCP address.
  [[nodiscard]] netaddr LocalAddr() const { return tcpq_local_addr(q_); }

  // Abort the listener queue; any blocked Accept() returns a nullptr.
  void Abort() { tcp_qshutdown(q_); }

 private:
  explicit TCPQueue(tcpqueue_t *q) noexcept : q_(q) {}

  tcpqueue_t *q_{nullptr};
};

}  // namespace sandook::rt
