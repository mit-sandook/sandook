#include "sandook/bindings/log.h"

#include "base/log.h"

namespace sandook::rt {

Logger::~Logger() { logk(level_, "%s", buf_.str().c_str()); }

}  // namespace sandook::rt
