#pragma once

namespace gigchain::platform {

// For a thread that must wake on time (the MIDI clock's): 1 ms scheduling
// and the highest thread priority where the system needs asking (Windows'
// default wait is 15.6 ms); nothing where timers are precise already.
void preciseTimingForThisThread();
// When that thread is done: the system's timing back as it was.
void endPreciseTimingForThisThread();

// For the audio thread: denormal floats become zero (a decaying reverb tail
// otherwise costs huge CPU). The processor's own switch: x86's MXCSR
// (flush-to-zero and denormals-are-zero), ARM64's FPCR (flush-to-zero).
void flushDenormalsToZeroForThisThread();

} // namespace gigchain::platform
