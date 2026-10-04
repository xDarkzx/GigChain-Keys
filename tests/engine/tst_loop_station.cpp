// The loop pedal on the audio thread: where recordings start and end, what
// plays back, layers, undo, stopping, clearing, running out of room, free mode.
#include "LoopStation.h"

#include <QtTest>

#include <atomic>
#include <cstdlib>
#include <new>

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
void operator delete(void* p) noexcept { std::free(p); }              // NOLINT(cppcoreguidelines-no-malloc)
void operator delete(void* p, std::size_t) noexcept { std::free(p); } // NOLINT(cppcoreguidelines-no-malloc)

using namespace gigchain::engine;

namespace {

constexpr int kBlock = 64;
constexpr LoopGrid kBars{.origin = 0, .unit = 1000.0}; // a "bar" of 1000 samples

// What the channel plays at sample t: every sample different, exact in float.
float live(int64_t t)
{
    return static_cast<float>((t % 997) + 1) / 1024.0F;
}

// The edges of a take, where its fades and the sound that rang on past
// its end are (the tests of exact playback look past them; the seam has
// tests of its own).
bool atSeam(int64_t q, int64_t take)
{
    return q < 512 || q >= take - LoopStation::kDeclickFrames;
}

bool near(float a, float b)
{
    return std::abs(a - b) < 1e-5F;
}

// Blocks through a station as the audio thread runs them, feeding slot 0
// (and slot 1 when asked) with live(t) and keeping what the loops played.
struct Rig
{
    LoopStation station;
    int64_t t = 0;
    std::vector<float> played; // loop output, sample by sample from t = 0
    bool feedSecond = false;

    void block(LoopGrid grid = kBars)
    {
        std::array<float, kBlock> in{};
        for (int i = 0; i < kBlock; ++i) in.at(static_cast<std::size_t>(i)) = live(t + i);
        std::array<float, kBlock> left{};
        std::array<float, kBlock> right{};
        station.beginBlock(t, kBlock, grid);
        station.record(0, in.data(), in.data(), kBlock);
        if (feedSecond) station.record(1, in.data(), in.data(), kBlock);
        station.play(left.data(), right.data(), kBlock);
        station.endBlock();
        played.insert(played.end(), left.begin(), left.end());
        t += kBlock;
    }
    void runTo(int64_t sample, LoopGrid grid = kBars)
    {
        while (t < sample) block(grid);
    }
    // Room for a recording of `frames` in `slot`.
    void give(int slot, int64_t frames)
    {
        auto data = std::make_shared<LoopData>();
        data->base = std::make_shared<LoopTake>(frames);
        station.setData(slot, std::move(data));
    }
    // What the main thread does once the loop closed (and what rang on
    // past its end is in): the base trimmed to the take and that spill,
    // and its layers, as long as the loop.
    void giveLayers(int slot)
    {
        QVERIFY(station.takeNeedsLayers(slot));
        giveLayersNow(slot);
    }
    void giveLayersNow(int slot)
    {
        const LoopData* now = station.data(slot);
        const LoopReading loop = station.read(slot);
        const int64_t kept = loop.take + loop.tail;
        auto data = std::make_shared<LoopData>();
        data->base = std::make_shared<LoopTake>(kept);
        std::copy_n(now->base->left.begin(), kept, data->base->left.begin());
        std::copy_n(now->base->right.begin(), kept, data->base->right.begin());
        for (int i = 0; i < LoopStation::kMaxLayers; ++i) data->layers.push_back(std::make_shared<LoopTake>(loop.length));
        station.setData(slot, std::move(data));
    }
    // Runs until the loop's spill is in and gives its layers.
    void settleLayers(int slot)
    {
        for (int i = 0; i < 64; ++i) {
            if (station.takeNeedsLayers(slot)) {
                giveLayersNow(slot);
                return;
            }
            block();
        }
        QFAIL("the loop never asked for its layers");
    }
    // Records a synced loop in slot 0 from bar 1 (sample 1000) to bar 3 (3000).
    void recordTwoBars()
    {
        give(0, 100000);
        runTo(128);
        station.post(0, LoopCommand::Record); // on the next bar: 1000
        runTo(2944);
        station.post(0, LoopCommand::Record); // closes on the nearest bar: 3000
        runTo(3008);
    }
    // What the main thread does after an undo: fresh buffers for the undone layers.
    void freshenLayers(int slot)
    {
        const uint32_t dirty = station.dirtyLayers(slot);
        QVERIFY(dirty != 0);
        const LoopData* now = station.data(slot);
        auto data = std::make_shared<LoopData>(*now);
        for (std::size_t i = 0; i < data->layers.size(); ++i) {
            if ((dirty & (1U << i)) != 0) data->layers.at(i) = std::make_shared<LoopTake>(now->layers.at(i)->frames());
        }
        data->freshMask = dirty;
        station.setData(slot, std::move(data));
    }
};

} // namespace

