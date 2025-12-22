#include "sandook/blk_dev/blk_dev.h"

#include <iostream>
#include <string>

#include "sandook/blk_dev/blk_dev_agent.h"

namespace sandook {

void BlkDev::Launch(const std::string queue_to_core_mapping) {
  auto *agent = BlkDevAgent::GetInstance();

  const int ret = agent->Run(queue_to_core_mapping);
  if (ret != 0) {
    std::cerr << "blk_dev error: " << ret << '\n';
  }
}

}  // namespace sandook
