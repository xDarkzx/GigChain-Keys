#pragma once

#include "RenderGraph.h"

#include "gigchain/engine/EngineTypes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gigchain::engine {

// Where a song's sections start, in quarter notes from bar 1 of the first.
// Built on the main thread, read-only once handed to the audio thread.
struct SongTimeline
{
    std::vector<double> starts; // one per section, then where the last one ends
    double quartersPerBar = 4.0;
    double lead = 0.25; // how far before its first beat a section takes over

    [[nodiscard]] int count() const { return starts.empty() ? 0 : static_cast<int>(starts.size()) - 1; }
    // The section in force at `ppq` (never before `from`, the section the
    // song was started at: a count-in belongs to it).
    [[nodiscard]] int sectionAt(double ppq, int from = 0) const noexcept
    {
        int section = std::clamp(from, 0, std::max(count() - 1, 0));
        while (section + 1 < count() && ppq + lead >= starts.at(static_cast<std::size_t>(section) + 1)) ++section;
        return section;
    }
    // Built from each section's bars. `lead` in quarter notes.
    static SongTimeline fromBars(const std::vector<int>& bars, double quartersPerBar, double lead)
    {
        SongTimeline timeline;
        timeline.quartersPerBar = quartersPerBar;
        timeline.lead = lead;
        double at = 0.0;
        timeline.starts.reserve(bars.size() + 1);
        for (const int n : bars) {
            timeline.starts.push_back(at);
            at += std::max(n, 1) * quartersPerBar;
        }
        timeline.starts.push_back(at);
        return timeline;
    }
};

// The song's play/stop/jump, counted on the audio thread. The main thread
// asks (play, stop, jump) and reads position(); advance() runs once per
// block and says which section is in force, sample-exactly, and what the
// backing track should do.
class SongTransport
{
public:
    // Main thread.
    void play(int from, bool countIn) noexcept { post(kPlay, from, countIn); }
    // (Its own flag: a stop and then a jump asked together both happen.)
    void stop() noexcept { m_stopAsked.store(true, std::memory_order_release); }
    // Playing: jump there now. Stopped: that is where the song is (and
    // where Play starts).
    void jump(int section) noexcept { post(kJump, section, false); }
    [[nodiscard]] SongPosition position() const noexcept
    {
        return SongPosition{.playing = m_outPlaying.load(std::memory_order_relaxed),
                            .countingIn = m_outCountingIn.load(std::memory_order_relaxed),
                            .section = m_outSection.load(std::memory_order_relaxed),
                            .bar = m_outBar.load(std::memory_order_relaxed),
                            .bars = m_outBars.load(std::memory_order_relaxed)};
    }

    struct Block
    {
        SectionGate gate;
        double ppq = 0.0;           // where the block starts (moved by play or a jump)
        bool seekTrack = false;     // put the backing track at `trackQuarter`
        double trackQuarter = 0.0;  // quarter notes from the first section's bar 1
        int startTrackAt = -1;      // start the backing track at this sample of the block
        bool stopTrack = false;
    };