class TestLoopStation : public QObject
{
    Q_OBJECT

private slots:
    void gridLines()
    {
        const LoopGrid grid{.origin = 100, .unit = 250.5};
        QCOMPARE(grid.next(100), int64_t{100});
        QCOMPARE(grid.next(101), int64_t{351}); // 350.5 rounds to 351
        QCOMPARE(grid.next(-400), int64_t{-151}); // lines at -401, -151 (-150.5), 100...
        QVERIFY(!LoopGrid{}.valid());
    }

    void aSyncedLoopStartsAndEndsOnTheBars()
    {
        Rig rig;
        rig.recordTwoBars();
        const LoopReading loop = rig.station.read(0);
        QCOMPARE(loop.state, LoopState::Playing);
        QCOMPARE(loop.length, int64_t{2000}); // bar 1 to bar 3, not press to press
        // It holds what was played from sample 1000 on, exactly.
        const LoopData* data = rig.station.data(0);
        for (int64_t i = 0; i < 2000; ++i) QCOMPARE(data->base->left.at(static_cast<std::size_t>(i)), live(1000 + i));
        // Nothing played while recording; the loop from sample 3000, pass after pass.
        rig.runTo(3000 + (10 * 2000));
        for (int64_t t = 0; t < 3000; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), 0.0F);
        for (int64_t t = 3000; t < 23000; ++t) {
            if (atSeam((t - 3000) % 2000, 2000)) continue;
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + ((t - 3000) % 2000)));
        }
    }

    void pressingRecordAgainBeforeTheBarCancels()
    {
        Rig rig;
        rig.give(0, 10000);
        rig.runTo(128); // past the first bar line
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(192);
        QCOMPARE(rig.station.read(0).state, LoopState::Armed);
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(2000);
        QCOMPARE(rig.station.read(0).state, LoopState::Empty);
    }

    void aLayerGoesOnTopAndUndoTakesItOff()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.settleLayers(0);
        rig.station.post(0, LoopCommand::Record); // a layer from the next bar: 4000 (loop position 1000)
        rig.runTo(3968); // (the next block holds the bar)
        QCOMPARE(rig.station.read(0).state, LoopState::OverdubArmed);
        rig.runTo(4064);
        QCOMPARE(rig.station.read(0).state, LoopState::Overdubbing);
        rig.runTo(5504);
        rig.station.post(0, LoopCommand::Record); // ends on the next bar: 6000 (one whole pass)
        rig.runTo(6064);
        QCOMPARE(rig.station.read(0).state, LoopState::Playing);
        QCOMPARE(rig.station.read(0).layers, 1);
        // Loop position p now plays the base plus what was played there during the layer.
        rig.runTo(8000);
        for (int64_t t = 6000; t < 8000; ++t) {
            const int64_t p = (t - 3000) % 2000;
            if (atSeam(p, 2000)) continue;
            const int64_t layerTime = 4000 + ((p - 1000 + 2000) % 2000); // when position p was layered
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + p) + live(layerTime));
        }
        rig.station.post(0, LoopCommand::Undo);
        rig.runTo(10000);
        QCOMPARE(rig.station.read(0).layers, 0);
        for (int64_t t = 8064; t < 10000; ++t) {
            if (atSeam((t - 3000) % 2000, 2000)) continue;
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + ((t - 3000) % 2000)));
        }
    }

    void aLayerWaitsForItsBuffers()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.station.post(0, LoopCommand::Record); // no layers yet
        rig.runTo(5000);
        QCOMPARE(rig.station.read(0).state, LoopState::OverdubArmed);
        rig.giveLayers(0);
        rig.runTo(5064);
        QCOMPARE(rig.station.read(0).state, LoopState::OverdubArmed); // until the next bar
        rig.runTo(6064);
        QCOMPARE(rig.station.read(0).state, LoopState::Overdubbing);
    }

    // Loop stops at once (no waiting for the bar); it starts on the bar.
    void stopAtOnceStartOnTheBar()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.runTo(3520);
        rig.station.post(0, LoopCommand::PlayStop);
        rig.block(); // 3520-3584
        QCOMPARE(rig.station.read(0).state, LoopState::Stopped);
        QCOMPARE(rig.played.at(3519), live(1000 + 519)); // playing up to the press...
        for (int64_t t = 3520; t < 3584; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), 0.0F); // ... silent from it
        rig.station.post(0, LoopCommand::PlayStop); // starts from the top on the next bar: 4000
        rig.runTo(3968);
        const LoopReading waiting = rig.station.read(0);
        QCOMPARE(waiting.state, LoopState::StartArmed);
        QCOMPARE(waiting.wait, int64_t{4000 - 3968}); // how long until it starts
        rig.runTo(5000);
        for (int64_t t = 3584; t < 4000; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), 0.0F);
        for (int64_t t = 4000; t < 5000; ++t) {
            if (atSeam(t - 4000, 2000)) continue;
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + (t - 4000)));
        }
        // Pressed again before the bar: it does not start after all.
        rig.station.post(0, LoopCommand::PlayStop); // stops at once
        rig.block();
        rig.station.post(0, LoopCommand::PlayStop); // start on the next bar...
        rig.block();
        rig.station.post(0, LoopCommand::PlayStop); // ... no: stay stopped
        rig.runTo(7000);
        QCOMPARE(rig.station.read(0).state, LoopState::Stopped);
    }

    // Ending a loop a little late closes it at the bar just gone, not the
    // next one: a 2-bar riff stays 2 bars, with no gap, and keeps in time.
    void aLoopClosesOnTheNearestBar()
    {
        Rig rig;
        rig.give(0, 100000);
        rig.runTo(128);
        rig.station.post(0, LoopCommand::Record); // from 1000
        rig.runTo(1536);
        QCOMPARE(rig.station.read(0).state, LoopState::Recording);
        QCOMPARE(rig.station.read(0).position, int64_t{536}); // recorded so far
        rig.runTo(3072);
        rig.station.post(0, LoopCommand::Record); // 72 samples after bar 3 (3000): closes there
        rig.block();
        const LoopReading loop = rig.station.read(0);
        QCOMPARE(loop.state, LoopState::Playing);
        QCOMPARE(loop.length, int64_t{2000});
        // In time, as if it had closed on the bar: position 72 at 3072.
        rig.runTo(8000);
        for (int64_t t = 3072; t < 8000; ++t) {
            if (atSeam((t - 3000) % 2000, 2000)) continue;
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + ((t - 3000) % 2000)));
        }
        // A little early: it waits for the bar that is nearest.
        Rig early;
        early.give(0, 100000);
        early.runTo(128);
        early.station.post(0, LoopCommand::Record);
        early.runTo(2944);
        early.station.post(0, LoopCommand::Record); // 56 before bar 3: closes on it
        early.runTo(3072);
        QCOMPARE(early.station.read(0).length, int64_t{2000});
        // Barely started: it cannot close on the bar it started on, so the next one.
        Rig barely;
        barely.give(0, 100000);
        barely.runTo(128);
        barely.station.post(0, LoopCommand::Record);
        barely.runTo(1088);
        barely.station.post(0, LoopCommand::Record);
        barely.runTo(2064);
        QCOMPARE(barely.station.read(0).length, int64_t{1000});
    }

    // A set length: the loop closes by itself at its end; stopped early, it
    // keeps the part that fills that length evenly (no gap).
    void aSetLengthClosesByItselfOrFillsIn()
    {
        Rig full;
        full.station.setTargetLines(4); // 4 bars
        full.give(0, 100000);
        full.runTo(128);
        full.station.post(0, LoopCommand::Record); // from 1000
        full.runTo(4992);
        QCOMPARE(full.station.read(0).state, LoopState::Recording);
        QCOMPARE(full.station.read(0).wait, int64_t{5000 - 4992}); // it knows where it ends
        full.runTo(5064);
        QCOMPARE(full.station.read(0).state, LoopState::Playing); // no second press
        QCOMPARE(full.station.read(0).length, int64_t{4000});
        full.runTo(9000);
        for (int64_t t = 5000; t < 9000; ++t) {
            if (atSeam(t - 5000, 4000)) continue;
            QCOMPARE(full.played.at(static_cast<std::size_t>(t)), live(1000 + (t - 5000)));
        }

        // Stopped after two bars (a hair late): a take of two bars, playing
        // twice in a loop of the four (so a layer on it can be four bars).
        Rig two;
        two.station.setTargetLines(4);
        two.give(0, 100000);
        two.runTo(128);
        two.station.post(0, LoopCommand::Record);
        two.runTo(3072);
        two.station.post(0, LoopCommand::Record);
        two.block();
        QCOMPARE(two.station.read(0).length, int64_t{4000});
        QCOMPARE(two.station.read(0).take, int64_t{2000});
        two.runTo(9000);
        for (int64_t t = 3072; t < 9000; ++t) {
            if (atSeam((t - 3000) % 2000, 2000)) continue;
            QCOMPARE(two.played.at(static_cast<std::size_t>(t)), live(1000 + ((t - 3000) % 2000)));
        }

        // Stopped in bar 3 of 4: three bars cannot fill four, so a take of two.
        Rig three;
        three.station.setTargetLines(4);
        three.give(0, 100000);
        three.runTo(128);
        three.station.post(0, LoopCommand::Record);
        three.runTo(3456); // 2.46 bars in: nearest is 2
        three.station.post(0, LoopCommand::Record);
        three.block();
        QCOMPARE(three.station.read(0).take, int64_t{2000});
        QCOMPARE(three.station.read(0).length, int64_t{4000});
        Rig threeLate;
        threeLate.station.setTargetLines(4);
        threeLate.give(0, 100000);
        threeLate.runTo(128);
        threeLate.station.post(0, LoopCommand::Record);
        threeLate.runTo(4032); // 3.03 bars in: nearest is 3, which cannot fill 4: 2
        threeLate.station.post(0, LoopCommand::Record);
        threeLate.block();
        QCOMPARE(threeLate.station.read(0).take, int64_t{2000});
        QCOMPARE(threeLate.station.read(0).length, int64_t{4000});
        // Stopped just before bar 2 ends: it closes there (a bar, four times).
        Rig one;
        one.station.setTargetLines(4);
        one.give(0, 100000);
        one.runTo(128);
        one.station.post(0, LoopCommand::Record);
        one.runTo(1920);
        one.station.post(0, LoopCommand::Record);
        one.runTo(2064);
        QCOMPARE(one.station.read(0).take, int64_t{1000});
        QCOMPARE(one.station.read(0).length, int64_t{4000});
    }

    // One bar recorded with the length set to four: a loop of four bars,
    // the bar playing four times in it, and a layer on it four bars long
    // (a line over the riff, not the riff's one bar over and over).
    void aShortTakeFillsTheSetLength()
    {
        Rig rig;
        rig.station.setTargetLines(4);
        rig.give(0, 100000);
        rig.runTo(128);
        rig.station.post(0, LoopCommand::Record); // from 1000
        rig.runTo(1920);
        rig.station.post(0, LoopCommand::Record); // closes at 2000: a take of one bar
        rig.runTo(2064);
        const LoopReading loop = rig.station.read(0);
        QCOMPARE(loop.take, int64_t{1000});
        QCOMPARE(loop.length, int64_t{4000});
        QCOMPARE(loop.position, rig.t - 1000); // the second time through the bar (bar 1 was the recording)
        rig.settleLayers(0);
        QCOMPARE(rig.station.data(0)->layers.front()->frames(), int64_t{4000});
        // A layer over the whole loop: from the next bar (6000, loop position
        // 1000) for one pass of the four bars, to 10000.
        rig.runTo(5504);
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(6064);
        QCOMPARE(rig.station.read(0).state, LoopState::Overdubbing);
        rig.runTo(9504);
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(10064);
        QCOMPARE(rig.station.read(0).layers, 1);
        rig.runTo(14000);
        // Each of the four bars plays the take plus what was played over that bar.
        for (int64_t t = 10000; t < 14000; ++t) {
            const int64_t p = (t - 1000) % 4000; // loop position (the recording was its first bar)
            const int64_t q = p % 1000;          // in the take
            if (atSeam(q, 1000)) continue;
            const int64_t layered = 6000 + ((p - 1000 + 4000) % 4000); // when position p was layered
            QVERIFY2(near(rig.played.at(static_cast<std::size_t>(t)), live(1000 + q) + live(layered)),
                     qPrintable(QString::number(t)));
        }
    }

    // Stopped a little late: what was played past the bar is not thrown
    // away, it rings on over the start of the loop (fading out), from the
    // second time round (the first time it is heard live).
    void aLateStopWrapsWhatRangOnIntoTheStart()
    {
        Rig rig;
        rig.give(0, 100000);
        rig.runTo(128);
        rig.station.post(0, LoopCommand::Record); // from 1000
        rig.runTo(3072);
        rig.station.post(0, LoopCommand::Record); // 72 late: closes on bar 3 (3000)
        rig.block();
        const LoopReading loop = rig.station.read(0);
        QCOMPARE(loop.take, int64_t{2000});
        QCOMPARE(loop.length, int64_t{2000});
        // The layers wait until all of the spill is in.
        QVERIFY(!rig.station.takeNeedsLayers(0));
        rig.settleLayers(0);
        const LoopReading settled = rig.station.read(0);
        const int64_t tail = settled.tail;
        const int64_t fade = LoopStation::tailFadeFrames(kBars, 2000);
        QCOMPARE(tail, int64_t{72} + fade);
        rig.runTo(7000);
        const auto expected = [&](int64_t q, bool second) {
            const float in = static_cast<float>(std::min<int64_t>(q, LoopStation::kDeclickFrames)) /
                             static_cast<float>(LoopStation::kDeclickFrames);
            float v = live(1000 + q) * in;
            if (second && q < tail) {
                const float out = std::min(1.0F, static_cast<float>(tail - q) / static_cast<float>(fade));
                v += live(3000 + q) * out;
            }
            return v;
        };
        // The first time round (from the press): the take alone.
        for (int64_t t = 3072; t < 3400; ++t) {
            QVERIFY2(near(rig.played.at(static_cast<std::size_t>(t)), expected(t - 3000, false)), qPrintable(QString::number(t)));
        }
        // The second time: the spill over the start, fading out.
        for (int64_t t = 5000; t < 5400; ++t) {
            QVERIFY2(near(rig.played.at(static_cast<std::size_t>(t)), expected(t - 5000, true)), qPrintable(QString::number(t)));
        }
        QVERIFY(near(rig.played.at(5000 + 30), live(1030) * 30.0F / LoopStation::kDeclickFrames + live(3030)));
    }

    // Closed on the bar: a held note still rings past it, and that bit
    // rings on over the start next time round instead of being cut dead.
    void aNoteRingingPastTheEndCarriesOn()
    {
        Rig rig;
        rig.recordTwoBars(); // closes on 3000
        rig.settleLayers(0);
        const int64_t fade = LoopStation::tailFadeFrames(kBars, 2000);
        QCOMPARE(rig.station.read(0).tail, fade);
        rig.runTo(5200);
        const int64_t q = 40;
        const float in = static_cast<float>(q) / static_cast<float>(LoopStation::kDeclickFrames);
        const float out = std::min(1.0F, static_cast<float>(fade - q) / static_cast<float>(fade));
        QVERIFY(near(rig.played.at(5000 + q), (live(1000 + q) * in) + (live(3000 + q) * out)));
    }

    // The loop starts from silence (a sound already ringing when the
    // recording began does not click in at every pass).
    void theStartOfALoopIsFadedIn()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.runTo(3200);
        QCOMPARE(rig.played.at(3000), 0.0F);
        QVERIFY(rig.played.at(3000 + (LoopStation::kDeclickFrames / 2)) < live(1000 + (LoopStation::kDeclickFrames / 2)));
        QCOMPARE(rig.played.at(3000 + LoopStation::kDeclickFrames), live(1000 + LoopStation::kDeclickFrames));
    }

    // Stopped, then started again: the first time round has no spill over
    // its start (it comes after silence, not after its own end).
    void aRestartedLoopStartsWithoutTheSpill()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.settleLayers(0);
        rig.runTo(3520);
        rig.station.post(0, LoopCommand::PlayStop); // stops at once
        rig.block();
        rig.station.post(0, LoopCommand::PlayStop); // starts on the bar: 4000
        rig.runTo(6200);
        const int64_t q = 300;
        QCOMPARE(rig.played.at(4000 + q), live(1000 + q)); // first time: the take alone
        const int64_t s = 20;
        QVERIFY(rig.played.at(6000 + s) > live(1000 + s) * static_cast<float>(s) / LoopStation::kDeclickFrames); // second: with it
    }

    // Buffers that do not fit the loop (never given by the engine, but if
    // they were): the loop is silent and says so; nothing is read past them.
    void buffersTooSmallForTheLoopAreRefused()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.settleLayers(0);
        auto small = std::make_shared<LoopData>();
        small->base = std::make_shared<LoopTake>(500); // the take is 2000
        rig.station.setData(0, small);
        rig.runTo(6000);
        for (int64_t t = 3300; t < 6000; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), 0.0F);
        QVERIFY(rig.station.takeFault(0));
        QVERIFY(!rig.station.takeFault(0)); // said once
    }

    // An undone layer leaves nothing behind for the next one.
    void aLayerAfterAnUndoStartsClean()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.settleLayers(0);
        rig.station.post(0, LoopCommand::Record); // a layer over the whole loop, 4000-6000
        rig.runTo(5504);
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(6064);
        QCOMPARE(rig.station.read(0).layers, 1);
        rig.station.post(0, LoopCommand::Undo);
        rig.block();
        QCOMPARE(rig.station.read(0).layers, 0);
        // Another layer, asked before the fresh buffer came: it waits for
        // it, then starts on the next bar (8000: loop position 1000) and
        // covers one bar only (to 9000: positions 1000-1999).
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(7500);
        QCOMPARE(rig.station.read(0).state, LoopState::OverdubArmed);
        rig.freshenLayers(0);
        rig.runTo(8064);
        QCOMPARE(rig.station.read(0).state, LoopState::Overdubbing);
        rig.station.post(0, LoopCommand::Record); // ends at 9000
        rig.runTo(9064);
        QCOMPARE(rig.station.read(0).layers, 1);
        // Positions 0-999 (not in the new layer): the base alone, nothing of the undone one.
        rig.runTo(12000);
        for (int64_t t = 11000; t < 12000; ++t) {
            const int64_t p = (t - 3000) % 2000;
            QVERIFY(p < 1000);
            if (atSeam(p, 2000)) continue;
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + p));
        }
    }

    void stopAndClearAreAtOnce()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.runTo(3500);
        rig.station.post(0, LoopCommand::Stop);
        rig.runTo(3564);
        QCOMPARE(rig.station.read(0).state, LoopState::Stopped);
        for (int64_t t = 3520; t < 3564; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), 0.0F);
        rig.station.post(0, LoopCommand::Clear);
        rig.runTo(3628);
        QCOMPARE(rig.station.read(0).state, LoopState::Empty);
        QCOMPARE(rig.station.read(0).length, int64_t{0});
        // A stop while recording drops the recording.
        rig.give(0, 10000);
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(4500);
        QCOMPARE(rig.station.read(0).state, LoopState::Recording);
        rig.station.post(0, LoopCommand::Stop);
        rig.runTo(4564);
        QCOMPARE(rig.station.read(0).state, LoopState::Empty);
    }

    void aLoopThatFillsItsRoomClosesAndSaysSo()
    {
        Rig rig;
        rig.give(0, 1500);
        rig.runTo(128);
        rig.station.post(0, LoopCommand::Record); // from the bar at 1000
        rig.runTo(4000);
        QCOMPARE(rig.station.read(0).state, LoopState::Playing);
        QCOMPARE(rig.station.read(0).length, int64_t{1500}); // 1000 to 2500
        QVERIFY(rig.station.takeFull(0));
        QVERIFY(!rig.station.takeFull(0)); // said once
        QCOMPARE(rig.played.at(2500 + 600), live(1600));
        QCOMPARE(rig.station.read(0).tail, int64_t{0}); // no room left for a spill
    }

    void freeLoopsSetTheGridForTheOthers()
    {
        Rig rig;
        rig.feedSecond = true;
        rig.station.setSync(false);
        rig.give(0, 10000);
        rig.give(1, 10000);
        rig.station.post(0, LoopCommand::Record); // at once: sample 0
        rig.runTo(640);
        rig.station.post(0, LoopCommand::Record); // closes at once: 640
        rig.runTo(704);
        QCOMPARE(rig.station.read(0).length, int64_t{640});
        const LoopGrid grid = rig.station.freeGrid();
        QCOMPARE(grid.origin, int64_t{0});
        QCOMPARE(grid.unit, 160.0); // quarters of the first loop
        // The second starts and ends on those quarters.
        rig.station.post(1, LoopCommand::Record); // next quarter: 800
        rig.runTo(1408);
        rig.station.post(1, LoopCommand::Record); // closes on the nearest quarter: 1440
        rig.runTo(1500);
        QCOMPARE(rig.station.read(1).length, int64_t{640});
        QCOMPARE(rig.station.data(1)->base->left.at(0), live(800));
        // All cleared: the next free loop sets a new grid.
        rig.station.post(0, LoopCommand::Clear);
        rig.station.post(1, LoopCommand::Clear);
        rig.runTo(1600);
        QVERIFY(!rig.station.freeGrid().valid());
    }

    void layersRunOutAtEight()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.settleLayers(0);
        for (int i = 0; i < LoopStation::kMaxLayers; ++i) {
            rig.station.post(0, LoopCommand::Record);
            rig.runTo(rig.t + 1000);
            rig.station.post(0, LoopCommand::Record);
            rig.runTo(rig.t + 3000);
        }
        QCOMPARE(rig.station.read(0).layers, LoopStation::kMaxLayers);
        rig.station.post(0, LoopCommand::Record);
        rig.block();
        QVERIFY(rig.station.takeNoLayerLeft(0));
        QCOMPARE(rig.station.read(0).state, LoopState::Playing);
    }

    // A press stays "waiting" until the audio thread has said what it did
    // with it: between taking it (the block's start) and saying so (its
    // end), the main thread must not see an empty loop with nothing waiting
    // (it would free the loop, and the press would be lost).
    void aPressWaitsUntilItsOutcomeIsSaid()
    {
        Rig rig;
        rig.give(0, 10000);
        rig.runTo(128);
        rig.station.post(0, LoopCommand::Record);
        QVERIFY(rig.station.pending(0));
        std::array<float, kBlock> in{};
        std::array<float, kBlock> left{};
        std::array<float, kBlock> right{};
        rig.station.beginBlock(rig.t, kBlock, kBars); // the press is taken...
        QVERIFY(rig.station.read(0).state == LoopState::Empty); // ... not yet said
        QVERIFY(rig.station.pending(0));
        rig.station.record(0, in.data(), in.data(), kBlock);
        rig.station.play(left.data(), right.data(), kBlock);
        rig.station.endBlock(); // ... said
        QVERIFY(!rig.station.pending(0));
        QCOMPARE(rig.station.read(0).state, LoopState::Armed);
    }

    void theAudioThreadNeverAllocates()
    {
        Rig rig;
        rig.played.reserve(200000);
        rig.give(0, 100000);
        g_allocations = 0;
        t_countAllocations = true;
        rig.station.post(0, LoopCommand::Record);
        rig.runTo(2304);
        rig.station.post(0, LoopCommand::Record); // (at once: the bar at 2000 is nearest)
        rig.runTo(5000);
        rig.station.post(0, LoopCommand::PlayStop);
        rig.runTo(7000);
        rig.station.post(0, LoopCommand::Clear);
        rig.runTo(8000);
        t_countAllocations = false;
        QCOMPARE(g_allocations.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestLoopStation)
#include "tst_loop_station.moc"
