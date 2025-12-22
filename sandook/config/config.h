#pragma once

#include <jsoncpp/json/value.h>

#include <filesystem>
#include <string>

#include "sandook/base/types.h"

namespace sandook {

class Config {
 public:
  enum ControlPlaneSchedulerType {
    kNoOp = 0,
    kProfileGuided = 1,
    kRWIsolationStrict = 2,
    kRWIsolationWeak = 3,
    kAdaptiveRWIsolationWeak = 4,
    kProfileGuidedRWIsolation = 5
  };

  enum DataPlaneSchedulerType {
    kWeightedReadWrite = 0,
    kRandomReadWrite = 1,
    kRandomReadHashWrite = 2,
    kWeightedReadHashWrite = 3
  };

  enum VirtualDiskType { kRemote = 0, kLocal = 1 };

  enum DiskServerBackend { kPOSIX = 0, kMemory = 1, kSPDK = 2 };

  /* Virtual disk configurations. */
  const static VirtualDiskType kVirtualDiskType;

  /* Remote virtual disk configurations. */
  const static std::string kVirtualDiskIP;
  const static int kVirtualDiskPort;
  const static ServerID kVirtualDiskServerAffinity;

  /* Controller configurations. */
  const static std::string kControllerIP;
  const static int kControllerPort;
  const static std::filesystem::path kSSDModelsDirPath;
  const static bool kDiskServerRejections;

  /* Storage server configurations. */
  const static std::string kStorageServerIP;
  const static int kStorageServerPort;
  const static DiskServerBackend kDiskServerBackend;

  /* Scheduling configurations. */
  const static DataPlaneSchedulerType kDataPlaneSchedulerType;
  const static ControlPlaneSchedulerType kControlPlaneSchedulerType;

 private:
  static const Json::Value root;
  static Json::Value LoadConfig();
};

}  // namespace sandook
