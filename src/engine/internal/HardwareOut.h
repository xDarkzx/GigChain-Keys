#pragma once

#include "MidiEvent.h"
#include "MidiQueue.h"

#include <QString>

#include <algorithm>
#include <atomic>
#include <bitset>
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
        for (const MidiEvent& event : events) {
            const uint8_t kind = event.status & 0xF0;
            if (kind < 0x80 || kind == 0xF0) continue;
            MidiEvent out = event;
            out.status = static_cast<uint8_t>(kind | m_channel);
            const bool on = kind == 0x90 && event.data2 > 0;
            const bool off = kind == 0x80 || (kind == 0x90 && event.data2 == 0);
            const std::size_t note = event.data1 & 0x7F;
            if (on && !newNotes) continue;
            if (off && !m_held.test(note)) continue; // its note-on never went
            if (!m_queue.push(out)) {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (on) m_held.set(note);
            if (off) m_held.reset(note);
        }
        m_holding.store(m_held.any(), std::memory_order_relaxed);
    }

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
    std::bitset<128> m_held; // audio thread
    std::atomic<bool> m_holding{false};
    std::atomic<uint64_t> m_dropped{0};
};

} // namespace gigchain::engine
