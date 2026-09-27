#pragma once

#include "MidiEvent.h"

#include "gigchain/engine/EngineTypes.h"

#include <array>
#include <atomic>
#include <span>

namespace gigchain::engine {

// Keeps what the keyboard is doing (keys down and how hard, pitch bend, mod
// wheel, sustain) for the screen. The audio thread applies each block's
// events; any thread reads a snapshot. Lock-free: plain atomics.
class MidiMonitor
{
public:
    // Audio thread.
    void apply(std::span<const MidiEvent> events) noexcept
    {
        for (const MidiEvent& e : events) {
            const int type = e.status & 0xF0;
            if (type == 0x90 || type == 0x80) {
                const uint8_t velocity = type == 0x90 ? e.data2 : 0; // a note-on at 0 is a note-off
                m_velocity.at(e.data1 & 0x7F).store(velocity, std::memory_order_relaxed);
            } else if (type == 0xE0) {
                m_pitchBend.store((e.data2 << 7) | e.data1, std::memory_order_relaxed);
            } else if (type == 0xB0 && e.data1 == 1) {
                m_modWheel.store(e.data2, std::memory_order_relaxed);
            } else if (type == 0xB0 && e.data1 == 64) {
                m_sustain.store(e.data2 >= 64, std::memory_order_relaxed);
            } else if (type == 0xB0 && (e.data1 == 120 || e.data1 == 123)) { // all sound / all notes off
                clear();
            }
        }
    }

    // Any thread: every key up, wheels and pedal at rest (Panic).
    void clear() noexcept
    {
        for (auto& v : m_velocity) v.store(0, std::memory_order_relaxed);
        m_sustain.store(false, std::memory_order_relaxed);
    }

    // Any thread.
    [[nodiscard]] MidiActivity read() const noexcept
    {
        MidiActivity now;
        for (std::size_t i = 0; i < now.velocity.size(); ++i) {
            now.velocity.at(i) = m_velocity.at(i).load(std::memory_order_relaxed);
        }
        now.pitchBend = m_pitchBend.load(std::memory_order_relaxed);
        now.modWheel = m_modWheel.load(std::memory_order_relaxed);
        now.sustain = m_sustain.load(std::memory_order_relaxed);
        return now;
    }

private:
    std::array<std::atomic<uint8_t>, 128> m_velocity{};
    std::atomic<int> m_pitchBend{8192};
    std::atomic<int> m_modWheel{0};
    std::atomic<bool> m_sustain{false};
};

} // namespace gigchain::engine
