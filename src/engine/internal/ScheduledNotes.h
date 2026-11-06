#pragma once

#include "MidiEvent.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gigchain::engine {

// Notes timed to the beat clock (IEngine::scheduleNote: Practice's notes),
// played on their sample rather than when the screen gets round to them.
// The main thread pushes, the audio thread takes those due in each block.
// Lock-free single producer / single consumer; a clear goes through the same
// queue, so notes pushed after it are kept.
class ScheduledNotes
{
public:
    static constexpr std::size_t kHeld = 512; // notes waiting for their time at most
    // Further ahead than this the clock must have jumped back (a song started
    // from its top): note-ons are dropped, note-offs played at once (no note hangs).
    static constexpr double kFarAhead = 64.0;

    // Main thread. False: the queue is full (the note is dropped).
    bool push(const MidiEvent& event, double quarter) noexcept { return put(Item{.event = event, .quarter = quarter, .clear = false}); }
    // Main thread: every note not yet played is forgotten.
    bool clear() noexcept { return put(Item{.event = {}, .quarter = 0.0, .clear = true}); }

    // Audio thread: the notes due in the block from `quarter` (`frames`
    // samples of `quartersPerSample` each) into `out`, each at its sample
    // (one already past: the first); how many. Sorted by sample.
    std::size_t take(double quarter, double quartersPerSample, int frames, std::span<MidiEvent> out) noexcept
    {
        Item item;
        while (pop(item)) {
            if (item.clear) {
                m_count = 0;
            } else if (m_count < kHeld) {
                m_held.at(m_count++) = item;
            } else {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const double end = quarter + (std::max(frames, 0) * quartersPerSample);
        std::size_t taken = 0;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < m_count; ++i) {
            const Item& held = m_held.at(i);
            const bool noteOn = (held.event.status & 0xF0) == 0x90 && held.event.data2 > 0;
            const bool jumpedBack = held.quarter > quarter + kFarAhead;
            if (jumpedBack && noteOn) continue; // (dropped)
            const bool due = jumpedBack || held.quarter < end || quartersPerSample <= 0.0;
            if (!due || taken >= out.size()) {
                m_held.at(kept++) = held;
                continue;
            }
            MidiEvent event = held.event;
            const double at = quartersPerSample > 0.0 && !jumpedBack ? (held.quarter - quarter) / quartersPerSample : 0.0;
            // (A hair under a whole sample is that sample: the quarters' sums round.)
            event.sampleOffset = static_cast<int32_t>(std::clamp(std::floor(at + 1e-6), 0.0, static_cast<double>(std::max(frames - 1, 0))));
            out[taken++] = event;
        }
        m_count = kept;
        // In sample order (stable: a note-off before a note-on at the same sample stays first).
        for (std::size_t i = 1; i < taken; ++i) {
            const MidiEvent moving = out[i];
            std::size_t j = i;
            while (j > 0 && out[j - 1].sampleOffset > moving.sampleOffset) {
                out[j] = out[j - 1];
                --j;
            }
            out[j] = moving;
        }
        return taken;
    }

    // Main thread: notes dropped since the last call (the queue or the waiting list was full).
    int takeDropped() noexcept { return m_dropped.exchange(0, std::memory_order_relaxed); }

private:
    struct Item
    {
        MidiEvent event;
        double quarter = 0.0;
        bool clear = false;
    };

    bool put(const Item& item) noexcept
    {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kSize;
        if (next == m_tail.load(std::memory_order_acquire)) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_items.at(head) = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Item& item) noexcept
    {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;
        item = m_items.at(tail);
        m_tail.store((tail + 1) % kSize, std::memory_order_release);
        return true;
    }

    static constexpr uint32_t kSize = 1024;
    std::array<Item, kSize> m_items{};
    alignas(64) std::atomic<uint32_t> m_head{0};
    alignas(64) std::atomic<uint32_t> m_tail{0};
    std::array<Item, kHeld> m_held{}; // audio thread only
    std::size_t m_count = 0;
    std::atomic<int> m_dropped{0};
};

} // namespace gigchain::engine
