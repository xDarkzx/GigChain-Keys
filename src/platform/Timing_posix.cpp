#include "gigchain/platform/Timing.h"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

#include <cstdint>

namespace gigchain::platform {

void preciseTimingForThisThread()
{
    // Linux and macOS timers wake within well under a millisecond already.
}

void endPreciseTimingForThisThread() {}

void flushDenormalsToZeroForThisThread()
{
#if defined(__x86_64__) || defined(__i386__)
    _mm_setcsr(_mm_getcsr() | 0x8040); // flush-to-zero (0x8000), denormals-are-zero (0x40)
#elif defined(__aarch64__)
    // Apple Silicon (and ARM64 Linux): FPCR's flush-to-zero bit (FZ, bit 24).
    std::uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= std::uint64_t{1} << 24;
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#else
#error "flushDenormalsToZeroForThisThread: this processor is not handled"
#endif
}

} // namespace gigchain::platform
