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

// Mixed read/write model file suffixes.
// We support a growing set of mix profiles. Not all SSDs will have all of them;
// we load whatever exists for the disk and then pick conservatively by rounding
// the observed write-ratio *up* to the next available bucket.
//
// Expected naming: "_<pct>w.model" where pct is an integer percent of writes,
// e.g. 100w, 200w, 300w, 400w, 500w, 600w, 700w, 750w.
constexpr auto kMixModelSuffix_10 = "_100w.model";
constexpr auto kMixModelSuffix_20 = "_200w.model";
constexpr auto kMixModelSuffix_25 = "_250w.model";  // legacy
constexpr auto kMixModelSuffix_30 = "_300w.model";
constexpr auto kMixModelSuffix_40 = "_400w.model";
constexpr auto kMixModelSuffix_50 = "_500w.model";
constexpr auto kMixModelSuffix_60 = "_600w.model";
constexpr auto kMixModelSuffix_70 = "_700w.model";
constexpr auto kMixModelSuffix_75 = "_750w.model";  // legacy

constexpr auto kPeakLoadDampeningFactor = 0.95;
// NOTE: Historically, the model "blew up" latency when queried above the last
// modeled load by multiplying by this factor. We now clamp latency at the
// saturation threshold instead to avoid unstable over-compensation.
[[maybe_unused]] constexpr auto kSaturationMagnificationFactor = 10;
constexpr auto kSaturationLatencyUs = 1000;
// Slightly penalize saturation to discourage operating right at the knee.
constexpr auto kSaturationLatencyPenaltyUs =
    static_cast<uint64_t>(kSaturationLatencyUs * 1.05);

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

    // return GetLatency(cur_load, &load_mix_30_, &latency_mix_30_);
    // return GetLatency(cur_load, &load_mix_70_, &latency_mix_70_);

    /* Read-only. */
    if (op == OpType::kRead && mode == ServerMode::kRead) {
      return GetLatency(cur_load, &load_read_, &latency_read_);
    }

    /* Write-only. */
    if (op == OpType::kWrite && mode == ServerMode::kWrite) {
      return GetLatency(cur_load, &load_write_, &latency_write_);
    }

    /* Read-write mixed.
     * Select the closest available write-mix model by rounding write_ratio up.
     */
    const auto &mix = SelectMixModel(write_ratio);
    return GetLatency(cur_load, &mix.load, &mix.latency);
  }

  [[nodiscard]] uint64_t GetPeakIOPS(
      ServerMode mode, double write_ratio = 0.0) const {
    switch (mode) {
      case ServerMode::kRead:
        return GetPeakIOPS(&load_read_, &latency_read_);

      case ServerMode::kWrite:
        return GetPeakIOPS(&load_write_, &latency_write_);

      case ServerMode::kMix: {
        const auto &mix = SelectMixModel(write_ratio);
        return GetPeakIOPS(&mix.load, &mix.latency);
      }

      default:
        throw std::runtime_error("Invalid server mode for obtaining peak load");
    }

    std::unreachable();
  }

 private:
  struct MixModel {
    double write_ratio_bucket;
    LoadValues load;
    LatencyValues latency;
  };

  LoadValues load_read_;
  LatencyValues latency_read_;

  LoadValues load_write_;
  LatencyValues latency_write_;

  std::vector<MixModel> mix_models_;

  [[nodiscard]] const MixModel &SelectMixModel(double write_ratio) const {
    if (mix_models_.empty()) {
      throw std::runtime_error("No mixed R/W models loaded");
    }

    // Clamp to sane range.
    if (write_ratio < 0.0) {
      write_ratio = 0.0;
    }
    if (write_ratio > 1.0) {
      write_ratio = 1.0;
    }

    // Conservative selection: round up to the next available bucket.
    for (const auto &m : mix_models_) {
      if (write_ratio <= m.write_ratio_bucket) {
        return m;
      }
    }
    // If higher than all buckets, use the highest available bucket.
    return mix_models_.back();
  }

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

    /* Load mixed models (best-effort). */
    mix_models_.clear();
    struct MixCandidate {
      double bucket;
      const char *suffix;
    };
    // Keep in ascending bucket order.
    constexpr MixCandidate kMixCandidates[] = {
        {0.10, kMixModelSuffix_10}, {0.20, kMixModelSuffix_20},
        {0.25, kMixModelSuffix_25}, {0.30, kMixModelSuffix_30},
        {0.40, kMixModelSuffix_40}, {0.50, kMixModelSuffix_50},
        {0.60, kMixModelSuffix_60}, {0.70, kMixModelSuffix_70},
        {0.75, kMixModelSuffix_75},
    };

    for (const auto &c : kMixCandidates) {
      const auto fname = name + std::string(c.suffix);
      const auto fpath = std::filesystem::path(Config::kSSDModelsDirPath / fname);
      try {
        model = LoadModel(fpath);
      } catch (...) {
        continue;
      }
      mix_models_.push_back(MixModel{
          .write_ratio_bucket = c.bucket,
          .load = std::move(std::get<0>(model)),
          .latency = std::move(std::get<1>(model)),
      });
    }

    if (mix_models_.empty()) {
      return MakeError(EINVAL);
    }

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
