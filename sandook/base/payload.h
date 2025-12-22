#pragma once

#include <cstddef>
#include <cstdlib>
#include <span>
#include <utility>

namespace sandook {

class Payload {
 public:
  explicit Payload(size_t size) : size_(size) {
    if (size == 0) {
      return;
    }
    buf_ = std::malloc(size);  // NOLINT
    view_ = std::span<std::byte>(static_cast<std::byte*>(buf_), size);
  }

  Payload(size_t alignment, size_t size) : size_(size) {
    if (size == 0) {
      return;
    }
    buf_ = std::aligned_alloc(alignment, size);  // NOLINT
    view_ = std::span<std::byte>(static_cast<std::byte*>(buf_), size);
  }

  ~Payload() {
    if (buf_ != nullptr) {
      std::free(buf_);  // NOLINT
    }
  }

  /* No copying. */
  Payload(const Payload&) = delete;
  Payload& operator=(const Payload&) = delete;

  /* Explicit move definition. */
  Payload(Payload&& other) noexcept { *this = std::move(other); }
  Payload& operator=(Payload&& other) noexcept {
    std::swap(size_, other.size_);
    std::swap(buf_, other.buf_);
    std::swap(view_, other.view_);
    return *this;
  }

  [[nodiscard]] std::span<std::byte> view() const { return view_; }
  [[nodiscard]] void* data() const { return view_.data(); }
  [[nodiscard]] size_t size() const { return size_; }

 private:
  /* Warning: must update move definition if any variables are modified. */
  size_t size_{};
  void* buf_{nullptr};
  std::span<std::byte> view_;
};

}  // namespace sandook
