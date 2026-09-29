#pragma once

#include "MidiEvent.h"
#include "RenderGraph.h"

#include "gigchain/engine/EngineTypes.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <span>

namespace gigchain::engine {

// Follows a song's chords as they are played, by the five rules of
// docs/superpowers/specs/2026-09-29-chord-follow-design.md: which chord of
// the song is being played, and the section gate that switches instruments
// on the note that entered a section.
//
// process() runs on the audio thread (no allocation, no locks);
// jumpToSection(), reset() and position() on any thread.
class ChordFollower
{
public:
    // Audio thread, each block. `map` null: not following. A map with another
    // `generation` than the last starts fresh: at its resumeAt, else waiting
    // for its first chord. The gate: the section in force at the block's
    // start (`before`) and from `switchAt` on (`after`).
    SectionGate process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events, int frames,
                        double sampleRate) noexcept;
    // The notes moved at this block's switch (see SectionGate::handover).
    [[nodiscard]] std::span<const MidiEvent> handover() const noexcept { return {m_handover.data(), m_handoverCount}; }

    // Any thread.
    void jumpToSection(int section) noexcept { m_jumpAsked.store(section, std::memory_order_release); }
    void reset() noexcept { m_resetAsked.store(true, std::memory_order_release); }
    [[nodiscard]] ChordFollowPosition position() const noexcept;

    // How long a key let go still counts (broken chords, arpeggios).
    static constexpr double kMemorySeconds = 0.5;
    // Keys landing this soon after a chord was heard are the rest of it (a
    // chord's keys are never struck all at once), not a new strike.
    static constexpr double kChordSpreadSeconds = 0.1;

private:
    static constexpr int64_t kLongAgo = INT64_MIN / 2;

    void clear() noexcept;
    [[nodiscard]] int sectionInForce(const ChordFollowMap& map) const noexcept;
    // `key` went down at `now`: whether the chart moved (rules 1 to 3).
    // `memory` and `spread`: kMemorySeconds and kChordSpreadSeconds in samples.
    bool hear(const ChordFollowMap& map, int key, int64_t now, int64_t memory, int64_t spread) noexcept;
    void fillHandover(int64_t switchTime, int offset) noexcept;
    void publish(const ChordFollowMap* map) noexcept;

    std::array<uint8_t, 128> m_velocity{};   // down: its velocity; 0 = up
    std::array<uint8_t, 128> m_status{};     // the note-on's status (its MIDI channel)
    std::array<int64_t, 128> m_pressedAt{};  // when it went down (samples)
    std::array<int64_t, 128> m_releasedAt{}; // when it was let go; -1 = not recently
    std::array<bool, 128> m_sustained{};     // let go while the pedal was down
    std::bitset<128> m_sinceChord;           // pressed since the last chord heard
    bool m_pedal = false;
    int64_t m_now = 0;
    int m_step = -1;      // the chord being played; -1 = not started
    uint64_t m_candidates = 0; // sections whose first chord was just heard clearly (rule 3)
    int64_t m_heardAt = kLongAgo; // when the last chord was heard (samples)
    uint64_t m_generation = 0;
    std::array<MidiEvent, kMaxEventsPerBlock> m_handover{};
    std::size_t m_handoverCount = 0;

    std::atomic<int> m_jumpAsked{-1};
    std::atomic<bool> m_resetAsked{false};
    std::atomic<bool> m_outActive{false};
    std::atomic<bool> m_outStarted{false};
    std::atomic<int> m_outStep{-1};
    std::atomic<int> m_outSection{-1};
};

} // namespace gigchain::engine
