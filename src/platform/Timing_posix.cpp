#include "gigchain/platform/Timing.h"

namespace gigchain::platform {

void preciseTimingForThisThread()
{
    // Linux and macOS timers wake within well under a millisecond already.
}

void endPreciseTimingForThisThread() {}

} // namespace gigchain::platform
