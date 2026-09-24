#pragma once

#include "MidiEvent.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace gigchain::engine {

// Lock-free single-producer / single-consumer queue: one MIDI input thread
// pushes, the audio thread pops. Use one queue per MIDI port. When full,
// push() drops the event and returns false.
//
// The two indices sit on separate cache lines so producer and consumer do not
// contend; MSVC warns about that intentional padding (C4324).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
class MidiQueue
{
public:
    static constexpr int capacity() { return static_cast<int>(kSize) - 1; }

    bool push(const MidiEvent& event) noexcept
    {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kSize;
        if (next == m_tail.load(std::memory_order_acquire)) return false;
        m_items[head] = event;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiEvent& event) noexcept
    {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;
        event = m_items[tail];
        m_tail.store((tail + 1) % kSize, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 1024;
    std::array<MidiEvent, kSize> m_items{};
    alignas(64) std::atomic<uint32_t> m_head{0};
    alignas(64) std::atomic<uint32_t> m_tail{0};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace gigchain::engine
