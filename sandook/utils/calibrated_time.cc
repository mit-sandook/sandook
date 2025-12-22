#include "sandook/utils/calibrated_time.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "base/compiler.h"
#include "sandook/base/error.h"
#include "sandook/base/io.h"
#include "sandook/base/msg.h"
#include "sandook/base/time.h"
#include "sandook/rpc/rpc.h"

namespace sandook::utils {

namespace {

constexpr auto kWarmupCalibrationRounds = 1000;
constexpr auto kCalibrationRounds = 10000;

}  // namespace

int64_t ControllerCalibrationMicroTime = 0;  // NOLINT

void SetControllerTimeCalibration(int64_t delta_us) {
  ControllerCalibrationMicroTime = delta_us;
}

uint64_t CalibratedMicroTime() {
  return MicroTime() + ControllerCalibrationMicroTime;
}

Status<int64_t> CalibrationWorker(RPCClient *ctrl, int rounds) {
  auto req = CreateGetControllerTimeMsg();
  const auto req_size = GetMsgSize(req.get());

  int64_t sum = 0;

  for (int i = 0; i < rounds; i++) {
    barrier();
    const auto server_start_time = MicroTime();
    barrier();

    auto resp = ctrl->Call(writable_span(req.get(), req_size));

    barrier();
    const auto server_end_time = MicroTime();
    barrier();

    if (resp.get_buf().size() < sizeof(GetControllerTimeReplyMsg)) {
      return MakeError(EINVAL);
    }

    const auto *msg = reinterpret_cast<const GetControllerTimeReplyMsg *>(
        const_cast<std::byte *>(
            resp.get_buf().last(sizeof(GetControllerTimeReplyMsg)).data()));

    const auto rtt = server_end_time - server_start_time;
    const auto single_trip = rtt / 2;
    const auto client_start_time = msg->microtime - single_trip;
    const auto delta =
        static_cast<int64_t>(client_start_time - server_start_time);

    sum += delta;
  }

  return sum / rounds;
}

Status<int64_t> CalibrateTimeWithController(RPCClient *ctrl) {
  auto warmup = CalibrationWorker(ctrl, kWarmupCalibrationRounds);
  if (!warmup) {
    return MakeError(warmup);
  }
  return CalibrationWorker(ctrl, kCalibrationRounds);
}

}  // namespace sandook::utils
