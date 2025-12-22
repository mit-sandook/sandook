#pragma once

#include <gtest/gtest.h>

namespace sandook::test {

inline ::testing::AssertionResult IsWithinRange(double value, double expected,
                                                double eps) {
  const auto min_value = expected - eps;
  const auto max_value = expected + eps;
  if ((value >= min_value) && (value <= max_value)) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << value << " is outside the range "
                                       << min_value << " to " << max_value;
}

}  // namespace sandook::test
