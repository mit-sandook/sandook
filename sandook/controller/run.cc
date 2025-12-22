#include <cerrno>
#include <cstddef>
#include <string>
extern "C" {
#include <runtime/runtime.h>
}

#include <iostream>
#include <span>

#include "sandook/controller/controller.h"

void Run(void* /*arg*/) { sandook::Controller::Launch(); }

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << '\n';
    return -EINVAL;
  }

  const auto args = std::span(argv, static_cast<size_t>(argc));

  const std::string cfg_file(args[1]);

  const int ret = runtime_init(cfg_file.c_str(), Run, nullptr);
  if (ret != 0) {
    std::cerr << "Failed to start Caladan runtime" << '\n';
    return ret;
  }

  return 0;
}
