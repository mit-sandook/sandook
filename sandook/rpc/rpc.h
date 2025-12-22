#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "runtime/net.h"
#include "sandook/bindings/net.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"

namespace sandook {

// RPCReturnBuffer manages a return data buffer and its lifetime.
class RPCReturnBuffer {
 public:
  RPCReturnBuffer() = default;
  RPCReturnBuffer(std::span<const std::byte> buf,
                  std::move_only_function<void()> deleter_fn)
      : buf_(buf), deleter_fn_(std::move(deleter_fn)) {}
  ~RPCReturnBuffer() {
    if (deleter_fn_) {
      deleter_fn_();
    }
  }

  // Disable copy
  RPCReturnBuffer(const RPCReturnBuffer &) = delete;
  RPCReturnBuffer &operator=(const RPCReturnBuffer &) = delete;

  // Support move
  RPCReturnBuffer(RPCReturnBuffer &&rbuf) noexcept
      : buf_(rbuf.buf_), deleter_fn_(std::move(rbuf.deleter_fn_)) {
    rbuf.buf_ = std::span<const std::byte>();
  }
  RPCReturnBuffer &operator=(RPCReturnBuffer &&rbuf) noexcept {
    deleter_fn_ = std::move(rbuf.deleter_fn_);
    buf_ = rbuf.buf_;
    rbuf.buf_ = std::span<const std::byte>();
    return *this;
  }

  explicit operator bool() const { return !buf_.empty(); }

  // Replaces the return data buffer.
  void Reset(std::span<const std::byte> buf,
             std::move_only_function<void()> deleter_fn) {
    if (deleter_fn_) {
      deleter_fn_();
    }
    buf_ = buf;
    deleter_fn_ = std::move(deleter_fn);
  }

  // Gets the return data buffer.
  [[nodiscard]] std::span<const std::byte> get_buf() const { return buf_; }

 private:
  std::span<const std::byte> buf_;
  std::move_only_function<void()> deleter_fn_;
};

class RPCHandler {
 public:
  RPCHandler() = default;
  virtual ~RPCHandler() = default;

  /* No copying. */
  RPCHandler(const RPCHandler &) = delete;
  RPCHandler &operator=(const RPCHandler &) = delete;

  // Support moving.
  RPCHandler(RPCHandler &&) = default;
  RPCHandler &operator=(RPCHandler &&) = default;

  virtual RPCReturnBuffer HandleMsg(std::span<const std::byte> payload) = 0;
};

namespace detail {

// RPCCompletion manages the completion of an inflight request.
class RPCCompletion {
 public:
  explicit RPCCompletion(RPCReturnBuffer *buf) : buf_(buf) { w_.Arm(); }
  ~RPCCompletion() = default;

  // Cannot copy or move.
  RPCCompletion(const RPCCompletion &) = delete;
  RPCCompletion &operator=(const RPCCompletion &) = delete;
  RPCCompletion(RPCCompletion &&) = delete;
  RPCCompletion &operator=(RPCCompletion &&) = delete;

  // Complete the request with return data and wake the blocking thread.
  void Done(std::span<const std::byte> buf,
            std::move_only_function<void()> deleter_fn) {
    buf_->Reset(buf, std::move(deleter_fn));
    w_.Wake();
  }

  // Complete the request without return data and wake the blocking thread.
  void Done() { w_.Wake(); }

 private:
  RPCReturnBuffer *buf_;
  rt::ThreadWaker w_;
};

// RPCFlow encapsulates one of the TCP connections used by an RPCClient.
class RPCFlow {
 public:
  static constexpr auto kNumCredits = 128;

  explicit RPCFlow(std::unique_ptr<rt::TCPConn> c) : c_(std::move(c)) {}
  ~RPCFlow();

  // Cannot copy or move.
  RPCFlow(const RPCFlow &) = delete;
  RPCFlow &operator=(const RPCFlow &) = delete;
  RPCFlow(RPCFlow &&) = delete;
  RPCFlow &operator=(RPCFlow &&) = delete;

  // A factory to create new flows with CPU affinity.
  static std::unique_ptr<RPCFlow> New(unsigned int cpu_affinity, netaddr raddr);

  // Make an RPC call over this flow.
  void Call(std::span<const std::byte> src, RPCCompletion *conn);

 private:
  // State for managing inflight requests.
  struct req_ctx {
    std::span<const std::byte> payload;
    RPCCompletion *completion;
  };

  // Internal worker threads for sending and receiving.
  void SendWorker();
  void ReceiveWorker();

  rt::Thread sender_, receiver_;
  rt::Spin lock_;
  bool close_{};
  rt::ThreadWaker wake_sender_;
  std::unique_ptr<rt::TCPConn> c_;
  unsigned int sent_count_{};
  unsigned int recv_count_{};
  unsigned int credits_{kNumCredits};
  std::queue<req_ctx> reqs_;
};

}  // namespace detail

// A function handler for each RPC request, invoked concurrently.
using RPCFuncPtr = RPCReturnBuffer (*)(std::span<const std::byte> args);
// Initializes and runs the RPC server.
void RPCServerInit(RPCHandler *handler, uint16_t port);
// Initializes and runs the RPC server and invokes the callback when the server
// is ready.
void RPCServerInit(RPCHandler *handler, uint16_t port,
                   const std::function<void()> &callback);

class RPCClient {
 public:
  ~RPCClient() = default;

  // Cannot copy or move.
  RPCClient(const RPCClient &) = delete;
  RPCClient &operator=(const RPCClient &) = delete;
  RPCClient(RPCClient &&) = delete;
  RPCClient &operator=(RPCClient &&) = delete;

  // Creates an RPC Client and establishes the underlying TCP connections.
  static std::unique_ptr<RPCClient> Dial(netaddr raddr);

  // Wrapper over Dial() that uses ip and port strings.
  static std::unique_ptr<RPCClient> Connect(const char *ip, uint16_t port);

  // Calls an RPC method.
  RPCReturnBuffer Call(std::span<const std::byte> args);

 private:
  using RPCCompletion = detail::RPCCompletion;
  using RPCFlow = detail::RPCFlow;

  explicit RPCClient(std::vector<std::unique_ptr<RPCFlow>> flows)
      : flows_(std::move(flows)) {}

  // an array of per-kthread RPC flows.
  std::vector<std::unique_ptr<RPCFlow>> flows_;
};

}  // namespace sandook