    // Audio thread. `timeline` null or empty: the song has no sections.
    Block advance(const SongTimeline* timeline, double ppq, int frames, double quartersPerSample) noexcept
    {
        Block block;
        block.ppq = ppq;
        const bool stopAsked = m_stopAsked.exchange(false, std::memory_order_acq_rel);
        const uint32_t command = m_command.exchange(0, std::memory_order_acq_rel);
        const int count = timeline != nullptr ? timeline->count() : 0;
        if (stopAsked) {
            if (m_playing) block.stopTrack = true;
            m_playing = false;
            m_trackPending = false;
        }
        if (count == 0) {
            // Everything plays. (A moment without sections while the patch
            // changes keeps the count going.)
            publish(false, false, -1, 0, 0);
            return block;
        }
        const int asked = std::clamp(static_cast<int>((command >> 8) & 0xFF), 0, count - 1);
        switch (command & 0x3) {
        case kPlay:
            m_from = asked;
            m_section = asked;
            m_playing = true;
            block.ppq = timeline->starts.at(static_cast<std::size_t>(asked)) - (((command & kCountIn) != 0) ? timeline->quartersPerBar : 0.0);
            block.seekTrack = true;
            block.trackQuarter = timeline->starts.at(static_cast<std::size_t>(asked));
            block.stopTrack = true; // until bar 1 (after the count-in)
            m_trackPending = true;
            break;
        case kJump:
            m_section = asked;
            if (m_playing) {
                m_from = asked;
                block.ppq = timeline->starts.at(static_cast<std::size_t>(asked));
                block.seekTrack = true;
                block.trackQuarter = block.ppq;
                if (m_trackPending) {
                    block.startTrackAt = 0;
                    m_trackPending = false;
                }
            }
            break;
        default: break;
        }
        m_section = std::clamp(m_section, 0, count - 1);

        if (!m_playing) {
            block.gate = SectionGate{.before = m_section, .after = m_section, .switchAt = 0};
            publish(false, false, m_section, 0, barsOf(*timeline, m_section));
            return block;
        }

        const double start = block.ppq;
        const double end = start + (frames * quartersPerSample);
        const int before = timeline->sectionAt(start, m_from);
        const int after = timeline->sectionAt(end, m_from);
        block.gate = SectionGate{.before = before, .after = after, .switchAt = frames};
        if (after != before && quartersPerSample > 0.0) {
            // The first sample at or past the new section's switch point.
            const double edge = timeline->starts.at(static_cast<std::size_t>(after)) - timeline->lead;
            block.gate.switchAt = firstSampleAtOrPast(edge, start, quartersPerSample, frames);
        }
        // The backing track starts on bar 1 of the section played from.
        const double first = timeline->starts.at(static_cast<std::size_t>(m_from));
        if (m_trackPending && end - first > kEpsilon) { // (a block ending right on bar 1 is before it)
            block.startTrackAt =
                start >= first || quartersPerSample <= 0.0
                    ? 0
                    : std::min(firstSampleAtOrPast(first, start, quartersPerSample, frames), frames - 1);
            m_trackPending = false;
        }
        m_section = after;

        // Past the end of the last section: the count stops, the last
        // section stays in force.
        const double songEnd = timeline->starts.back();
        if (end >= songEnd - kEpsilon) {
            m_playing = false;
            m_section = count - 1;
        }
        const bool countingIn = end - first <= kEpsilon;
        const double from = timeline->starts.at(static_cast<std::size_t>(m_section));
        const int bar = countingIn ? 0 : static_cast<int>(std::floor((std::max(end, from) - from) / timeline->quartersPerBar)) + 1;
        const int bars = barsOf(*timeline, m_section);
        publish(m_playing, countingIn && m_playing, m_section, std::min(bar, bars), bars);
        return block;
    }

private:
    static constexpr uint32_t kPlay = 1;
    static constexpr uint32_t kJump = 2;
    static constexpr uint32_t kCountIn = 0x4;
    // Quarter notes: summing block lengths is not exact.
    static constexpr double kEpsilon = 1e-9;

    void post(uint32_t kind, int section, bool countIn) noexcept
    {
        m_command.store(kind | (countIn ? kCountIn : 0U) | (static_cast<uint32_t>(std::clamp(section, 0, 255)) << 8),
                        std::memory_order_release);
    }
    // The first sample of a block starting at `start` that is at or past
    // `at` (both in quarter notes). The clock is a running sum of block
    // lengths, a hair off exact: within a millionth of a sample counts as on it.
    static int firstSampleAtOrPast(double at, double start, double quartersPerSample, int frames)
    {
        const double samples = (at - start) / quartersPerSample;
        return std::clamp(static_cast<int>(std::ceil(samples - 1e-6)), 0, frames);
    }
    static int barsOf(const SongTimeline& timeline, int section)
    {
        const auto s = static_cast<std::size_t>(section);
        return static_cast<int>(std::lround((timeline.starts.at(s + 1) - timeline.starts.at(s)) / timeline.quartersPerBar));
    }
    void publish(bool playing, bool countingIn, int section, int bar, int bars) noexcept
    {
        m_outPlaying.store(playing, std::memory_order_relaxed);
        m_outCountingIn.store(countingIn, std::memory_order_relaxed);
        m_outSection.store(section, std::memory_order_relaxed);
        m_outBar.store(bar, std::memory_order_relaxed);
        m_outBars.store(bars, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> m_command{0}; // the latest play or jump (main thread -> audio thread)
    std::atomic<bool> m_stopAsked{false};
    // Audio thread only.
    bool m_playing = false;
    bool m_trackPending = false;
    int m_from = 0;    // the section the song was started (or jumped) at
    int m_section = 0; // in force now
    // For position() (audio thread -> main thread).
    std::atomic<bool> m_outPlaying{false};
    std::atomic<bool> m_outCountingIn{false};
    std::atomic<int> m_outSection{-1};
    std::atomic<int> m_outBar{0};
    std::atomic<int> m_outBars{0};
};

} // namespace gigchain::engine
