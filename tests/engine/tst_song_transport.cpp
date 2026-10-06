// Counting a song's sections on the audio thread: which section is in force,
// exactly from which sample, and what the backing track does.
#include "SongTransport.h"

#include <QtTest>

#include <atomic>
#include <cstdlib>
#include <new>

// Counts heap allocations made while `t_countAllocations` is set on this
// thread, so the test can prove advance() never allocates.
// (Replacing the global operator new needs global state and malloc/free:
// the only way to see every allocation.)
namespace {
thread_local bool t_countAllocations = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<int> g_allocations{0};            // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

void* operator new(std::size_t size)
{
    if (t_countAllocations) g_allocations.fetch_add(1);
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p; // NOLINT(cppcoreguidelines-no-malloc)
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }                 // NOLINT(cppcoreguidelines-no-malloc)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }    // NOLINT(cppcoreguidelines-no-malloc)

using namespace gigchain::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr double kQuartersPerSample = 120.0 / 60.0 / kRate; // 120 BPM: a quarter note is 24000 samples
constexpr int kBlock = 256;

// Blocks through a transport, as the engine's audio thread runs them.
struct Run
{
    explicit Run(SongTimeline t = {}) : timeline(std::move(t)) {}

    SongTimeline timeline;
    SongTransport transport;
    double ppq = 0.0;
    int64_t sample = 0; // where the next block starts, counted from the first block
    SongTransport::Block last;

    SongTransport::Block step(int frames = kBlock)
    {
        last = transport.advance(&timeline, ppq, frames, kQuartersPerSample);
        ppq = last.ppq + (frames * kQuartersPerSample);
        sample += frames;
        return last;
    }
    // Runs until the gate changes section (within a block, or right on a
    // block's first sample); the absolute sample it changes at, or -1.
    int64_t sampleOfNextSwitch(int maxBlocks = 5000)
    {
        int previous = last.gate.after;
        for (int i = 0; i < maxBlocks; ++i) {
            const int64_t start = sample;
            const auto block = step();
            if (previous >= 0 && block.gate.before != previous) return start;
            if (block.gate.after != block.gate.before) return start + block.gate.switchAt;
            previous = block.gate.after;
        }
        return -1;
    }
};

// Two sections of 2 bars of 4/4 (a bar is 4 quarter notes = 96000 samples).
Run twoSections(double lead = 0.25)
{
    return Run(SongTimeline::fromBars({2, 2}, 4.0, lead));
}

} // namespace

class TestSongTransport : public QObject
{
    Q_OBJECT

private slots:
    void timelineFromBars()
    {
        const SongTimeline t = SongTimeline::fromBars({2, 1, 4}, 3.0, 0.25); // 6/8: 3 quarter notes a bar
        QCOMPARE(t.count(), 3);
        QCOMPARE(t.starts, (std::vector<double>{0.0, 6.0, 9.0, 21.0}));
        QCOMPARE(t.sectionAt(5.74), 0);
        QCOMPARE(t.sectionAt(5.75), 1); // a sixteenth before its first beat
        QCOMPARE(t.sectionAt(100.0), 2);
        QCOMPARE(t.sectionAt(-4.0, 1), 1); // counting in to the second section
    }

