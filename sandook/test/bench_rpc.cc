extern "C" {
#include <base/compiler.h>
#include <net/ip.h>
#include <runtime/net.h>
}

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sandook/bindings/runtime.h"
#include "sandook/bindings/thread.h"
#include "sandook/rpc/rpc.h"

namespace {
constexpr uint16_t kRPCPort = 8080;

class ServerHandler : public sandook::RPCHandler {
 public:
  ServerHandler(size_t resp_len) : resp_len_(resp_len) {}
  ~ServerHandler() override = default;

  /* No copying. */
  ServerHandler(const ServerHandler &) = delete;
  ServerHandler &operator=(const ServerHandler &) = delete;

  /* No moving. */
  ServerHandler(ServerHandler &&) noexcept;
  ServerHandler &operator=(ServerHandler &&) noexcept;

  sandook::RPCReturnBuffer HandleMsg(
      std::span<const std::byte> payload) override {
    auto buf = std::make_unique<std::byte[]>(resp_len_);
    const std::span<const std::byte> s(buf.get(), resp_len_);
    return sandook::RPCReturnBuffer{s, [b = std::move(buf)]() mutable {}};
  }

 private:
  size_t resp_len_;
};

void RunServer(size_t resp_len) {
  ServerHandler handler(resp_len);
  sandook::RPCServerInit(&handler, kRPCPort);
}

void RunClient(netaddr raddr, int threads, int samples, size_t req_len) {
  const std::unique_ptr<sandook::RPCClient> c = sandook::RPCClient::Dial(raddr);
  std::vector<sandook::rt::Thread> workers;
  size_t resp_len = 0;

  // |--- start experiment duration timing ---|
  barrier();
  auto start = std::chrono::steady_clock::now();
  barrier();

  workers.reserve(threads);
  for (int i = 0; i < threads; ++i) {
    workers.emplace_back([c = c.get(), samples, req_len, &resp_len] {
      auto buf = std::make_unique<std::byte[]>(req_len);
      for (int i = 0; i < samples; ++i) {
        auto resp_buf = c->Call({buf.get(), req_len});
	if (unlikely(!resp_len)) {
          resp_len = resp_buf.get_buf().size_bytes();
        }
      }
    });
  }
  for (auto &t : workers) {
    t.Join();
  }

  // |--- end experiment duration timing ---|
  barrier();
  auto finish = std::chrono::steady_clock::now();
  barrier();

  // report results
  const double seconds =
      duration_cast<std::chrono::duration<double>>(finish - start).count();
  const int reqs = samples * threads;

  const size_t sent_mbytes = req_len * reqs / 1000 / 1000;
  const double sent_mbytes_per_second =
      static_cast<double>(sent_mbytes) / seconds;
  std::cout << "Sent " << sent_mbytes_per_second << " MB/s" << '\n';

  const size_t received_mbytes = resp_len * reqs / 1000 / 1000;
  const double received_mbytes_per_second =
      static_cast<double>(received_mbytes) / seconds;
  std::cout << "Received " << received_mbytes_per_second << " MB/s" << '\n';

  const double reqs_per_second = static_cast<double>(reqs) / seconds;
  std::cout << "RPC rate: " << reqs_per_second << " reqs/s" << '\n';
}

int StringToAddr(const char *str, uint32_t *addr) {
  uint8_t a, b, c, d;  // NOLINT
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return -EINVAL;
  }
  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

}  // namespace

// General arguments.
constexpr auto kArgConfIdx = 1;
constexpr auto kArgCmdIdx = 2;

// Client arguments.
constexpr auto kArgIPIdx = 3;
constexpr auto kArgThreadsIdx = 4;
constexpr auto kArgSamplesIdx = 5;
constexpr auto kArgReqLenIdx = 6;
constexpr auto kClientNumArgs = kArgReqLenIdx + 1;

// Server arguments.
constexpr auto kArgRespLenIdx = 3;
constexpr auto kServerNumArgs = kArgRespLenIdx + 1;

int main(int argc, char *argv[]) {
  std::string cmd;
  netaddr raddr;
  int threads;
  int samples;
  size_t req_len;
  size_t resp_len;
  bool is_client;
  std::span<char *> args;

  if (argc <= kArgCmdIdx) {
    goto wrong_args;
  }
  args = std::span(argv, argc);
  cmd = args[kArgCmdIdx];

  if (cmd == "client") {
    is_client = true;
    if (argc != kClientNumArgs) {
      goto wrong_client_args;
    }

    const int ret = StringToAddr(args[kArgIPIdx], &raddr.ip);
    if (ret != 0) {
      goto wrong_client_args;
    }
    raddr.port = kRPCPort;
    threads = std::stoi(args[kArgThreadsIdx], nullptr, 0);
    samples = std::stoi(args[kArgSamplesIdx], nullptr, 0);
    req_len = std::stoul(args[kArgReqLenIdx], nullptr, 0);
  } else if (cmd == "server") {
    is_client = false;
    if (argc != kServerNumArgs) {
      goto wrong_server_args;
    }
    resp_len = std::stoul(args[kArgRespLenIdx], nullptr, 0);
  } else  {
    goto wrong_args;
  }

  return sandook::rt::RuntimeInit(args[kArgConfIdx], [=] {
    if (is_client) {
      RunClient(raddr, threads, samples, req_len);
    } else {
      RunServer(resp_len);
    }
  });

wrong_client_args:
  std::cerr << "usage: [cfg_file] " << cmd << " [ip_addr] [threads] "
            << "[samples] [req_len]" << '\n';
  return -EINVAL;

wrong_server_args:
  std::cerr << "usage: [cfg_file] " << cmd << " [resp_len]" << '\n';
  return -EINVAL;

wrong_args:
  std::cerr << "usage: [cfg_file] [command] ..." << '\n';
  std::cerr << "commands>" << '\n';
  std::cerr << "\tserver - runs an RPC server" << '\n';
  std::cerr << "\tclient - runs an RPC client" << '\n';
  return -EINVAL;

}
