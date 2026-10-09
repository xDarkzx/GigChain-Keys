#pragma once

#include <array>
#include <cstddef>

namespace gigchain::core {

// A channel's MIDI effects (MainStage's Chord Trigger and Arpeggiator):
// what one key plays, before the instrument hears it.

// The chord a key plays (its intervals in semitones from the key).
enum class ChordTrigger : int { None = 0, Major, Minor, Power, Octaves, Sus2, Sus4, Seventh };
inline constexpr int kChordTriggerCount = 8;

struct ChordIntervals
{
    std::array<int, 4> steps{};
    std::size_t count = 0;
};

[[nodiscard]] constexpr ChordIntervals chordIntervals(int shape) noexcept
{
    switch (static_cast<ChordTrigger>(shape)) {
    case ChordTrigger::Major: return {{0, 4, 7, 0}, 3};
    case ChordTrigger::Minor: return {{0, 3, 7, 0}, 3};
    case ChordTrigger::Power: return {{0, 7, 12, 0}, 3};
    case ChordTrigger::Octaves: return {{0, 12, 0, 0}, 2};
    case ChordTrigger::Sus2: return {{0, 2, 7, 0}, 3};
    case ChordTrigger::Sus4: return {{0, 5, 7, 0}, 3};
    case ChordTrigger::Seventh: return {{0, 4, 7, 10}, 4};
    case ChordTrigger::None: break;
    }
    return {{0, 0, 0, 0}, 1};
}

// The order an arpeggiator plays the held keys in.
enum class ArpPattern : int { Off = 0, Up, Down, UpDown, AsPlayed };
inline constexpr int kArpPatternCount = 5;

// How often it steps, as a note length.
enum class ArpRate : int { Quarter = 0, Eighth, EighthTriplet, Sixteenth };
inline constexpr int kArpRateCount = 4;

[[nodiscard]] constexpr double arpStepQuarters(int rate) noexcept
{
    switch (static_cast<ArpRate>(rate)) {
    case ArpRate::Quarter: return 1.0;
    case ArpRate::EighthTriplet: return 1.0 / 3.0;
    case ArpRate::Sixteenth: return 0.25;
    case ArpRate::Eighth: break;
    }
    return 0.5;
}

inline constexpr int kMaxArpOctaves = 3;

} // namespace gigchain::core
