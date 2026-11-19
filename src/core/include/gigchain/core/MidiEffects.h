#pragma once

#include <array>
#include <cstddef>

namespace gigchain::core {

// A channel's MIDI effects (MainStage's Chord Trigger and Arpeggiator):
// what one key plays, before the instrument hears it.

// The chord a key plays (its intervals in semitones from the key). InKey:
// the chord of the song's key built on it (keyChordIntervals), Sunday Keys'
// Easy Chord.
enum class ChordTrigger : int { None = 0, Major, Minor, Power, Octaves, Sus2, Sus4, Seventh, InKey };
inline constexpr int kChordTriggerCount = 9;

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
    case ChordTrigger::InKey: return {{0, 12, 0, 0}, 2}; // (without a key: keyChordIntervals)
    case ChordTrigger::None: break;
    }
    return {{0, 0, 0, 0}, 1};
}

// Below this key (C3) a key-chord is voiced open and low: root, fifth and the
// third an octave up, warm under the band; from it up, a close triad.
inline constexpr int kOpenVoicingBelow = 48;

// ChordTrigger::InKey: the chord of the song's key built on `note` (0-127).
// `keyRoot` is the key's pitch class (C = 0), or -1 for no key; `minor`: a
// minor key (natural minor). In D: D major, E minor, F# minor, G major, A
// major, B minor, C# diminished. A note outside the key, or no key at all,
// plays itself and the octave above. Add the octave above with the thumb and
// its chord sounds an octave higher, over the first.
[[nodiscard]] constexpr ChordIntervals keyChordIntervals(int note, int keyRoot, bool minor) noexcept
{
    constexpr ChordIntervals kOctave{{0, 12, 0, 0}, 2};
    if (keyRoot < 0 || keyRoot > 11) return kOctave;
    // The third and fifth above each step of the scale, by semitones above the key's root (0: not in the key).
    constexpr std::array<std::array<int, 2>, 12> kMajor{{{4, 7}, {0, 0}, {3, 7}, {0, 0}, {3, 7}, {4, 7},
                                                         {0, 0}, {4, 7}, {0, 0}, {3, 7}, {0, 0}, {3, 6}}};
    constexpr std::array<std::array<int, 2>, 12> kMinor{{{3, 7}, {0, 0}, {3, 6}, {4, 7}, {0, 0}, {3, 7},
                                                         {0, 0}, {3, 7}, {4, 7}, {0, 0}, {4, 7}, {0, 0}}};
    const int step = (((note - keyRoot) % 12) + 12) % 12;
    const auto [third, fifth] = (minor ? kMinor : kMajor).at(static_cast<std::size_t>(step));
    if (third == 0) return kOctave;
    if (note < kOpenVoicingBelow) return {{0, fifth, 12 + third, 0}, 3};
    return {{0, third, fifth, 0}, 3};
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
