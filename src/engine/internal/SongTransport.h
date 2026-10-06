#pragma once

#include "RenderGraph.h"

#include "gigchain/engine/EngineTypes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace gigchain::engine {

// The song as played: its parts (the flow: a section each time it comes
// round, a chorus played twice is two parts), where each starts in quarter
// notes from bar 1 of the first, and which section each part plays.
// Built on the main thread, read-only once handed to the audio thread.
struct SongTimeline
{
    std::vector<double> starts;  // one per part, then where the last one ends
    std::vector<int> sections;   // per part: the section it plays
    double quartersPerBar = 4.0;
    double lead = 0.25; // how far before its first beat a part takes over

    [[nodiscard]] int count() const { return starts.empty() ? 0 : static_cast<int>(starts.size()) - 1; }
    [[nodiscard]] int sectionOf(int part) const
    {
        return part >= 0 && part < count() && std::cmp_less(part, sections.size()) ? sections.at(static_cast<std::size_t>(part)) : part;
    }
    // The part in force at `ppq` (never before `from`, the part the song was
    // started at: a count-in belongs to it), in a straight run through.
    [[nodiscard]] int sectionAt(double ppq, int from = 0) const noexcept
    {
        int part = std::clamp(from, 0, std::max(count() - 1, 0));
        while (part + 1 < count() && ppq + lead >= starts.at(static_cast<std::size_t>(part) + 1)) ++part;
        return part;
    }
    // Each section once, in order, of `bars` bars each. `lead` in quarter notes.
    static SongTimeline fromBars(const std::vector<int>& bars, double quartersPerBar, double lead)
    {
        std::vector<int> sections(bars.size());
        for (std::size_t i = 0; i < bars.size(); ++i) sections.at(i) = static_cast<int>(i);
        return fromParts(sections, bars, quartersPerBar, lead);
    }
    // The parts `parts` (each a section of `sectionBars`) in playing order.
    static SongTimeline fromParts(const std::vector<int>& parts, const std::vector<int>& sectionBars, double quartersPerBar,
                                  double lead)
    {
        SongTimeline timeline;
        timeline.quartersPerBar = quartersPerBar;
        timeline.lead = lead;
        double at = 0.0;
        timeline.starts.reserve(parts.size() + 1);
        for (const int section : parts) {
            if (section < 0 || std::cmp_greater_equal(section, sectionBars.size())) continue;
            timeline.starts.push_back(at);
            timeline.sections.push_back(section);
            at += std::max(sectionBars.at(static_cast<std::size_t>(section)), 1) * quartersPerBar;
        }
        timeline.starts.push_back(at);
        return timeline;
    }
};

// The song's play/stop/jump and its live controls, counted on the audio
// thread. The main thread asks (play, stop, jump, queue...) and reads
// position(); advance() runs once per block and says which section is in
// force, sample-exactly, and what the backing track should do.
//
// Live controls land on the next bar line (Next part, Go to part), or when
// the part ends (Repeat, Hold, Stop at end): the band stays in time.
class SongTransport
{
public:
    // Main thread.
    void play(int fromPart, bool countIn) noexcept { post(kPlay, fromPart, countIn); }
    // (Its own flag: a stop and then a jump asked together both happen.)
    void stop() noexcept { m_stopAsked.store(true, std::memory_order_release); }
    // Now. Playing: the count moves there. Stopped: that is where the song
    // is (and where Play starts).
    void jump(int part) noexcept { post(kJump, part, false); }
    // On the next bar line: the next part (pressed again: not).
    void queueNext() noexcept { m_askNext.store(true, std::memory_order_release); }
    // On the next bar line: that part (the same part asked again: not).
    void queuePart(int part) noexcept { m_askPart.store(std::max(part, 0), std::memory_order_release); }
    // When the part ends: once more (each call one more time).
    void queueRepeat() noexcept { m_askRepeat.fetch_add(1, std::memory_order_acq_rel); }
    // The part loops until this is asked again.
    void toggleHold() noexcept { m_askHold.store(true, std::memory_order_release); }
    // Stops when the part ends (asked again: not).
    void toggleStopAtEnd() noexcept { m_askStopAtEnd.store(true, std::memory_order_release); }
    // Nothing queued any more.
    void cancelQueued() noexcept { m_askCancel.store(true, std::memory_order_release); }

