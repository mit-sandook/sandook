#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace sandook {

class VolumeDesc {
 public:
  VolumeDesc(uint32_t id, std::string ip, int port, uint64_t nsectors)
      : id_(id), ip_(std::move(ip)), port_(port), nsectors_(nsectors) {}
  ~VolumeDesc() = default;

  [[nodiscard]] uint64_t nsectors() const { return nsectors_; }

  /* No copying. */
  VolumeDesc(const VolumeDesc &) = delete;
  VolumeDesc &operator=(const VolumeDesc &) = delete;

  /* Explicit move definition. */
  VolumeDesc(VolumeDesc &&other) = delete;
  VolumeDesc &operator=(VolumeDesc &&other) = delete;

  friend std::ostream &operator<<(std::ostream &out, const VolumeDesc &v) {
    out << "Volume: " << v.id_ << '\n';
    out << "\t" << v.ip_ << ":" << v.port_ << '\n';
    out << "\t" << v.nsectors_ << " sectors";
    return out;
  }

 private:
  uint32_t id_{};
  std::string ip_;
  int port_{};
  uint64_t nsectors_{};
};

}  // namespace sandook
