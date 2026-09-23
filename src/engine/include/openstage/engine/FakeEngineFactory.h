#pragma once

#include "openstage/engine/IEngine.h"

#include <functional>
#include <memory>

namespace openstage::engine {

// Monotonic time in seconds. Injected so tests can control the simulation.
using Clock = std::function<double()>;

// An engine that makes no sound: fixed demo plugin list, simulated meters,
// CPU and MIDI activity. Used until the real engine exists (sub-project 4).
std::unique_ptr<IEngine> createFakeEngine();
std::unique_ptr<IEngine> createFakeEngine(Clock clock);

} // namespace openstage::engine
