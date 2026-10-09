#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/engine/IEngine.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <set>

using namespace gigchain;
using namespace Qt::StringLiterals;

namespace {

struct ManualClock
{
    double now = 0.3;
};

core::Patch twoChannelPatch()
{
    core::Patch patch = core::makePatch(u"Verse"_s);
    patch.channels.push_back(core::makeChannel(u"Piano"_s));
    patch.channels.push_back(core::makeChannel(u"Strings"_s));
    return patch;
}

} // namespace

class TestFakeEngine : public QObject
{
    Q_OBJECT

private slots:
    // A knob turned (as if from the keyboard) is the one MIDI Learn takes,
    // and once learned for the mixer it moves that control; out-of-range
    // values are ignored.
    void aTurnedKnobIsLearnedAndMovesItsControl()
    {
        const auto engine = engine::createFakeEngine();
        QVERIFY(!engine->takeMovedController());
        engine->injectController(1, 21, 90);
        const auto moved = engine->takeMovedController();
        QVERIFY(moved.has_value());
        QCOMPARE(*moved, (std::pair{1, 21}));
        QVERIFY(!engine->takeMovedController()); // taken once

        engine::AppKnobs knobs{};
        knobs.at(1) = engine::MidiTrigger{.kind = engine::MidiTrigger::ControlChange, .channel = 0, .number = 21}; // strip 1's volume
        engine->setAppKnobs(knobs);
        engine->injectController(1, 21, 64);
        engine->injectController(2, 21, 100); // another channel: not this knob
        engine::AppKnobValues values = engine->takeAppKnobValues();
        QCOMPARE(values.at(1), 64);
        QCOMPARE(values.at(0), -1);
        QCOMPARE(engine->takeAppKnobValues().at(1), -1); // taken once

        engine->injectController(17, 21, 10);
        engine->injectController(1, 21, 128);
        QCOMPARE(engine->takeAppKnobValues().at(1), -1);
    }

    void offersInstrumentsAndEffectsWithUniqueIds()
    {
        const auto engine = engine::createFakeEngine();
        const auto plugins = engine->availablePlugins();
        std::set<QString> ids;
        int instruments = 0;
        int effects = 0;
        for (const auto& plugin : plugins) {
            QVERIFY(ids.insert(plugin.id).second);
            QVERIFY(!plugin.name.isEmpty());
            (plugin.kind == engine::PluginKind::Instrument ? instruments : effects) += 1;
        }
        QVERIFY(instruments > 0);
        QVERIFY(effects > 0);
        QVERIFY(ids.count(u"fake.grand-piano"_s) == 1);
        QVERIFY(ids.count(u"fake.reverb"_s) == 1);
    }

    void levelsStayInRange()
    {
        ManualClock clock;
        const auto engine = engine::createFakeEngine([&clock] { return clock.now; });
        const core::Patch patch = twoChannelPatch();
        engine->applyPatch(patch);
        for (int step = 0; step < 200; ++step) {
            clock.now = step * 0.05;
            for (const auto& channel : patch.channels) {
                auto level = engine->channelLevel(channel.id);
                QVERIFY(level.peak >= 0.0F && level.peak <= 1.0F);
                QVERIFY(level.rms >= 0.0F && level.rms <= level.peak);
            }
        }
    }

    void unknownChannelIsSilent()
    {
        const auto engine = engine::createFakeEngine();
        engine->applyPatch(twoChannelPatch());
        QCOMPARE(engine->channelLevel(core::ChannelId::generate()).peak, 0.0F);
    }

    void muteAndSoloSilenceChannels()
    {
        ManualClock clock;
        const auto engine = engine::createFakeEngine([&clock] { return clock.now; });
        const core::Patch patch = twoChannelPatch();
        const auto& piano = patch.channels[0].id;
        const auto& strings = patch.channels[1].id;
        engine->applyPatch(patch);
        QVERIFY(engine->channelLevel(piano).peak > 0.0F);

        engine->setChannelMute(piano, true);
        QCOMPARE(engine->channelLevel(piano).peak, 0.0F);
        QVERIFY(engine->channelLevel(strings).peak > 0.0F);
        engine->setChannelMute(piano, false);

        engine->setChannelSolo(strings, true);
        QCOMPARE(engine->channelLevel(piano).peak, 0.0F);
        QVERIFY(engine->channelLevel(strings).peak > 0.0F);
    }

    void volumeFloorSilencesAndBadValuesAreIgnored()
    {
        const auto engine = engine::createFakeEngine();
        const core::Patch patch = twoChannelPatch();
        engine->applyPatch(patch);
        engine->setChannelVolume(patch.channels[0].id, -96.0);
        QCOMPARE(engine->channelLevel(patch.channels[0].id).peak, 0.0F);

        engine->setMasterVolume(50.0);
        QCOMPARE(engine->masterVolume(), 12.0);
        engine->setMasterVolume(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(engine->masterVolume(), 12.0);
    }

    void applyPatchReplacesChannels()
    {
        const auto engine = engine::createFakeEngine();
        const core::Patch first = twoChannelPatch();
        engine->applyPatch(first);
        engine->applyPatch(core::makePatch(u"Empty"_s));
        QCOMPARE(engine->channelLevel(first.channels[0].id).peak, 0.0F);
    }

    void cpuAndMidiAreSimulated()
    {
        ManualClock clock;
        const auto engine = engine::createFakeEngine([&clock] { return clock.now; });
        QVERIFY(engine->cpuLoad() > 0.0F && engine->cpuLoad() < 1.0F);
        clock.now = 0.1;
        QVERIFY(engine->midiActivity());
        clock.now = 1.0;
        QVERIFY(!engine->midiActivity());
    }

    // A playing loop goes round at the tempo, timed by the engine's clock:
    // at 120 bpm a 4-bar loop is 8 seconds, so 3 seconds in it is in bar 2.
    void aPlayingLoopGoesRoundByTheClock()
    {
        ManualClock clock;
        const auto engine = engine::createFakeEngine([&clock] { return clock.now; });
        engine->setTempo(120.0);
        const core::ChannelId piano = core::makeChannel(u"Piano"_s).id;
        engine->loopCommand(piano, engine::LoopCommand::Record);
        engine->loopCommand(piano, engine::LoopCommand::Record); // closes it: playing
        QCOMPARE(engine->loops().at(0).bar, 1);
        clock.now += 3.0;
        QCOMPARE(engine->loops().at(0).bar, 2);
        QVERIFY(std::abs(engine->loops().at(0).progress - 3.0 / 8.0) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestFakeEngine)
#include "tst_fake_engine.moc"
