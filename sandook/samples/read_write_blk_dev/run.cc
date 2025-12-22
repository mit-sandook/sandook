extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
}

#include <argparse/argparse.hpp>
#include <cstring>
#include <fstream>
#include <iostream>

#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/log.h"

using namespace sandook;

bool PerformSeek(int fd, int offset) {
  off_t ret = lseek(fd, offset, SEEK_SET);
  if (ret == -1) {
    std::cerr << "Cannot lseek: " << std::strerror(errno) << std::endl;
    return false;
  }
  return true;
}

int PerformWrite(int fd, const std::string &payload) {
  Status<void> ret =
      WriteFull(fd, writable_span(payload.c_str(), payload.size()));
  if (!ret) {
    std::cerr << "Failed to write" << std::endl;
    return -1;
  }

  std::cout << "Wrote " << payload.size() << " bytes" << std::endl;
  LogBytes(writable_span(payload.c_str(), payload.size()));

  return 0;
}

int PerformRead(int fd, int len) {
  std::vector<std::byte> payload(len);
  char *buf = reinterpret_cast<char *>(payload.data());
  Status<void> ret = ReadFull(fd, readable_span(buf, payload.size()));
  if (!ret) {
    std::cerr << "Failed to read payload" << std::endl;
    return -1;
  }

  std::cout << "Read " << len << " bytes" << std::endl;
  LogBytes(payload);

  return 0;
}

int PerformAction(const std::string &device, bool is_write,
                  const std::string &payload, int offset, int len) {
  int fd = open(device.c_str(), O_RDWR);
  if (fd >= 0) {
    std::cout << "Opened device: fd = " << fd << std::endl;
  } else {
    std::cerr << "Failed to open: " << std::strerror(errno) << std::endl;
    return 1;
  }

  int ret = 0;
  if (PerformSeek(fd, offset)) {
    if (is_write) {
      ret = PerformWrite(fd, payload);
    } else {
      ret = PerformRead(fd, len);
    }
  } else {
    return -1;
  }

  close(fd);

  return ret;
}

int main(int argc, char *argv[]) {
  argparse::ArgumentParser program("read_write_blkdev");
  program.add_argument("-d", "--device")
      .default_value(std::string("/dev/ublkb0"))
      .required()
      .help("specify the block device.");
  program.add_argument("-w", "--write")
      .implicit_value(true)
      .default_value(false)
      .help("perform write operation.");
  program.add_argument("-p", "--payload")
      .default_value(std::string("foobar"))
      .help("payload string to write (if --write specified).");
  program.add_argument("-o", "--offset")
      .default_value(0)
      .scan<'i', int>()
      .help("offset to read/write");
  program.add_argument("-l", "--length")
      .default_value(0)
      .scan<'i', int>()
      .help("number of bytes to read (if --write is NOT specified).");
  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  auto device = program.get<std::string>("--device");
  auto is_write = program.get<bool>("--write");
  auto payload = program.get<std::string>("--payload");
  auto offset = program.get<int>("--offset");
  auto len = program.get<int>("--length");
  std::cout << "Parameters:" << std::endl;
  std::cout << "\tDevice: " << device << std::endl;
  if (is_write) {
    std::cout << "\tOperation: Write" << std::endl;
    std::cout << "\tPayload: " << payload << std::endl;
  } else {
    std::cout << "\tOperation: Read" << std::endl;
    std::cout << "\tlength: " << len << std::endl;
  }
  std::cout << "\tOffset: " << offset << std::endl;
  std::cout << std::endl;

  if (PerformAction(device, is_write, payload, offset, len)) {
    std::cerr << "Failed!" << std::endl;
  } else {
    std::cout << "Succeeded!" << std::endl;
  }

  return 0;
}
