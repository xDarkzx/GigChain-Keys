#pragma once

#include "MidiEvent.h"

#include <optional>

namespace gigchain::engine {

// How one channel listens to the keyboard (mirrors core::Channel).
struct RouteSettings
{
    int keyLow = 0;
    int keyHigh = 127;
    int transpose = 0;
    int midiChannel = 0; // 0 = omni, 1..16
    int velocityLow = 1;
    int velocityHigh = 127;
};

// The event as this channel should receive it, or nullopt when the channel
// ignores it. Notes outside the key range, note-ons outside the velocity
// range, and notes transposed outside 0..127 are dropped; controllers, pitch bend and pressure pass through to every layer;
// system messages are dropped. Real-time safe.
std::optional<MidiEvent> routeEvent(const MidiEvent& event, const RouteSettings& route) noexcept;

} // namespace gigchain::engine
