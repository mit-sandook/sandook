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
// Mixed-read/write models are bucketed by write ratio. We currently expect
// 30%/50%/70% write mix profiles.
//
// Backward compatibility:
// - If a 300w model isn't present, we fall back to 250w
// - If a 700w model isn't present, we fall back to 750w
constexpr auto kMixModelSuffix_30 = "_300w.model";
constexpr auto kMixModelSuffix_50 = "_500w.model";
constexpr auto kMixModelSuffix_70 = "_700w.model";
constexpr auto kMixModelSuffixFallback_25 = "_250w.model";
constexpr auto kMixModelSuffixFallback_75 = "_750w.model";

constexpr auto kPeakLoadDampeningFactor = 0.95;
// NOTE: Historically, the model "blew up" latency when queried above the last
// modeled load by multiplying by this factor. We now clamp latency at the
// saturation threshold instead to avoid unstable over-compensation.
[[maybe_unused]] constexpr auto kSaturationMagnificationFactor = 10;
constexpr auto kSaturationLatencyUs = 1000;
// Slightly penalize saturation to discourage operating right at the knee.
constexpr auto kSaturationLatencyPenaltyUs =
    static_cast<uint64_t>(kSaturationLatencyUs * 1.05);

// Write-ratio bucket boundaries are midpoints between the modeled mixes:
// 30% / 50% / 70% writes.
// - < 40% writes  -> use 30% model
// - < 60% writes  -> use 50% model
// - >= 60% writes -> use 70% model
constexpr auto kPercentWrites_40 = 0.40;
constexpr auto kPercentWrites_60 = 0.60;

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

    return GetLatency(cur_load, &load_mix_30_, &latency_mix_30_);
#if 0
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
    if (write_ratio < kPercentWrites_40) {
      return GetLatency(cur_load, &load_mix_30_, &latency_mix_30_);
    }
    if (write_ratio < kPercentWrites_60) {
      return GetLatency(cur_load, &load_mix_50_, &latency_mix_50_);
    }
    return GetLatency(cur_load, &load_mix_70_, &latency_mix_70_);
#endif
  }

  [[nodiscard]] uint64_t GetPeakIOPS(
      ServerMode mode, double write_ratio = kPercentWrites_40) const {
    switch (mode) {
      case ServerMode::kRead:
        return GetPeakIOPS(&load_read_, &latency_read_);

      case ServerMode::kWrite:
        return GetPeakIOPS(&load_write_, &latency_write_);

      case ServerMode::kMix: {
        if (write_ratio < kPercentWrites_40) {
          return GetPeakIOPS(&load_mix_30_, &latency_mix_30_);
        }
        if (write_ratio < kPercentWrites_60) {
          return GetPeakIOPS(&load_mix_50_, &latency_mix_50_);
        }
        return GetPeakIOPS(&load_mix_70_, &latency_mix_70_);
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

  LoadValues load_mix_30_;
  LatencyValues latency_mix_30_;

  LoadValues load_mix_50_;
  LatencyValues latency_mix_50_;

  LoadValues load_mix_70_;
  LatencyValues latency_mix_70_;

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

    auto load_model_with_fallback = [&](const std::string &primary_suffix,
                                        const std::string &fallback_suffix)
        -> Status<LoadLatency> {
      auto primary = std::filesystem::path(Config::kSSDModelsDirPath /
                                          (name + primary_suffix));
      try {
        return LoadModel(primary);
      } catch (...) {
        // Fall back for older datasets (e.g., 250w/750w).
        auto fallback = std::filesystem::path(Config::kSSDModelsDirPath /
                                             (name + fallback_suffix));
        try {
          return LoadModel(fallback);
        } catch (...) {
          return MakeError(EINVAL);
        }
      }
    };

    /* Load 30% writes mixed model. */
    auto model_ret =
        load_model_with_fallback(kMixModelSuffix_30, kMixModelSuffixFallback_25);
    if (!model_ret) {
      return MakeError(model_ret);
    }
    load_mix_30_ = std::move(std::get<0>(*model_ret));
    latency_mix_30_ = std::move(std::get<1>(*model_ret));

    /* Load 50% writes mixed model. */
    try {
      fname = name + kMixModelSuffix_50;
      fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
      model = LoadModel(fpath);
    } catch (...) {
      return MakeError(EINVAL);
    }
    load_mix_50_ = std::move(std::get<0>(model));
    latency_mix_50_ = std::move(std::get<1>(model));

    /* Load 70% writes mixed model. */
    model_ret =
        load_model_with_fallback(kMixModelSuffix_70, kMixModelSuffixFallback_75);
    if (!model_ret) {
      return MakeError(model_ret);
    }
    load_mix_70_ = std::move(std::get<0>(*model_ret));
    latency_mix_70_ = std::move(std::get<1>(*model_ret));

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
      // If offered load exceeds the modeled range, clamp to saturation instead
      // of blowing up. This avoids destabilizing feedback in scheduling.
      cur_latency = kSaturationLatencyPenaltyUs;
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

    // Never return above saturation. The disk model is used as a control signal;
    // clamping prevents extreme/unstable weight swings when close to the knee.
    return std::min<uint64_t>(cur_latency, kSaturationLatencyPenaltyUs);
  }
};

using DiskModels = std::array<DiskModel, kNumMaxServers>;

}  // namespace sandook
