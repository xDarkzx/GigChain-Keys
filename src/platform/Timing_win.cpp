#include "gigchain/platform/Timing.h"

#include <windows.h>
#include <timeapi.h>
#include <xmmintrin.h>

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

void flushDenormalsToZeroForThisThread()
{
    _mm_setcsr(_mm_getcsr() | 0x8040); // flush-to-zero (0x8000), denormals-are-zero (0x40)
}

} // namespace gigchain::platform
