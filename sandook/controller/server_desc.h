#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include "sandook/base/constants.h"
#include "sandook/base/msg.h"

namespace sandook {

class ServerDesc {
 public:
  ServerDesc(uint32_t id, std::string ip, int port, std::string name,
             uint64_t nsectors)
      : id_(id),
        ip_(std::move(ip)),
        name_(std::move(name)),
        port_(port),
        nsectors_(nsectors) {
    std::cout << "Allocated: " << nsectors_ << '\n';
  }
  ~ServerDesc() = default;

  /* No copying. */
  ServerDesc(const ServerDesc &) = delete;
  ServerDesc &operator=(const ServerDesc &) = delete;

  /* No moving. */
  ServerDesc(ServerDesc &&other) = delete;
  ServerDesc &operator=(ServerDesc &&other) = delete;

  [[nodiscard]] ServerInfo info() const {
    ServerInfo info{};
    info.id = id_;
    std::strncpy(static_cast<char *>(info.ip), ip_.c_str(), ip_.size());
    std::strncpy(static_cast<char *>(info.name), name_.c_str(), name_.size());
    info.port = port_;
    return info;
  }

  [[nodiscard]] uint64_t nsectors() const { return nsectors_; }

  friend std::ostream &operator<<(std::ostream &out, const ServerDesc &p) {
    out << "DiskServer: " << p.id_ << '\n';
    out << "\t" << p.name_ << '\n';
    out << "\t" << p.ip_ << ":" << p.port_ << '\n';
    out << "\t" << p.nsectors_ << " sectors";
    return out;
  }

 private:
  /* Unique ID of the associated remote server as assigned by the controller. */
  uint32_t id_{};

  /* Static properties of the associated remote server. */
  std::string ip_;
  std::string name_;
  int port_{};
  uint64_t nsectors_{};
};

}  // namespace sandook
