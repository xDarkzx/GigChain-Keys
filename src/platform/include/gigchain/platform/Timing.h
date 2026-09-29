#pragma once

namespace gigchain::platform {

// For a thread that must wake on time (the MIDI clock's): 1 ms scheduling
// and the highest thread priority where the system needs asking (Windows'
// default wait is 15.6 ms); nothing where timers are precise already.
void preciseTimingForThisThread();
// When that thread is done: the system's timing back as it was.
void endPreciseTimingForThisThread();

} // namespace gigchain::platform
