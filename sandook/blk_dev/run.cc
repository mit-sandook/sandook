extern "C" {
#include <runtime/runtime.h>
}

#include <iostream>

#include "sandook/blk_dev/blk_dev.h"

using namespace sandook;

void Run(void *arg) {
  std::string q2c;
  if (arg != nullptr) {
    char *q2c_buf = reinterpret_cast<char *>(arg);
    q2c = std::string(q2c_buf);
  }

  BlkDev::Launch(q2c);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: [cfg_file] [optional: queue_to_core_mapping "
                 "(e.g., 0:1,1:5)]\n";
    return -EINVAL;
  }

  std::string cfg_file(argv[1]);

  std::string q2c;
  void *arg = nullptr;
  if (argc > 2) {
    q2c = std::string(argv[2]);
    arg = const_cast<void *>(reinterpret_cast<const void *>(q2c.c_str()));
  }
  int ret = runtime_init(cfg_file.c_str(), Run, arg);
  if (ret != 0) {
    std::cerr << "Failed to start Caladan runtime" << std::endl;
    return ret;
  }

  return 0;
}
