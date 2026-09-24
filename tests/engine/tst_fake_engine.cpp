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
};

QTEST_GUILESS_MAIN(TestFakeEngine)
#include "tst_fake_engine.moc"
