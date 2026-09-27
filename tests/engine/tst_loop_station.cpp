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
    // What the main thread does once the loop closed: the base trimmed to
    // the loop, and its layers.
    void giveLayers(int slot)
    {
        QVERIFY(station.takeNeedsLayers(slot));
        const LoopData* now = station.data(slot);
        const int64_t length = station.read(slot).length;
        auto data = std::make_shared<LoopData>();
        data->base = std::make_shared<LoopTake>(length);
        std::copy_n(now->base->left.begin(), length, data->base->left.begin());
        std::copy_n(now->base->right.begin(), length, data->base->right.begin());
        for (int i = 0; i < LoopStation::kMaxLayers; ++i) data->layers.push_back(std::make_shared<LoopTake>(length));
        station.setData(slot, std::move(data));
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
        rig.giveLayers(0);
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
            const int64_t layerTime = 4000 + ((p - 1000 + 2000) % 2000); // when position p was layered
            QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + p) + live(layerTime));
        }
        rig.station.post(0, LoopCommand::Undo);
        rig.runTo(10000);
        QCOMPARE(rig.station.read(0).layers, 0);
        for (int64_t t = 8064; t < 10000; ++t) {
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
        for (int64_t t = 4000; t < 5000; ++t) QCOMPARE(rig.played.at(static_cast<std::size_t>(t)), live(1000 + (t - 4000)));
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

    // An undone layer leaves nothing behind for the next one.
    void aLayerAfterAnUndoStartsClean()
    {
        Rig rig;
        rig.recordTwoBars();
        rig.giveLayers(0);
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
        QCOMPARE(rig.played.at(2500), live(1000));
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
        rig.giveLayers(0);
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
