#pragma once

#include <string>

namespace sandook {

class BlkDev {
 public:
  static void Launch(const std::string queue_to_core_mapping);
};

}  // namespace sandook