    [[nodiscard]] SongPosition position() const noexcept
    {
        return SongPosition{.playing = m_outPlaying.load(std::memory_order_relaxed),
                            .countingIn = m_outCountingIn.load(std::memory_order_relaxed),
                            .section = m_outSection.load(std::memory_order_relaxed),
                            .bar = m_outBar.load(std::memory_order_relaxed),
                            .bars = m_outBars.load(std::memory_order_relaxed),
                            .part = m_outPart.load(std::memory_order_relaxed),
                            .quarter = m_outQuarter.load(std::memory_order_relaxed),
                            .queuedPart = m_outQueuedPart.load(std::memory_order_relaxed),
                            .repeats = m_outRepeats.load(std::memory_order_relaxed),
                            .hold = m_outHold.load(std::memory_order_relaxed),
                            .stopAtEnd = m_outStopAtEnd.load(std::memory_order_relaxed)};
    }

    struct Block
    {
        SectionGate gate;
        double ppq = 0.0;           // where the block starts (moved by play or a jump)
        bool seekTrack = false;     // put the backing track at `trackQuarter`
        double trackQuarter = 0.0;  // quarter notes from the first part's bar 1
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
            // Stopped just after the next part's sound came in: that part is in force.
            if (m_playing && m_landing.armed && m_landing.part >= 0 && ppq >= m_landing.switchAt - kEpsilon) m_part = m_landing.part;
            m_playing = false;
            m_trackPending = false;
            clearQueue();
        }
        if (count == 0) {
            takeRequests();
            // Everything plays. (A moment without sections while the patch
            // changes keeps the count going.)
            clearQueue();
            publish(false, false, -1, -1, 0, 0, 0.0);
            return block;
        }
        const int asked = std::clamp(static_cast<int>((command >> 8) & 0xFF), 0, count - 1);
        switch (command & 0x3) {
        case kPlay:
            m_from = asked;
            m_part = asked;
            m_playing = true;
            clearQueue();
            block.ppq = startOf(*timeline, asked) - (((command & kCountIn) != 0) ? timeline->quartersPerBar : 0.0);
            block.seekTrack = true;
            block.trackQuarter = startOf(*timeline, asked);
            block.stopTrack = true; // until bar 1 (after the count-in)
            m_trackPending = true;
            break;
        case kJump:
            m_part = asked;
            clearQueue();
            if (m_playing) {
                m_from = asked;
                block.ppq = startOf(*timeline, asked);
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
        takeRequests(); // (after Play: what is asked with it is for this run)
        m_part = std::clamp(m_part, 0, count - 1);
        if (m_goTo >= count) m_goTo = -1;

        if (!m_playing) {
            const int section = timeline->sectionOf(m_part);
            block.gate = SectionGate{.before = section, .after = section, .switchAt = 0};
            m_landing = Landing{};
            publish(false, false, section, m_part, 0, barsOf(*timeline, m_part), 0.0);
            return block;
        }

        // A landing reached (the part's end, or a queued bar line): on from there.
        if (m_landing.armed && block.ppq >= m_landing.at - kEpsilon) land(*timeline, block);
        if (!m_playing) {
            const int section = timeline->sectionOf(m_part);
            block.gate = SectionGate{.before = section, .after = section, .switchAt = 0};
            publish(false, false, section, m_part, 0, barsOf(*timeline, m_part), 0.0);
            return block;
        }

        const double start = block.ppq;
        const double end = start + (frames * quartersPerSample);
        // Where the count goes next (kept once its sound has switched).
        if (!m_landing.armed || start < m_landing.switchAt - kEpsilon) m_landing = nextLanding(*timeline, start);
        const int now = timeline->sectionOf(m_part);
        const int then = m_landing.part >= 0 ? timeline->sectionOf(m_landing.part) : now;
        const bool switched = start >= m_landing.switchAt - kEpsilon;
        block.gate = SectionGate{.before = switched ? then : now, .after = switched ? then : now, .switchAt = frames};
        if (!switched && end > m_landing.switchAt + kEpsilon && quartersPerSample > 0.0) {
            block.gate.after = then;
            if (then != now) block.gate.switchAt = firstSampleAtOrPast(m_landing.switchAt, start, quartersPerSample, frames);
        }
        if (block.gate.after == block.gate.before) block.gate.switchAt = frames;

        // The backing track starts on bar 1 of the part played from.
        const double first = startOf(*timeline, m_from);
        if (m_trackPending && end - first > kEpsilon) { // (a block ending right on bar 1 is before it)
            block.startTrackAt =
                start >= first || quartersPerSample <= 0.0
                    ? 0
                    : std::min(firstSampleAtOrPast(first, start, quartersPerSample, frames), frames - 1);
            m_trackPending = false;
        }

        const double from = startOf(*timeline, m_part);
        const bool countingIn = end - first <= kEpsilon && m_part == m_from;
        const int bars = barsOf(*timeline, m_part);
        const int bar = countingIn ? 0 : static_cast<int>(std::floor((std::max(end, from) - from) / timeline->quartersPerBar)) + 1;
        publish(true, countingIn, now, m_part, std::clamp(bar, 0, bars), bars, std::max(0.0, end - from));
        return block;
    }

private:
    static constexpr uint32_t kPlay = 1;
    static constexpr uint32_t kJump = 2;
    static constexpr uint32_t kCountIn = 0x4;
    // Quarter notes: summing block lengths is not exact.
    static constexpr double kEpsilon = 1e-9;
    static constexpr int kStopHere = -2; // a landing that stops the count
    static constexpr int kSongEnd = -3;  // the end of the last part: the count stops, its sound stays

    enum class Kind : uint8_t { Natural, Next, GoTo, Repeat, Hold, Stop, End };
    // Where the count goes next: at `at` (quarter notes), on to `part`; its
    // sound switches at `switchAt` (a bar line, or a part's start less the lead).
    struct Landing
    {
        bool armed = false;
        double at = 0.0;
        double switchAt = 0.0;
        int part = -1; // or kStopHere / kSongEnd
        Kind kind = Kind::Natural;
    };

    void post(uint32_t kind, int part, bool countIn) noexcept
    {
        m_command.store(kind | (countIn ? kCountIn : 0U) | (static_cast<uint32_t>(std::clamp(part, 0, 255)) << 8),
                        std::memory_order_release);
    }
    static double startOf(const SongTimeline& timeline, int part) { return timeline.starts.at(static_cast<std::size_t>(part)); }
    // The first sample of a block starting at `start` that is at or past
    // `at` (both in quarter notes). The clock is a running sum of block
    // lengths, a hair off exact: within a millionth of a sample counts as on it.
    static int firstSampleAtOrPast(double at, double start, double quartersPerSample, int frames)
    {
        const double samples = (at - start) / quartersPerSample;
        return std::clamp(static_cast<int>(std::ceil(samples - 1e-6)), 0, frames);
    }
    static int barsOf(const SongTimeline& timeline, int part)
    {
        const auto p = static_cast<std::size_t>(part);
        return static_cast<int>(std::lround((timeline.starts.at(p + 1) - timeline.starts.at(p)) / timeline.quartersPerBar));
    }

    void clearQueue() noexcept
    {
        m_next = false;
        m_goTo = -1;
        m_repeats = 0;
        m_hold = false;
        m_stopAtEnd = false;
        m_landing = Landing{};
    }
    void takeRequests() noexcept
    {
        if (m_askCancel.exchange(false, std::memory_order_acq_rel)) clearQueue();
        if (m_askNext.exchange(false, std::memory_order_acq_rel)) {
            m_next = !m_next;
            m_goTo = -1;
        }
        if (const int part = m_askPart.exchange(-1, std::memory_order_acq_rel); part >= 0) {
            m_goTo = part == m_goTo ? -1 : part;
            m_next = false;
        }
        m_repeats += m_askRepeat.exchange(0, std::memory_order_acq_rel);
        if (m_askHold.exchange(false, std::memory_order_acq_rel)) m_hold = !m_hold;
        if (m_askStopAtEnd.exchange(false, std::memory_order_acq_rel)) m_stopAtEnd = !m_stopAtEnd;
    }

    // Where the count goes from `start` (the part in force: m_part).
    Landing nextLanding(const SongTimeline& timeline, double start) const noexcept
    {
        const int count = timeline.count();
        const double partStart = startOf(timeline, m_part);
        const double partEnd = startOf(timeline, m_part + 1);
        // Next part / Go to part: on the next bar line (bar 1, counting in).
        if (m_next || m_goTo >= 0) {
            const double barLine = start < partStart ? partStart
                                                     : std::ceil((start - partStart - kEpsilon) / timeline.quartersPerBar) *
                                                               timeline.quartersPerBar +
                                                           partStart;
            const int target = m_goTo >= 0 ? m_goTo : m_part + 1;
            if (barLine < partEnd - kEpsilon) {
                return Landing{.armed = true,
                               .at = barLine,
                               .switchAt = barLine,
                               .part = target < count ? target : kStopHere,
                               .kind = m_goTo >= 0 ? Kind::GoTo : Kind::Next};
            }
        }
        // At the part's end.
        Landing landing{.armed = true, .at = partEnd, .switchAt = partEnd - timeline.lead, .part = m_part + 1, .kind = Kind::Natural};
        if (m_stopAtEnd) {
            landing.part = kStopHere;
            landing.kind = Kind::Stop;
        } else if (m_goTo >= 0) {
            landing.part = m_goTo;
            landing.kind = Kind::GoTo;
        } else if (m_next) {
            landing.kind = Kind::Next;
        } else if (m_hold) {
            landing.part = m_part;
            landing.kind = Kind::Hold;
        } else if (m_repeats > 0) {
            landing.part = m_part;
            landing.kind = Kind::Repeat;
        }
        if (landing.part == count) {
            landing.part = kSongEnd;
            landing.kind = Kind::End;
        }
        return landing;
    }

    // The count reached m_landing at the start of `block`: move it on.
    void land(const SongTimeline& timeline, Block& block) noexcept
    {
        const Landing landing = m_landing;
        m_landing = Landing{};
        switch (landing.kind) {
        case Kind::Next: m_next = false; break;
        case Kind::GoTo: m_goTo = -1; break;
        case Kind::Repeat: --m_repeats; break;
        case Kind::Stop: m_stopAtEnd = false; break;
        case Kind::Natural:
        case Kind::Hold:
        case Kind::End: break;
        }
        if (landing.part == kSongEnd) {
            m_playing = false;
            return;
        }
        if (landing.part == kStopHere) {
            m_playing = false;
            block.stopTrack = true;
            return;
        }
        // On to another part: what was queued for this one is done.
        if (landing.part != m_part) {
            m_repeats = 0;
            m_hold = false;
        }
        const double over = block.ppq - landing.at; // (a block starts a hair past it)
        const double target = startOf(timeline, landing.part) + over;
        if (std::abs(startOf(timeline, landing.part) - landing.at) > kEpsilon) {
            block.ppq = target;
            block.seekTrack = true;
            block.trackQuarter = target;
        }
        if (m_trackPending) {
            block.startTrackAt = 0;
            m_trackPending = false;
        }
        m_part = landing.part;
        m_from = landing.part;
    }

    void publish(bool playing, bool countingIn, int section, int part, int bar, int bars, double quarter) noexcept
    {
        m_outPlaying.store(playing, std::memory_order_relaxed);
        m_outCountingIn.store(countingIn, std::memory_order_relaxed);
        m_outSection.store(section, std::memory_order_relaxed);
        m_outPart.store(part, std::memory_order_relaxed);
        m_outBar.store(bar, std::memory_order_relaxed);
        m_outBars.store(bars, std::memory_order_relaxed);
        m_outQuarter.store(quarter, std::memory_order_relaxed);
        m_outQueuedPart.store(m_next ? m_part + 1 : m_goTo, std::memory_order_relaxed);
        m_outRepeats.store(m_repeats, std::memory_order_relaxed);
        m_outHold.store(m_hold, std::memory_order_relaxed);
        m_outStopAtEnd.store(m_stopAtEnd, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> m_command{0}; // the latest play or jump (main thread -> audio thread)
    std::atomic<bool> m_stopAsked{false};
    std::atomic<bool> m_askNext{false};
    std::atomic<int> m_askPart{-1};
    std::atomic<int> m_askRepeat{0};
    std::atomic<bool> m_askHold{false};
    std::atomic<bool> m_askStopAtEnd{false};
    std::atomic<bool> m_askCancel{false};
    // Audio thread only.
    bool m_playing = false;
    bool m_trackPending = false;
    int m_from = 0; // the part the song was started (or jumped) at
    int m_part = 0; // in force now
    bool m_next = false;
    int m_goTo = -1;
    int m_repeats = 0;
    bool m_hold = false;
    bool m_stopAtEnd = false;
    Landing m_landing;
    // For position() (audio thread -> main thread).
    std::atomic<bool> m_outPlaying{false};
    std::atomic<bool> m_outCountingIn{false};
    std::atomic<int> m_outSection{-1};
    std::atomic<int> m_outPart{-1};
    std::atomic<int> m_outBar{0};
    std::atomic<int> m_outBars{0};
    std::atomic<double> m_outQuarter{0.0};
    std::atomic<int> m_outQueuedPart{-1};
    std::atomic<int> m_outRepeats{0};
    std::atomic<bool> m_outHold{false};
    std::atomic<bool> m_outStopAtEnd{false};
};

} // namespace gigchain::engine
