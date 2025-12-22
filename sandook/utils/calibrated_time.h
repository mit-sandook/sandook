#pragma once

#include <cstdint>

#include "sandook/base/error.h"
#include "sandook/rpc/rpc.h"

namespace sandook::utils {

Status<int64_t> CalibrateTimeWithController(RPCClient *ctrl);
void SetControllerTimeCalibration(int64_t delta_us);
uint64_t CalibratedMicroTime();

}  // namespace sandook::utils
