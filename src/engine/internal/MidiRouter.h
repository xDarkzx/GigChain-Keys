#pragma once

#include "MidiEvent.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace gigchain::engine {

// How one channel listens to the keyboard (mirrors core::Channel).
// What a channel can be set to ignore from the keyboard (MainStage's MIDI
// input filter), as bits of RouteSettings::ignores.
namespace midi_filter {
inline constexpr uint32_t kSustain = 1U << 0;    // CC 64
inline constexpr uint32_t kExpression = 1U << 1; // CC 11
inline constexpr uint32_t kModWheel = 1U << 2;   // CC 1
inline constexpr uint32_t kPitchBend = 1U << 3;
inline constexpr uint32_t kAftertouch = 1U << 4; // channel and key pressure
} // namespace midi_filter

struct RouteSettings
{
    int keyLow = 0;
    int keyHigh = 127;
    int transpose = 0;
    int midiChannel = 0; // 0 = omni, 1..16
    int velocityLow = 1;
    int velocityHigh = 127;
    uint32_t ignores = 0; // midi_filter bits
    // Only this keyboard (keyboardSource): 0 = every keyboard. The app's own
    // notes (MidiEvent::source 0) reach it either way.
    uint16_t source = 0;
};

// A keyboard's number for MidiEvent::source and RouteSettings::source, from
// its MIDI input's name: the same name always gets the same number (1 and up)
// for as long as the app runs, so a keyboard unplugged and plugged back in
// is known again. Any thread but the audio thread (it locks).
[[nodiscard]] uint16_t keyboardSource(const QString& name);

// The event as this channel should receive it, or nullopt when the channel
// ignores it. Notes outside the key range, note-ons outside the velocity
// range, and notes transposed outside 0..127 are dropped; controllers, pitch
// bend and pressure pass through to every layer, except those it ignores;
// system messages are dropped. Real-time safe.
std::optional<MidiEvent> routeEvent(const MidiEvent& event, const RouteSettings& route) noexcept;

} // namespace gigchain::engine
