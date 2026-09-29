#include "gigchain/platform/Timing.h"

#include <windows.h>
#include <timeapi.h>

namespace gigchain::platform {

void preciseTimingForThisThread()
{
    // 1 ms scheduling for this thread's waits (Windows' default is 15.6 ms).
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

void endPreciseTimingForThisThread()
{
    timeEndPeriod(1);
}

} // namespace gigchain::platform
