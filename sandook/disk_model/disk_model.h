#pragma once

#include <array>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sandook/base/constants.h"
#include "sandook/base/error.h"
#include "sandook/base/io_desc.h"
#include "sandook/base/server_stats.h"
#include "sandook/bindings/log.h"
#include "sandook/config/config.h"

constexpr auto kReadOnlyModelSuffix = "_100r.model";
constexpr auto kWriteOnlyModelSuffix = "_1000w.model";
constexpr auto kMixModelSuffix_25 = "_250w.model";
constexpr auto kMixModelSuffix_50 = "_500w.model";
constexpr auto kMixModelSuffix_75 = "_750w.model";

constexpr auto kPeakLoadDampeningFactor = 0.95;
constexpr auto kSaturationMagnificationFactor = 10;
constexpr auto kSaturationLatencyUs = 1000;

constexpr auto kPercentWrites_25 = 0.25;
constexpr auto kPercentWrites_50 = 0.50;

namespace sandook {

using LoadValues = std::vector<uint64_t>;
using LatencyValues = std::vector<uint64_t>;
using LoadLatency = std::pair<LoadValues, LatencyValues>;

class DiskModel {
 public:
  explicit DiskModel(const std::string &name) {
    if (!LoadModels(name)) {
      LOG(WARN) << "Cannot load disk model for: " << name;
      throw std::runtime_error("Cannot load model for: " + name);
    }
  }

  DiskModel() = default;
  ~DiskModel() = default;

  [[nodiscard]] uint64_t GetLatency(uint64_t cur_load, OpType op,
                                    ServerMode mode, double write_ratio) const {
    /* Read-only. */
    if (op == OpType::kRead && mode == ServerMode::kRead) {
      return GetLatency(cur_load, &load_read_, &latency_read_);
    }

    /* Write-only. */
    if (op == OpType::kWrite && mode == ServerMode::kWrite) {
      return GetLatency(cur_load, &load_write_, &latency_write_);
    }

    /* Read-write mixed.
     * Using a little lower margins to err more on the side of higher writes.
     */
    if (write_ratio < kPercentWrites_25) {
      return GetLatency(cur_load, &load_mix_25_, &latency_mix_25_);
    }
    if (write_ratio < kPercentWrites_50) {
      return GetLatency(cur_load, &load_mix_50_, &latency_mix_50_);
    }
    return GetLatency(cur_load, &load_mix_75_, &latency_mix_75_);
  }

  [[nodiscard]] uint64_t GetPeakIOPS(
      ServerMode mode, double write_ratio = kPercentWrites_25) const {
    switch (mode) {
      case ServerMode::kRead:
        return GetPeakIOPS(&load_read_, &latency_read_);

      case ServerMode::kWrite:
        return GetPeakIOPS(&load_write_, &latency_write_);

      case ServerMode::kMix: {
        if (write_ratio < kPercentWrites_25) {
          return GetPeakIOPS(&load_mix_25_, &latency_mix_25_);
        }
        if (write_ratio < kPercentWrites_50) {
          return GetPeakIOPS(&load_mix_50_, &latency_mix_50_);
        }
        return GetPeakIOPS(&load_mix_75_, &latency_mix_75_);
      }

      default:
        throw std::runtime_error("Invalid server mode for obtaining peak load");
    }

    std::unreachable();
  }

 private:
  LoadValues load_read_;
  LatencyValues latency_read_;

  LoadValues load_write_;
  LatencyValues latency_write_;

  LoadValues load_mix_25_;
  LatencyValues latency_mix_25_;

  LoadValues load_mix_50_;
  LatencyValues latency_mix_50_;

  LoadValues load_mix_75_;
  LatencyValues latency_mix_75_;

