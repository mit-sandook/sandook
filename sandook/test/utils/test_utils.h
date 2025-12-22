#pragma once

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

inline constexpr auto kMeasureRounds = 100000;

using us = std::chrono::duration<double, std::micro>;
using BenchResults = std::vector<std::pair<const std::string, us>>;

inline int GetMeasureRounds() {
  static int measure_rounds;

  if (measure_rounds != 0) {
    return measure_rounds;
  }

  char *env = getenv("MEASURE_ROUNDS");  // NOLINT
  if (env == nullptr) {
    measure_rounds = kMeasureRounds;
  } else {
    measure_rounds = std::stoi(std::string(env));
  }

  std::cout << "Measure rounds: " << measure_rounds << '\n';

  return measure_rounds;
}

inline void StoreResult(BenchResults *results, const std::string &name,
                        us time) {
  time /= GetMeasureRounds();
  results->emplace_back(name, time);
}

inline void PrintResult(const std::string &name, us time) {
  std::cout << "test '" << name << "' took " << time.count() << " us (for "
            << GetMeasureRounds() << " rounds.)" << '\n';
  time /= GetMeasureRounds();
  std::cout << "test '" << name << "' took " << time.count()
            << " us (per round.)" << '\n';
}

inline bool Bench(const std::string &name,
                  const std::function<bool(int, void *)> &fn, void *args,
                  BenchResults *results) {
  const int measure_rounds = GetMeasureRounds();

  auto start = std::chrono::steady_clock::now();
  const bool pass = fn(measure_rounds, args);
  if (!pass) {
    return false;
  }
  auto finish = std::chrono::steady_clock::now();

  auto t = std::chrono::duration_cast<us>(finish - start);
  PrintResult(name, t);
  StoreResult(results, name, t);

  return true;
}

inline void PrintAllResults(const BenchResults *results) {
  if (results->empty()) {
    return;
  }

  /* Print benchmark names. */
  {
    for (size_t i = 0; i < results->size() - 1; i++) {
      const auto &[k, v] = results->at(i);
      std::cerr << k << ",";
    }
    const auto &[k, v] = results->at(results->size() - 1);
    std::cerr << k << '\n';
  }

  /* Print benchmark values. */
  {
    for (size_t i = 0; i < results->size() - 1; i++) {
      const auto &[k, v] = results->at(i);
      std::cerr << v << ",";
    }
    const auto &[k, v] = results->at(results->size() - 1);
    std::cerr << v << '\n';
  }
}
