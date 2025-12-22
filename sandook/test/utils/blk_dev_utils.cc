#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "base/compiler.h"
extern "C" {
#include <fcntl.h>
#include <stdio.h>
}

#include <iostream>
#include <random>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/test/utils/blk_dev_utils.h"
#include "sandook/test/utils/test_utils.h"

inline constexpr auto kPayloadSize = 1 << sandook::kSectorShift;
inline constexpr auto kAlignment = 1 << sandook::kSectorShift;

/* Capping to a few sectors to make the test faster since it needs to start by
 * writing to all the sectors before running any tests.
 */
inline constexpr auto kMaxSectors = 1ULL << 10;

bool FillBlockDevice(const std::string &dev_path) {
  /* Write random bytes into the entire device to prepare for the rest of the
   * tests.
   */
  void *buf = std::aligned_alloc(kAlignment, kPayloadSize);  // NOLINT
  char *payload_buf = static_cast<char *>(buf);

  std::random_device rand_payload;
  for (int i = 0; i < kPayloadSize; i++) {
    payload_buf[i] = static_cast<char>(rand_payload());  // NOLINT
  }
  auto payload = sandook::writable_span(payload_buf, kPayloadSize);

  const int fd = open(dev_path.c_str(), O_RDWR | O_DIRECT);
  if (fd < 0) {
    std::cerr << "Cannot open " << dev_path << '\n';  // NOLINT
    std::cerr << std::strerror(errno) << '\n';        // NOLINT
    return false;
  }

  for (uint64_t i = 0; i < kMaxSectors; i++) {
    const off_t off = static_cast<off_t>(i) * kPayloadSize;
    if (lseek(fd, off, SEEK_SET) == -1) {
      std::cerr << "Cannot seek" << '\n';  // NOLINT
      return false;
    }
    auto ret = sandook::WriteFull(fd, payload);
    if (!ret) {
      std::cerr << "write: " << ret.error() << '\n';
      return false;
    }
  }

  close(fd);
  std::free(payload_buf);  // NOLINT

  return true;
}

struct BenchReadArgs {
  /* Number of sectors in the disk. */
  uint64_t nsectors;

  /* Sectors to access in this benchmark.
   * This should contain measure_rounds entries.
   */
  std::vector<uint64_t> sectors;

  /* fd of the block device. */
  int fd;
};

bool BenchRead(int measure_rounds, const void *args_ptr) {
  const auto *args = reinterpret_cast<const BenchReadArgs *>(args_ptr);

  /* Since we are using O_DIRECT, we need to pass a buffer aligned to the block
   * size of the device (likely kAlignment).
   */
  // NOLINTNEXTLINE
  char *buf = static_cast<char *>(std::aligned_alloc(kAlignment, kPayloadSize));
  if (buf == nullptr) {
    return false;
  }

  auto payload = sandook::readable_span(buf, kPayloadSize);

  bool pass = true;

  for (int i = 0; i < measure_rounds; i++) {
    const off_t off = static_cast<off_t>(args->sectors[i])
                      << sandook::kSectorShift;
    if (lseek(args->fd, off, SEEK_SET) == -1) {
      std::cerr << "lseek: " << std::strerror(errno) << '\n';  // NOLINT
    }
    auto r = sandook::ReadFull(args->fd, payload);
    if (unlikely(!r)) {
      std::cerr << "Cannot read: " << r.error() << '\n';  // NOLINT
      pass = false;
      break;
    }
  }

  std::free(buf);  // NOLINT

  return pass;
}

struct BenchWriteArgs {
  /* Number of sectors in the virtual disk. */
  uint64_t nsectors;

  /* Sectors to access in this benchmark.
   * This should contain measure_rounds entries.
   */
  std::vector<uint64_t> sectors;

  /* Payload to write. */
  std::span<const std::byte> payload;

  /* fd of the block device. */
  int fd;
};

bool BenchWrite(int measure_rounds, const void *args_ptr) {
  const auto *args = reinterpret_cast<const BenchWriteArgs *>(args_ptr);

  bool pass = true;

  for (int i = 0; i < measure_rounds; i++) {
    const off_t off = static_cast<off_t>(args->sectors[i])
                      << sandook::kSectorShift;
    if (lseek(args->fd, off, SEEK_SET) == -1) {
      std::cerr << "lseek: " << std::strerror(errno) << '\n';  // NOLINT
    }
    auto r = sandook::WriteFull(args->fd, args->payload);
    if (unlikely(!r)) {
      std::cerr << "Cannot write: " << r.error() << '\n';  // NOLINT
      pass = false;
      break;
    }
  }

  return pass;
}

bool BenchBlockDeviceRead(const std::string &dev_path, BenchResults *results) {
  const int measure_rounds = GetMeasureRounds();

  BenchReadArgs args{.nsectors = kMaxSectors};
  args.sectors.resize(measure_rounds);

  /* Open the block device in O_DIRECT mode to bypass the page cache. */
  args.fd = open(dev_path.c_str(), O_RDONLY | O_DIRECT);
  if (args.fd < 0) {
    std::cerr << "Cannot open " << dev_path << '\n';  // NOLINT
    std::cerr << std::strerror(errno) << '\n';        // NOLINT
    return false;
  }

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, args.nsectors - 1);
  for (auto &sector : args.sectors) {
    sector = dist(rng);
  }

  const bool pass =
      Bench("BenchRead", BenchRead, reinterpret_cast<void *>(&args), results);

  close(args.fd);

  return pass;
}

bool BenchBlockDeviceWrite(const std::string &dev_path, BenchResults *results) {
  const int measure_rounds = GetMeasureRounds();

  BenchWriteArgs args{.nsectors = kMaxSectors};
  args.sectors.resize(measure_rounds);

  /* Open the block device in O_DIRECT mode to bypass the page cache. */
  args.fd = open(dev_path.c_str(), O_WRONLY | O_DIRECT);
  if (args.fd < 0) {
    std::cerr << "Cannot open " << dev_path << '\n';  // NOLINT
    std::cerr << std::strerror(errno) << '\n';        // NOLINT
    return false;
  }

  /* Since we are using O_DIRECT, we need to pass a buffer aligned to the block
   * size of the device (likely kAlignment).
   */
  // NOLINTNEXTLINE
  char *buf = static_cast<char *>(std::aligned_alloc(kAlignment, kPayloadSize));
  if (buf == nullptr) {
    return false;
  }

  /* Create a random set of sectors to access. */
  std::random_device rand_sectors;
  std::mt19937 rng(rand_sectors());
  std::uniform_int_distribution<std::mt19937::result_type> dist(
      0, args.nsectors - 1);
  for (auto &sector : args.sectors) {
    sector = dist(rng);
  }

  /* Fill the buffer to write. */
  std::random_device rand_payload;
  for (size_t i = 0; i < args.payload.size(); i++) {
    buf[i] = static_cast<char>(rand_payload());  // NOLINT
  }
  args.payload = sandook::writable_span(buf, kPayloadSize);

  const bool pass =
      Bench("BenchWrite", BenchWrite, reinterpret_cast<void *>(&args), results);

  std::free(buf);  // NOLINT
  close(args.fd);

  return pass;
}
