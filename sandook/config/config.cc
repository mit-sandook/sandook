#include "sandook/config/config.h"

#include <jsoncpp/json/json.h>  // NOLINT
#include <jsoncpp/json/value.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sandook/base/constants.h"
#include "sandook/base/types.h"

namespace sandook {

// NOLINTBEGIN
inline const auto kSandookConfig = getenv("SANDOOK_CONFIG") != nullptr
                                       ? getenv("SANDOOK_CONFIG")
                                       : "build/config.json";
// NOLINTEND

const Json::Value Config::root = Config::LoadConfig();

Json::Value Config::LoadConfig() {
  Json::Value root;
  std::ifstream file(kSandookConfig);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open config.json");
  }
  file >> root;
  std::cout << root << '\n';
  return root;
}

const Config::VirtualDiskType Config::kVirtualDiskType = [](auto &root) {
  if (strcmp(root["kVirtualDiskType"].asCString(), "Remote") == 0) {
    return Config::VirtualDiskType::kRemote;
  }
  if (strcmp(root["kVirtualDiskType"].asCString(), "Local") == 0) {
    return Config::VirtualDiskType::kLocal;
  }
  throw std::runtime_error("Unknown virtual disk type");
}(root);
const std::string Config::kVirtualDiskIP =
    Config::kVirtualDiskType == Config::VirtualDiskType::kRemote
        ? root["kVirtualDiskIP"].asString()
        : "";
const int Config::kVirtualDiskPort =
    Config::kVirtualDiskType == Config::VirtualDiskType::kRemote
        ? root["kVirtualDiskPort"].asInt()
        : 0;
const ServerID Config::kVirtualDiskServerAffinity =
    Config::kVirtualDiskType == Config::VirtualDiskType::kRemote
        ? [](auto &root) {
            const auto affinity = root["kVirtualDiskServerAffinity"].asUInt();
            if (affinity < kInvalidServerID || affinity >= kNumMaxServers) {
              throw std::runtime_error("Invalid affinity");
            }
            return affinity;
          }(root)
        : kInvalidServerID;

const std::string Config::kControllerIP = root["kControllerIP"].asString();
const int Config::kControllerPort = root["kControllerPort"].asInt();
const std::filesystem::path Config::kSSDModelsDirPath = [](auto &root) {
  std::filesystem::path dir = root["kSSDModelsDirPath"].asString();
  if (!std::filesystem::exists(dir)) {
    throw std::runtime_error("SSDModelsDirPath does not exist");
  }
  return dir;
}(root);
const bool Config::kDiskServerRejections =
    root["kDiskServerRejections"].asBool();

const std::string Config::kStorageServerIP =
    root["kStorageServerIP"].asString();
const int Config::kStorageServerPort = root["kStorageServerPort"].asInt();

const Config::ControlPlaneSchedulerType Config::kControlPlaneSchedulerType =
    [](auto &root) {
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(), "NoOp") == 0) {
        return Config::ControlPlaneSchedulerType::kNoOp;
      }
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(),
                 "ProfileGuided") == 0) {
        return Config::ControlPlaneSchedulerType::kProfileGuided;
      }
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(),
                 "RWIsolationStrict") == 0) {
        return Config::ControlPlaneSchedulerType::kRWIsolationStrict;
      }
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(),
                 "RWIsolationWeak") == 0) {
        return Config::ControlPlaneSchedulerType::kRWIsolationWeak;
      }
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(),
                 "AdaptiveRWIsolationWeak") == 0) {
        return Config::ControlPlaneSchedulerType::kAdaptiveRWIsolationWeak;
      }
      if (strcmp(root["kControlPlaneSchedulerType"].asCString(),
                 "ProfileGuidedRWIsolation") == 0) {
        return Config::ControlPlaneSchedulerType::kProfileGuidedRWIsolation;
      }
      throw std::runtime_error("Unknown control plane scheduler type");
    }(root);

const Config::DataPlaneSchedulerType Config::kDataPlaneSchedulerType =
    [](auto &root) {
      if (strcmp(root["kDataPlaneSchedulerType"].asCString(),
                 "WeightedReadWrite") == 0) {
        return Config::DataPlaneSchedulerType::kWeightedReadWrite;
      }
      if (strcmp(root["kDataPlaneSchedulerType"].asCString(),
                 "RandomReadWrite") == 0) {
        return Config::DataPlaneSchedulerType::kRandomReadWrite;
      }
      throw std::runtime_error("Unknown data plane scheduler type");
    }(root);

const Config::DiskServerBackend Config::kDiskServerBackend = [](auto &root) {
  if (strcmp(root["kDiskServerBackend"].asCString(), "POSIX") == 0) {
    return Config::DiskServerBackend::kPOSIX;
  }
  if (strcmp(root["kDiskServerBackend"].asCString(), "Memory") == 0) {
    return Config::DiskServerBackend::kMemory;
  }
  if (strcmp(root["kDiskServerBackend"].asCString(), "SPDK") == 0) {
    return Config::DiskServerBackend::kSPDK;
  }
  throw std::runtime_error("Unknown disk server backend");
}(root);

}  // namespace sandook
