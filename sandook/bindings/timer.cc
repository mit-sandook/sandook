#include "sandook/bindings/timer.h"

namespace sandook::rt::timer_internal {

void TimerTrampoline(unsigned long arg) {  // NOLINT
  auto *t = static_cast<timer_node *>(reinterpret_cast<void *>(arg));
  t->Run();
}

}  // namespace sandook::rt::timer_internal
