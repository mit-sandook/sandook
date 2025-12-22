#pragma once

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>

std::ostream& operator<<(std::ostream& os, std::byte b) {
  return os << to_integer<int>(b);
}

void LogBytes(const std::span<const std::byte> buf) {
  std::cout << std::hex << std::setfill('0');
  for (const auto& b : buf) {
    std::cout << "0x" << std::setw(2) << b << " ";
  }
  std::cout << std::endl;
}
