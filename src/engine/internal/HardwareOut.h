#pragma once

#include "MidiEvent.h"
#include "MidiQueue.h"

#include <QString>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <utility>

namespace gigchain::engine {

// A hardware synth a channel plays (core::Channel::midiOutPort): the audio
// thread puts the strip's keys in its queue, on the synth's MIDI channel,
// and a thread of the engine's sends them (RealEngine). Only the audio
// thread pushes; only that thread pops.
class HardwareOut
{
public:
    HardwareOut(QString port, int midiChannel)
        : m_port(std::move(port)), m_channel(static_cast<uint8_t>(std::clamp(midiChannel, 1, 16) - 1))
    {
    }

    [[nodiscard]] const QString& port() const { return m_port; }
    [[nodiscard]] int midiChannel() const { return m_channel + 1; }

    // Audio thread: the strip's events this block. `newNotes` false (muted,
    // or a tail of an earlier patch): no new notes, but a held note's
    // note-off and the controllers still go, so nothing hangs on the synth.
    // Clock and system messages never go. A full queue drops (counted).
    void push(std::span<const MidiEvent> events, bool newNotes) noexcept
    {
        // Asked to let go (releaseHeld): every note it holds ends first.
        if (m_releaseHeld.exchange(false, std::memory_order_relaxed)) {
            for (std::size_t note = 0; note < m_held.size(); ++note) {
                if (m_held.at(note) == 0) continue;
                const MidiEvent off{.status = static_cast<uint8_t>(0x80 | m_channel), .data1 = static_cast<uint8_t>(note)};
                if (m_queue.push(off)) {
                    m_held.at(note) = 0;
                } else {
                    m_dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        for (const MidiEvent& event : events) {
            const uint8_t kind = event.status & 0xF0;
            if (kind < 0x80 || kind == 0xF0) continue;
            MidiEvent out = event;
            out.status = static_cast<uint8_t>(kind | m_channel);
            const bool on = kind == 0x90 && event.data2 > 0;
            const bool off = kind == 0x80 || (kind == 0x90 && event.data2 == 0);
            const std::size_t note = event.data1 & 0x7F;
            if (on && !newNotes) continue;
            uint8_t& holders = m_held.at(note);
            if (off && holders == 0) continue; // its note-on never went
            // The note came from more than one MIDI channel, all on the synth's one: it ends with the last.
            if (off && holders > 1) {
                --holders;
                continue;
            }
            if (!m_queue.push(out)) {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (on && holders < UINT8_MAX) ++holders;
            if (off) holders = 0;
        }
        m_holding.store(std::ranges::any_of(m_held, [](uint8_t holders) { return holders > 0; }), std::memory_order_relaxed);
    }

    // Any thread: every note it holds ends at its next block. For a strip
    // whose MIDI effects change how keys map to notes (the song's key, key
    // chords on or off): notes sent under the old mapping would never get
    // their note-off from the new one.
    void releaseHeld() noexcept { m_releaseHeld.store(true, std::memory_order_relaxed); }

    // Whether a note it sent is still held on the synth (its strip rings on as a tail until let go).
    [[nodiscard]] bool holdsNotes() const noexcept { return m_holding.load(std::memory_order_relaxed); }

    // The sender's thread: the next message to send.
    bool pop(MidiEvent& event) noexcept { return m_queue.pop(event); }
    // Messages left out since the last call (the queue was full).
    uint64_t takeDropped() noexcept { return m_dropped.exchange(0, std::memory_order_relaxed); }

private:
    QString m_port;
    uint8_t m_channel; // 0-15
    MidiQueue m_queue;
    std::array<uint8_t, 128> m_held{}; // audio thread: how many note-ons each note has sent and not yet ended
    std::atomic<bool> m_holding{false};
    std::atomic<bool> m_releaseHeld{false};
    std::atomic<uint64_t> m_dropped{0};
};

} // namespace gigchain::engine
