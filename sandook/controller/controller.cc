#include "sandook/controller/controller.h"

#include <csignal>
#include <memory>

#include "sandook/bindings/log.h"
#include "sandook/config/config.h"
#include "sandook/controller/controller_agent.h"
#include "sandook/controller/controller_conn_handler.h"
#include "sandook/rpc/rpc.h"

std::unique_ptr<sandook::ControllerAgent> ctrl_;  // NOLINT

namespace sandook {

void SignalHandler(int sig) { ctrl_->HandleSignal(sig); }

void Controller::Launch() {
  ctrl_ = std::make_unique<ControllerAgent>();

  std::signal(SIGTERM, SignalHandler);

  ControllerConnHandler handler(ctrl_.get());
  RPCServerInit(&handler, Config::kControllerPort,
                []() { LOG(INFO) << "Controller is ready..."; });
}

}  // namespace sandook