  Status<void> LoadModels(const std::string &name) {
    LoadLatency model;

    /* Load read-only model. */
    auto fname = name + kReadOnlyModelSuffix;
    std::filesystem::path fpath(Config::kSSDModelsDirPath / fname);
    try {
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_read_ = std::move(std::get<0>(model));
    latency_read_ = std::move(std::get<1>(model));

    /* Load write-only model. */
    fname = name + kWriteOnlyModelSuffix;
    fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
    try {
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_write_ = std::move(std::get<0>(model));
    latency_write_ = std::move(std::get<1>(model));

    /* Load 25% writes mixed model. */
    fname = name + kMixModelSuffix_25;
    fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
    try {
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_mix_25_ = std::move(std::get<0>(model));
    latency_mix_25_ = std::move(std::get<1>(model));

    /* Load 50% writes mixed model. */
    fname = name + kMixModelSuffix_50;
    fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
    try {
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_mix_50_ = std::move(std::get<0>(model));
    latency_mix_50_ = std::move(std::get<1>(model));

    /* Load 75% writes mixed model. */
    fname = name + kMixModelSuffix_75;
    fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
    try {
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_mix_75_ = std::move(std::get<0>(model));
    latency_mix_75_ = std::move(std::get<1>(model));

    return {};
  }

  /* Expected format:
   * Load,Latency
   */
  static LoadLatency LoadModel(const std::filesystem::path &fpath) {
    LOG(DEBUG) << "Reading model: " << fpath;

    std::ifstream f(fpath);
    if (!f.is_open() || !f.good()) {
      const std::string fpath_str(fpath);
      throw std::runtime_error("Could not open model file: " + fpath_str);
    }

    LoadLatency result;
    uint64_t load = 0;
    double lat = NAN;
    std::string line;

    /* Ignore the first line with headers. */
    std::getline(f, line);

    while (std::getline(f, line)) {
      std::stringstream ss(line);
      /* First item is load. */
      ss >> load;
      /* Ignore the comma. */
      ss.ignore();
      /* Second item is latency. */
      ss >> lat;

      std::get<0>(result).emplace_back(load);
      std::get<1>(result).emplace_back(lat);
    }

    f.close();

    return result;
  }

  static uint64_t GetPeakIOPS(const LoadValues *load,
                              const LatencyValues *latency) {
    assert(!load->empty());
    assert(!latency->empty());
    assert(load->size() == latency->size());

    // Choose the largest load with latency still below saturation.
    uint64_t ret = load->back();
    for (size_t i = 0; i < load->size(); i++) {
      if (latency->at(i) >= kSaturationLatencyUs) {
        ret = (i == 0) ? 0 : load->at(i - 1);
        break;
      }
    }

    double dampened_load = static_cast<double>(ret) * kPeakLoadDampeningFactor;
    return static_cast<uint64_t>(dampened_load);
  }

  static uint64_t GetLatency(uint64_t cur_load, const LoadValues *load,
                             const LatencyValues *latency) {
    assert(!load->empty());
    assert(!latency->empty());
    assert(load->size() == latency->size());

    /* Start by assuming we are at saturation unless we find a better latency
     * point based on the offered load.
     */
    size_t idx = load->size();
    for (size_t i = 0; i < load->size(); i++) {
      if (load->at(i) >= cur_load) {
        idx = i;
        break;
      }
    }

    uint64_t cur_latency = 0;
    if (idx == load->size()) {
      cur_latency = latency->at(idx - 1) * kSaturationMagnificationFactor;
    } else if (idx == 0) {
      cur_latency = latency->at(idx);
    } else {
      /* Interpolate. */
      const auto start_load = load->at(idx - 1);
      const auto end_load = load->at(idx);
      const auto load_range = static_cast<double>(end_load - start_load);
      const auto cur_load_diff = static_cast<double>(cur_load - start_load);
      const auto cur_pct = cur_load_diff / load_range;
      const auto start_latency = latency->at(idx - 1);
      const auto end_latency = latency->at(idx);
      const auto latency_range =
          static_cast<double>(end_latency - start_latency);
      const auto latency_delta = static_cast<uint64_t>(latency_range * cur_pct);
      cur_latency = start_latency + latency_delta;
    }

    return cur_latency;
  }
};

using DiskModels = std::array<DiskModel, kNumMaxServers>;

}  // namespace sandook