    void theSectionChangesASixteenthBeforeItsFirstBeatToTheSample()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        const auto first = run.step();
        QCOMPARE(first.gate.before, 0);
        QCOMPARE(first.gate.after, 0);
        // Bar 3 starts at 8 quarter notes = 192000 samples; a sixteenth
        // (6000 samples) before that is 186000.
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{186000});
        QCOMPARE(run.last.gate.before, 0);
        QCOMPARE(run.last.gate.after, 1);
    }

    void switchingEarlyIsAWholeBeatBefore()
    {
        Run run = twoSections(1.0);
        run.transport.play(0, false);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{168000}); // 7 quarter notes
    }

    void barsOfSixEight()
    {
        Run run(SongTimeline::fromBars({1, 1}, 3.0, 0.25)); // a 6/8 bar: 3 quarter notes
        run.transport.play(0, false);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{66000}); // 2.75 quarter notes
    }

    void aCountInDelaysBarOneAndTheBackingTrack()
    {
        Run run = twoSections();
        run.ppq = 37.3; // wherever the clock was
        run.transport.play(0, true);
        const auto first = run.step();
        QCOMPARE(first.ppq, -4.0); // a bar before bar 1
        QVERIFY(first.seekTrack);
        QCOMPARE(first.trackQuarter, 0.0);
        QVERIFY(first.stopTrack);
        QCOMPARE(first.startTrackAt, -1);
        QCOMPARE(first.gate.before, 0); // the count-in belongs to the first section
        QVERIFY(run.transport.position().countingIn);
        QCOMPARE(run.transport.position().bar, 0);
        // The track starts exactly on bar 1: 4 quarter notes (96000 samples) later.
        int64_t startedAt = -1;
        for (int i = 0; i < 1000 && startedAt < 0; ++i) {
            const int64_t blockStart = run.sample;
            const auto block = run.step();
            if (block.startTrackAt >= 0) startedAt = blockStart + block.startTrackAt;
        }
        QCOMPARE(startedAt, int64_t{96000});
        QVERIFY(!run.transport.position().countingIn);
        QCOMPARE(run.transport.position().bar, 1);
    }

    void playingFromASectionStartsThere()
    {
        Run run = twoSections();
        run.transport.play(1, false);
        const auto first = run.step();
        QCOMPARE(first.ppq, 8.0);
        QCOMPARE(first.gate.before, 1);
        QCOMPARE(first.trackQuarter, 8.0); // the track from the second section's place
        QCOMPARE(first.startTrackAt, 0);
        const SongPosition at = run.transport.position();
        QVERIFY(at.playing);
        QCOMPARE(at.section, 1);
        QCOMPARE(at.bar, 1);
        QCOMPARE(at.bars, 2);
    }

    void stoppedTheSelectedSectionIsInForce()
    {
        Run run = twoSections();
        const auto idle = run.step();
        QCOMPARE(idle.gate.before, 0); // never played: the first section
        run.transport.jump(1);
        const double clock = run.ppq;
        const auto selected = run.step();
        QCOMPARE(selected.gate.before, 1);
        QCOMPARE(selected.gate.after, 1);
        QCOMPARE(selected.ppq, clock); // the clock is not moved
        QVERIFY(!run.transport.position().playing);
        QCOMPARE(run.transport.position().section, 1);
    }

    void aJumpWhilePlayingMovesTheCountAndTheTrack()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        for (int i = 0; i < 10; ++i) run.step();
        run.transport.jump(1);
        const auto jumped = run.step();
        QCOMPARE(jumped.ppq, 8.0);
        QVERIFY(jumped.seekTrack);
        QCOMPARE(jumped.trackQuarter, 8.0);
        QCOMPARE(jumped.gate.before, 1);
        QCOMPARE(run.transport.position().section, 1);
    }

    void stoppingKeepsTheSectionAndStopsTheTrack()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{186000});
        run.transport.stop();
        const auto stopped = run.step();
        QVERIFY(stopped.stopTrack);
        QCOMPARE(stopped.gate.before, 1);
        QVERIFY(!run.transport.position().playing);
        QCOMPARE(run.transport.position().section, 1);
    }

    void aStopAndAJumpAskedTogetherBothHappen()
    {
        // As on a song change: stop, and back to the first section.
        Run run = twoSections();
        run.transport.play(1, false);
        run.step();
        run.transport.stop();
        run.transport.jump(0);
        const auto block = run.step();
        QVERIFY(block.stopTrack);
        QVERIFY(!run.transport.position().playing);
        QCOMPARE(run.transport.position().section, 0);
        QCOMPARE(block.gate.before, 0);
    }

    void afterTheLastSectionTheCountStops()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        run.step();
        QVERIFY(run.transport.position().playing);
        for (int i = 0; i < 2000 && run.transport.position().playing; ++i) run.step();
        QVERIFY(!run.transport.position().playing);
        // Stopped within the block that reached the end: 16 quarter notes = 384000 samples.
        QVERIFY2(run.sample >= 384000 && run.sample <= 384000 + kBlock, qPrintable(QString::number(run.sample)));
        const auto after = run.step();
        QCOMPARE(after.gate.before, 1); // the last sound stays
        QVERIFY(!after.stopTrack);      // the track plays on (an outro)
    }

    void theBarCountFollows()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        run.step();
        QCOMPARE(run.transport.position().bar, 1);
        while (run.sample < 100000) run.step(); // into the second bar
        QCOMPARE(run.transport.position().bar, 2);
        QCOMPARE(run.transport.position().bars, 2);
    }

    void withoutSectionsThereIsNoGate()
    {
        SongTransport transport;
        transport.play(0, false);
        const auto block = transport.advance(nullptr, 3.0, kBlock, kQuartersPerSample);
        QCOMPARE(block.gate.before, -1);
        QCOMPARE(block.gate.after, -1);
        QCOMPARE(block.ppq, 3.0);
        QCOMPARE(transport.position().section, -1);
        const SongTimeline empty;
        QCOMPARE(transport.advance(&empty, 3.0, kBlock, kQuartersPerSample).gate.before, -1);
    }

    // ---- The flow: parts, a section each time it comes round.

    void aTimelineOfPartsPlaysASectionTwice()
    {
        // Verse (2 bars), Chorus (2 bars), Chorus again.
        const SongTimeline t = SongTimeline::fromParts({0, 1, 1, 7}, {2, 2}, 4.0, 0.25); // (section 7: none, left out)
        QCOMPARE(t.count(), 3);
        QCOMPARE(t.starts, (std::vector<double>{0.0, 8.0, 16.0, 24.0}));
        QCOMPARE(t.sections, (std::vector<int>{0, 1, 1}));
        Run run(t);
        run.transport.play(0, false);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{186000}); // into the chorus
        while (run.ppq < 17.0) run.step();
        const SongPosition at = run.transport.position();
        QCOMPARE(at.part, 2);
        QCOMPARE(at.section, 1); // the chorus again, without a switch
        QCOMPARE(at.bar, 1);
    }

    // Next part: queued mid-bar, it lands on the next bar line, to the sample,
    // the count and the track moving to the next part.
    void nextPartLandsOnTheNextBarLine()
    {
        Run run(SongTimeline::fromBars({4, 2}, 4.0, 0.25)); // a verse of 4 bars
        run.transport.play(0, false);
        while (run.ppq < 1.0) run.step(); // in the first bar
        run.transport.queueNext();
        run.step();
        QCOMPARE(run.transport.position().queuedPart, 1);
        // The next bar line: 4 quarter notes = 96000 samples.
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{96000});
        QCOMPARE(run.last.gate.after, 1);
        // The count moves to the chorus's bar 1 in the block starting on (or
        // just after) that bar line.
        int64_t blockStart = run.sample - kBlock;
        SongTransport::Block landed = run.last;
        if (!landed.seekTrack) {
            blockStart = run.sample;
            landed = run.step();
        }
        QVERIFY(landed.seekTrack);
        QVERIFY(std::abs(landed.ppq - (16.0 + (blockStart - 96000) * kQuartersPerSample)) < 1e-6); // bar 1 of the chorus
        QCOMPARE(landed.trackQuarter, landed.ppq);
        QCOMPARE(run.transport.position().part, 1);
        QCOMPARE(run.transport.position().queuedPart, -1);
    }

    // Asked again before the bar line: not.
    void aQueuedNextAskedAgainIsCancelled()
    {
        Run run(SongTimeline::fromBars({4, 2}, 4.0, 0.25));
        run.transport.play(0, false);
        run.step();
        run.transport.queueNext();
        run.step();
        run.transport.queueNext();
        run.step();
        QCOMPARE(run.transport.position().queuedPart, -1);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{378000}); // the verse's own end (16 quarters less a sixteenth)
    }

    // Repeat: the part plays once more; then the song goes on.
    void aRepeatedPartPlaysOnceMore()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        run.step();
        run.transport.queueRepeat();
        while (run.transport.position().part == 0 && run.ppq < 7.9) run.step();
        QCOMPARE(run.transport.position().repeats, 1);
        while (run.ppq >= 1.0 || run.sample < 200000) run.step(); // past its end: back to its start
        QCOMPARE(run.transport.position().part, 0);
        QCOMPARE(run.transport.position().repeats, 0);
        QVERIFY(run.last.gate.before == 0 && run.last.gate.after == 0);
        // Then on to the chorus: two bars later.
        while (run.transport.position().part == 0) run.step();
        QVERIFY2(run.sample >= 384000 && run.sample <= 384000 + 2 * kBlock, qPrintable(QString::number(run.sample)));
    }

    // Hold: the part loops until released, then the song goes on at its end.
    void aHeldPartLoopsUntilReleased()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        run.transport.toggleHold();
        for (int i = 0; i < 2000; ++i) run.step(); // 512000 samples: past its end twice
        QCOMPARE(run.transport.position().part, 0);
        QVERIFY(run.transport.position().hold);
        run.transport.toggleHold();
        while (run.transport.position().part == 0) run.step();
        QCOMPARE(run.transport.position().part, 1);
        QVERIFY(!run.transport.position().hold);
    }

    // Stop at the end of the part: the count and the track stop there.
    void stopAtTheEndOfThePart()
    {
        Run run = twoSections();
        run.transport.play(0, false);
        run.transport.toggleStopAtEnd();
        run.step();
        QVERIFY(run.transport.position().stopAtEnd);
        bool trackStopped = false;
        while (run.transport.position().playing && run.sample < 400000) trackStopped = run.step().stopTrack || trackStopped;
        QVERIFY(trackStopped);
        QVERIFY2(run.sample >= 192000 && run.sample <= 192000 + 2 * kBlock, qPrintable(QString::number(run.sample)));
        QCOMPARE(run.transport.position().part, 0);
    }

    // Go to a part (a tile tapped): on the next bar line; at a part's end
    // its sound switches early, as the song's own changes do.
    void aPartChosenIsPlayedFromTheNextBarLine()
    {
        Run run(SongTimeline::fromBars({1, 1, 1}, 4.0, 0.25));
        run.transport.play(0, false);
        run.step();
        run.transport.queuePart(2);
        QCOMPARE(run.sampleOfNextSwitch(), int64_t{90000}); // the bar line is the verse's end: a sixteenth before
        QCOMPARE(run.last.gate.after, 2);
        while (run.transport.position().part == 0) run.step();
        QCOMPARE(run.transport.position().part, 2);
        QVERIFY(run.last.ppq >= 8.0 && run.last.ppq < 8.1);
    }

    void advanceDoesNotAllocate()
    {
        Run run = twoSections();
        run.transport.play(0, true);
        g_allocations = 0;
        t_countAllocations = true;
        for (int i = 0; i < 3000; ++i) run.step(); // count-in, both sections, the end
        run.transport.jump(0);
        run.step();
        run.transport.play(0, false);
        run.transport.queueRepeat();
        run.transport.toggleHold();
        run.transport.queueNext();
        run.transport.toggleStopAtEnd();
        for (int i = 0; i < 1000; ++i) run.step();
        t_countAllocations = false;
        QCOMPARE(g_allocations.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSongTransport)
#include "tst_song_transport.moc"
