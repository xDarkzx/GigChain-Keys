// Notes timed to the beat clock (Practice's chords): each played on its own
// sample, whichever block it falls in, in order, and forgotten when asked.
#include "ScheduledNotes.h"

#include <QtTest>

#include <array>
#include <vector>

using namespace gigchain::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kQuartersPerSample = 120.0 / 60.0 / kRate; // 120 BPM

MidiEvent note(int pitch, int velocity)
{
    return MidiEvent{.status = static_cast<uint8_t>(velocity > 0 ? 0x90 : 0x80), .data1 = static_cast<uint8_t>(pitch),
                     .data2 = static_cast<uint8_t>(velocity)};
}

struct Heard
{
    int64_t sample = 0; // from the clock's start
    MidiEvent event;
};

// The clock run block by block from quarter `from` for `blocks` blocks: every note taken, at its sample.
std::vector<Heard> run(ScheduledNotes& notes, int blocks, double from = 0.0)
{
    std::vector<Heard> heard;
    std::array<MidiEvent, 64> out{};
    for (int b = 0; b < blocks; ++b) {
        const double quarter = from + (static_cast<double>(b) * kBlock * kQuartersPerSample);
        const std::size_t taken = notes.take(quarter, kQuartersPerSample, kBlock, out);
        for (std::size_t i = 0; i < taken; ++i) heard.push_back({.sample = (static_cast<int64_t>(b) * kBlock) + out.at(i).sampleOffset, .event = out.at(i)});
    }
    return heard;
}

} // namespace

class TestScheduledNotes : public QObject
{
    Q_OBJECT

private slots:
    // A note on quarter 1 (at 120 BPM, half a second: sample 24000) is
    // played on that very sample, not at the start of its block.
    void aNotePlaysOnItsSample()
    {
        ScheduledNotes notes;
        QVERIFY(notes.push(note(60, 90), 1.0));
        QVERIFY(notes.push(note(60, 0), 2.0));
        const std::vector<Heard> heard = run(notes, 400);
        QCOMPARE(heard.size(), std::size_t{2});
        QCOMPARE(heard.at(0).sample, int64_t{24000}); // (block 93 starts at 23808: 192 into it)
        QCOMPARE(heard.at(0).event.data2, uint8_t{90});
        QCOMPARE(heard.at(1).sample, int64_t{48000});
        QCOMPARE(heard.at(1).event.data2, uint8_t{0});
    }

    // In one block: in sample order; a note let go and struck again on the
    // same sample, let go first (so it sounds again).
    void notesInABlockAreInOrder()
    {
        ScheduledNotes notes;
        const double inBlock = 10.0 * kQuartersPerSample;
        QVERIFY(notes.push(note(64, 90), 3 * inBlock));
        QVERIFY(notes.push(note(60, 0), inBlock));
        QVERIFY(notes.push(note(60, 90), inBlock));
        std::array<MidiEvent, 8> out{};
        QCOMPARE(notes.take(0.0, kQuartersPerSample, kBlock, out), std::size_t{3});
        QCOMPARE(out.at(0).sampleOffset, 10);
        QCOMPARE(out.at(0).data2, uint8_t{0}); // let go first
        QCOMPARE(out.at(1).sampleOffset, 10);
        QCOMPARE(out.at(1).data2, uint8_t{90});
        QCOMPARE(out.at(2).sampleOffset, 30);
    }

    // A note whose time has gone by (late): played at once.
    void aLateNotePlaysAtOnce()
    {
        ScheduledNotes notes;
        QVERIFY(notes.push(note(60, 90), 0.5));
        std::array<MidiEvent, 8> out{};
        QCOMPARE(notes.take(4.0, kQuartersPerSample, kBlock, out), std::size_t{1});
        QCOMPARE(out.at(0).sampleOffset, 0);
    }

    // Cleared: the notes waiting are forgotten; those asked after it are kept.
    void aClearForgetsTheNotesWaiting()
    {
        ScheduledNotes notes;
        QVERIFY(notes.push(note(60, 90), 1.0));
        QVERIFY(notes.clear());
        QVERIFY(notes.push(note(62, 90), 1.0));
        const std::vector<Heard> heard = run(notes, 400);
        QCOMPARE(heard.size(), std::size_t{1});
        QCOMPARE(heard.at(0).event.data1, uint8_t{62});
    }

    // The clock jumped back (a song started from its top) far behind the
    // notes waiting: their note-ons are dropped, their note-offs played now
    // (nothing left hanging).
    void aClockThatJumpedBackLeavesNoNoteHanging()
    {
        ScheduledNotes notes;
        QVERIFY(notes.push(note(60, 90), 200.0));
        QVERIFY(notes.push(note(60, 0), 204.0));
        std::array<MidiEvent, 8> out{};
        QCOMPARE(notes.take(0.0, kQuartersPerSample, kBlock, out), std::size_t{1});
        QCOMPARE(out.at(0).data2, uint8_t{0});
        QCOMPARE(notes.take(200.0, kQuartersPerSample, kBlock, out), std::size_t{0}); // (the note-on is gone)
    }

    // Room for a block's events only: the rest wait for the next block.
    void aFullBlockLeavesTheRestForTheNext()
    {
        ScheduledNotes notes;
        for (int i = 0; i < 5; ++i) QVERIFY(notes.push(note(60 + i, 90), 0.0));
        std::array<MidiEvent, 3> out{};
        QCOMPARE(notes.take(0.0, kQuartersPerSample, kBlock, out), std::size_t{3});
        QCOMPARE(notes.take(0.0, kQuartersPerSample, kBlock, out), std::size_t{2});
    }
};

QTEST_GUILESS_MAIN(TestScheduledNotes)
#include "tst_scheduled_notes.moc"
