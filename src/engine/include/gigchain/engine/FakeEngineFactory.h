#pragma once

#include "gigchain/engine/IEngine.h"

#include <functional>
#include <memory>

namespace gigchain::engine {

// Monotonic time in seconds. Injected so tests can control the simulation.
using Clock = std::function<double()>;

// The demo engine: a stand-in for the real one, which makes no sound. It is
// NOT how the app plays: the real engine (createRealEngine, in
// internal/RealEngine) hosts your VST3 plugins and plays them through your
// audio interface. The demo engine exists for two reasons:
//   - Tests. The UI is tested on every build against an engine that answers
//     the same way every time, needs no sound card, no MIDI keyboard and no
//     plugins (the build servers have none), and can be driven from the test:
//     a fixed list of demo instruments, meters that move, notes and knobs
//     played as if from a keyboard.
//   - A safety net. When the real engine cannot start (no audio device at
//     all), the app still opens so the setlist and charts can be read and
//     edited, and says plainly that it is running without sound (main.cpp).
std::unique_ptr<IEngine> createFakeEngine();
std::unique_ptr<IEngine> createFakeEngine(Clock clock);

} // namespace gigchain::engine
