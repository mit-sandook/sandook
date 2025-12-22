#include <bits/types/struct_iovec.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sandook/base/error.h"

extern "C" {
#include <base/compiler.h>
#include <base/log.h>
#include <runtime/net.h>
#include <runtime/preempt.h>
}

#include "sandook/bindings/net.h"
#include "sandook/bindings/runtime.h"
#include "sandook/bindings/sync.h"
#include "sandook/bindings/thread.h"
#include "sandook/rpc/rpc.h"

namespace sandook {

namespace {

// The RPC header.
struct RPCHeader {
  unsigned int demand;  // number of RPCs waiting to be sent and inflight
  std::size_t len;      // the length of this RPC request
  std::size_t completion_data;  // an opaque token to complete the RPC
};

constexpr RPCHeader CreateRPCHeader(unsigned int demand, std::size_t len,
                                    std::size_t completion_data) {
  return RPCHeader{demand, len, completion_data};
}

class RPCServer {
 public:
  RPCServer(std::unique_ptr<rt::TCPConn> c, RPCHandler *handler)
      : c_(std::move(c)), handler_(handler) {}
  ~RPCServer() = default;

  // Cannot copy or move.
  RPCServer(const RPCServer &) = delete;
  RPCServer &operator=(const RPCServer &) = delete;
  RPCServer(RPCServer &&) = delete;
  RPCServer &operator=(RPCServer &&) = delete;

  // Runs the RPCServer, returning when the connection is closed.
  void Run();
  // Sends the return results of an RPC.
  void Return(RPCReturnBuffer &&buf, std::size_t completion_data);

 private:
  // Internal worker threads for sending and receiving.
  void SendWorker();
  void ReceiveWorker();

  struct completion {
    RPCReturnBuffer buf;
    std::size_t completion_data;
  };

