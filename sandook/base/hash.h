#pragma once

#include <functional>

namespace sandook {

/* Hashes two 64 bit variables by first taking their XOR.
 */
inline size_t Hash(uint64_t var1, uint64_t var2) {
  return std::hash<uint64_t>{}(var1 ^ var2);
}

/* Hashes a 128 bit variable by dividing it into the two 64 bit halves.
 */
inline size_t Hash(__uint128_t var) {
  return Hash(static_cast<uint64_t>(var), static_cast<uint64_t>(var >> 64));
}

}  // namespace sandook
