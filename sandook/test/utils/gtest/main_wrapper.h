// NOLINTBEGIN

#pragma once

// This header is used to initialize Caladan's runtime and ensure that
// gtest's main() is run in a Caladan thread.

extern "C" {
#include <base/log.h>
}

#include <cstdlib>
#include <iostream>
#include <utility>

#include "sandook/bindings/runtime.h"

int __ret_val;
extern "C" int __real_main(int, char**);

extern "C" void init_shutdown(int status) {
  bool exit_success = (status == EXIT_SUCCESS);
  if (!exit_success) {
    exit(status);
  }
  log_info("init: shutting down -> %s", exit_success ? "SUCCESS" : "FAILURE");
  exit(__ret_val);
}

extern "C" int __wrap_main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  std::string cfg_file(argv[1]);

  argc -= 1;
  argv++;

  auto ret = sandook::rt::RuntimeInit(
      cfg_file, [&] { __ret_val = __real_main(argc, argv); });
  if (ret != 0) {
    std::cerr << "Failed to start Caladan runtime" << std::endl;
    return ret;
  }

  std::unreachable();
}

// NOLINTEND