  rt::Spin lock_;
  std::unique_ptr<rt::TCPConn> c_;
  rt::ThreadWaker wake_sender_;
  std::vector<completion> completions_;
  unsigned int demand_{};
  RPCHandler *handler_;
  bool close_{};
};

void RPCServer::Run() {
  rt::Thread th([this] { SendWorker(); });
  ReceiveWorker();
  th.Join();
}

void RPCServer::Return(RPCReturnBuffer &&buf, std::size_t completion_data) {
  const rt::SpinGuard guard(lock_);
  completions_.emplace_back(std::move(buf), completion_data);
  wake_sender_.Wake();
}

void RPCServer::SendWorker() {
  std::vector<completion> completions;
  std::vector<iovec> iovecs;
  std::vector<RPCHeader> hdrs;

  while (true) {
    {
      // wait for an actionable state.
      rt::SpinGuard guard(lock_);
      while (completions_.empty() && !close_) {
        guard.Park(wake_sender_);
      }

      // gather all queued completions.
      std::move(completions_.begin(), completions_.end(),
                std::back_inserter(completions));
      completions_.clear();
    }

    // Check if the connection is closed.
    if (unlikely(close_ && completions.empty())) {
      break;
    }

    // process each of the requests.
    iovecs.clear();
    hdrs.clear();
    hdrs.reserve(completions.size());
    for (const auto &c : completions) {
      auto span = c.buf.get_buf();
      hdrs.emplace_back(
          CreateRPCHeader(demand_, span.size_bytes(), c.completion_data));

      iovecs.emplace_back(&hdrs.back(), sizeof(decltype(hdrs)::value_type));
      if (span.size_bytes() == 0) {
        continue;
      }
      iovecs.emplace_back(const_cast<std::byte *>(span.data()),
                          span.size_bytes());
    }

    // send data on the wire.
    auto status = c_->WritevFull(std::span<const iovec>(iovecs));
    if (unlikely(!status)) {
      log_err("rpc: WritevFull failed, err = %s",
              status.error().ToString().c_str());
      return;
    }
    completions.clear();
  }
  if (!c_->Abort(SHUT_WR)) {
    c_->Abort();
  }
}

void RPCServer::ReceiveWorker() {
  while (true) {
    // Read the request header.
    RPCHeader hdr{};
    auto status = c_->ReadFull(std::as_writable_bytes(std::span(&hdr, 1)));
    if (unlikely(!status)) {
      auto &error = status.error();
      if (error.code() != EEOF) {
        log_err("rpc: ReadFull failed, err = %s", error.ToString().c_str());
      }
      break;
    }

    // Parse the request header.
    const std::size_t completion_data = hdr.completion_data;
    demand_ = hdr.demand;

    // Spawn a handler with no argument data provided.
    if (hdr.len == 0) {
      rt::Spawn([this, completion_data]() {
        Return(handler_->HandleMsg(std::span<const std::byte>{}),
               completion_data);
      });
      continue;
    }

    // Allocate and fill a buffer with the argument data.
    auto buf = std::make_unique_for_overwrite<std::byte[]>(hdr.len);
    status = c_->ReadFull(std::span<std::byte>(buf.get(), hdr.len));
    if (unlikely(!status)) {
      auto &error = status.error();
      if (error.code() != EEOF) {
        log_err("rpc: ReadFull failed, err = %s", error.ToString().c_str());
      }
      break;
    }
    // Spawn a handler with argument data provided.
    rt::Spawn([this, completion_data, hdr, b = std::move(buf)]() mutable {
      Return(handler_->HandleMsg(std::span<const std::byte>{b.get(), hdr.len}),
             completion_data);
    });
  }

  // Wake the sender to close the connection.
  {
    const rt::SpinGuard guard(lock_);
    close_ = true;
    wake_sender_.Wake();
  }
}

void RPCServerWorker(std::unique_ptr<rt::TCPConn> c, RPCHandler *handler) {
  RPCServer s(std::move(c), handler);
  s.Run();
}

void RPCServerReplicaListener(RPCHandler *handler, uint16_t port,
                              const std::function<void()> &callback) {
  constexpr auto kMaxBackLog = 4096;
  std::unique_ptr<rt::TCPQueue> q(
      rt::TCPQueue::Listen({0, port}, kMaxBackLog).value());

  callback();

  while (true) {
    std::unique_ptr<rt::TCPConn> c(q->Accept().value());
    rt::Thread([c = std::move(c), handler]() mutable {
      RPCServerWorker(std::move(c), handler);
    }).Detach();
  }
}

}  // namespace

namespace detail {

RPCFlow::~RPCFlow() {
  {
    const rt::SpinGuard guard(lock_);
    close_ = true;
    wake_sender_.Wake();
  }
  sender_.Join();
  receiver_.Join();
}

void RPCFlow::Call(std::span<const std::byte> src, RPCCompletion *conn) {
  assert_preempt_disabled();
  const rt::SpinGuard guard(lock_);
  reqs_.emplace(req_ctx{src, conn});
  if (sent_count_ - recv_count_ < credits_) {
    wake_sender_.Wake();
  }
}

void RPCFlow::SendWorker() {
  std::vector<req_ctx> reqs;
  std::vector<iovec> iovecs;
  std::vector<RPCHeader> hdrs;

  while (true) {
    unsigned int demand = 0;
    unsigned int inflight = 0;
    bool close = false;

    {
      // wait for an actionable state.
      rt::SpinGuard guard(lock_);
      inflight = sent_count_ - recv_count_;
      while ((reqs_.empty() || inflight >= credits_) &&
             !(close_ && reqs_.empty())) {
        guard.Park(wake_sender_);
        inflight = sent_count_ - recv_count_;
      }

      // gather queued requests up to the credit limit.
      while (!reqs_.empty() && inflight < credits_) {
        reqs.emplace_back(reqs_.front());
        reqs_.pop();
        inflight++;
      }
      sent_count_ += reqs.size();
      close = close_ && reqs_.empty();
      demand = inflight;
    }

    // Check if it is time to close the connection.
    if (unlikely(close)) {
      break;
    }

    // construct a scatter-gather list for all the pending requests.
    iovecs.clear();
    hdrs.clear();
    hdrs.reserve(reqs.size());
    for (const auto &r : reqs) {
      const auto &span = r.payload;
      hdrs.emplace_back(
          CreateRPCHeader(demand, span.size_bytes(),
                          reinterpret_cast<std::size_t>(r.completion)));
      iovecs.emplace_back(&hdrs.back(), sizeof(decltype(hdrs)::value_type));
      if (span.size_bytes() == 0) {
        continue;
      }
      iovecs.emplace_back(const_cast<std::byte *>(span.data()),
                          span.size_bytes());
    }

    // send data on the wire.
    auto status = c_->WritevFull(std::span<const iovec>(iovecs));
    if (unlikely(!status)) {
      log_err("rpc: WritevFull failed, err = %s",
              status.error().ToString().c_str());
      return;
    }
    reqs.clear();
  }

  // send FIN on the wire.
  if (!c_->Abort(SHUT_WR)) {
    c_->Abort();
  }
}

void RPCFlow::ReceiveWorker() {
  while (true) {
    // Read the response header.
    RPCHeader hdr{};
    auto status = c_->ReadFull(std::as_writable_bytes(std::span(&hdr, 1)));
    if (unlikely(!status)) {
      auto &error = status.error();
      if (error.code() != EEOF) {
        log_err("rpc: ReadFull failed, err = %s", error.ToString().c_str());
      }
      return;
    }

    // Check if we should wake the sender.
    {
      const rt::SpinGuard guard(lock_);
      const unsigned int inflight = sent_count_ - ++recv_count_;
      // credits_ = hdr.credits;
      if (credits_ > inflight && !reqs_.empty()) {
        wake_sender_.Wake();
      }
    }

    // Check if there is no return data.
    auto *completion = reinterpret_cast<RPCCompletion *>(hdr.completion_data);
    if (hdr.len == 0) {
      completion->Done();
      continue;
    }

    // Allocate and fill a buffer for the return data.
    auto buf = std::make_unique_for_overwrite<std::byte[]>(hdr.len);
    status = c_->ReadFull(std::span<std::byte>(buf.get(), hdr.len));
    if (unlikely(!status)) {
      auto &error = status.error();
      if (error.code() != EEOF) {
        log_err("rpc: ReadFull failed, err = %s", error.ToString().c_str());
      }
      return;
    }

    // Issue a completion, waking the blocked thread.
    const std::span<const std::byte> s(buf.get(), hdr.len);
    completion->Done(s, [b = std::move(buf)]() mutable {});
  }
}

std::unique_ptr<RPCFlow> RPCFlow::New(unsigned int cpu_affinity,
                                      netaddr raddr) {
  auto conn = rt::TCPConn::DialAffinity(cpu_affinity, raddr);
  if (!conn) {
    throw std::runtime_error("Unable to establish a connection");
  }
  std::unique_ptr<rt::TCPConn> c = std::move(conn.value());
  std::unique_ptr<RPCFlow> f = std::make_unique<RPCFlow>(std::move(c));
  f->sender_ = rt::Thread([f = f.get()] { f->SendWorker(); });
  f->receiver_ = rt::Thread([f = f.get()] { f->ReceiveWorker(); });
  return f;
}

}  // namespace detail

void RPCServerInit(RPCHandler *handler, uint16_t port) {
  RPCServerReplicaListener(handler, port, []() {});
}

void RPCServerInit(RPCHandler *handler, uint16_t port,
                   const std::function<void()> &callback) {
  RPCServerReplicaListener(handler, port, callback);
}

std::unique_ptr<RPCClient> RPCClient::Dial(netaddr raddr) {
  std::vector<std::unique_ptr<RPCFlow>> v;
  for (unsigned int i = 0; i < rt::RuntimeMaxCores(); ++i) {
    v.emplace_back(RPCFlow::New(i, raddr));
  }
  return std::unique_ptr<RPCClient>(new RPCClient(std::move(v)));
}

std::unique_ptr<RPCClient> RPCClient::Connect(const char *ip, uint16_t port) {
  netaddr raddr{};
  const std::string ipaddr = ip;
  const std::string addr = ipaddr + ":" + std::to_string(port);
  str_to_netaddr(addr.c_str(), &raddr);
  raddr.port = port;
  return Dial(raddr);
}

RPCReturnBuffer RPCClient::Call(std::span<const std::byte> args) {
  RPCReturnBuffer buf;
  RPCCompletion completion(&buf);
  {
    rt::Preempt p;
    const rt::PreemptGuardAndPark guard(p);
    flows_[sandook::rt::Preempt::get_cpu()]->Call(args, &completion);
  }
  return buf;
}

}  // namespace sandook
