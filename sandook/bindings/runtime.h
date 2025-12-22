#pragma once

#include <functional>
#include <string>
#include <utility>

extern "C" {
#include <base/init.h>
#include <runtime/runtime.h>
#include <runtime/thread.h>
}
#include <runtime.h>

namespace sandook::rt {

inline bool RuntimeInitialized() { return base_init_done && thread_self(); }

inline int RuntimeInit(std::string cfg_path, std::function<void()> main_func) {
  return ::rt::RuntimeInit(std::move(cfg_path), std::move(main_func));
}

// Gets the maximum number of cores the runtime could run on.
inline unsigned int RuntimeMaxCores() { return runtime_max_cores(); }

}  // namespace sandook::rt
