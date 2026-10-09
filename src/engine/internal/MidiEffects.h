#pragma once

#include "INode.h"
#include "MidiEvent.h"

#include "gigchain/core/MidiEffects.h"

#include <array>
#include <cstddef>
#include <span>

namespace gigchain::engine {

// A channel's MIDI effects settings (mirrors core::Channel).
struct MidiEffectSettings
{
    int chord = 0;      // core::ChordTrigger
    int arpeggio = 0;   // core::ArpPattern
    int arpRate = 1;    // core::ArpRate
    int arpOctaves = 1; // 1..core::kMaxArpOctaves

    [[nodiscard]] bool any() const noexcept { return chord != 0 || arpeggio != 0; }
};

// Plays a channel's chord trigger and arpeggiator on the audio thread: the
// keys it was given become the notes the instrument hears. The arpeggiator
// steps on the song's beat grid (tempo-synced), starting at once on the
// first key. Real-time safe: fixed storage, no allocation.
class MidiEffects
{
public:
    static constexpr std::size_t kMaxHeld = 32;

    explicit MidiEffects(MidiEffectSettings settings = {}) : m_settings(settings) {}
    [[nodiscard]] const MidiEffectSettings& settings() const noexcept { return m_settings; }

    // `in` (sorted by offset, the channel's own) into `out` (sorted, up to
    // its size); returns how many were written. `frames` long block at `time`.
    std::size_t process(std::span<const MidiEvent> in, std::span<MidiEvent> out, int frames, const TimeInfo& time) noexcept;

private:
    // A key (after the chord) going down or up, for the arpeggiator.
    void hold(int note, int velocity) noexcept;
    void release(int note) noexcept;
    // The note the arpeggiator plays at step `step` (-1: none held).
    [[nodiscard]] int arpNote(long long step) const noexcept;

    MidiEffectSettings m_settings;
    std::array<int, kMaxHeld> m_held{}; // keys down, in the order played
    std::size_t m_heldCount = 0;
    int m_velocity = 100;      // the last key's
    int m_playing = -1;        // the arpeggiated note sounding (-1: none)
    double m_offAt = 0.0;      // when it ends (quarters)
    long long m_step = 0;      // steps played since the first key
    double m_nextStep = -1.0;  // when the next step plays (quarters; below 0: at the next key)
    uint8_t m_status = 0x90;   // the channel's note-on status (its MIDI channel)
};

} // namespace gigchain::engine
