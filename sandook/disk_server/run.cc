#include <cerrno>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>

#include "sandook/bindings/runtime.h"
#include "sandook/disk_server/disk_server.h"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: [cfg_file] [optional: dev_name]" << '\n';
    return -EINVAL;
  }

  const auto args = std::span(argv, static_cast<size_t>(argc));

  const std::string cfg_file(args[1]);
  std::string dev_name;
  if (argc == 3) {
    dev_name = std::string(args[2]);
    std::cout << "Using device: " << dev_name << '\n';
  }
  const auto dev_path = "/dev/" + dev_name;

  auto ret = sandook::rt::RuntimeInit(
      cfg_file, [&] { sandook::DiskServer::Launch(dev_path); });
  if (ret != 0) {
    std::cerr << "Failed to start Caladan runtime" << '\n';
    return ret;
  }

  return 0;
}
