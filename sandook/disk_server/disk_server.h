#pragma once

#include <string>

namespace sandook {

class DiskServer {
 public:
  static void Launch(const std::string& backing_device);
};

}  // namespace sandook
