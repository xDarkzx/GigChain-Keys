#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/engine/IEngine.h"
#include "gigchain/engine/SidechainCurves.h"

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
        // The app's own instruments too, as the real engine offers them.
        QVERIFY(ids.count(u"builtin:saw-stack-pro"_s) == 1);
        QVERIFY(ids.count(u"builtin:piano-pro"_s) == 1);
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

    // The demo engine learns as the real one does: the last pad, key or
    // button pressed (Learn, Pads…, Set by playing), and a pad assigned to
    // the panel is a press of its button, never a note on the keyboard.
    void padsAreLearnedAndPressThePanel()
    {
        const auto engine = engine::createFakeEngine();
        engine->injectNote(10, 36, 100);
        QCOMPARE(engine->takeLearnedTrigger(),
                 (engine::MidiTrigger{.kind = engine::MidiTrigger::Note, .channel = 9, .number = 36}));
        QVERIFY(!engine->takeLearnedTrigger().isSet()); // taken once
        engine->injectNote(10, 36, 0);
        QVERIFY(!engine->takeLearnedTrigger().isSet()); // letting go is no press
        engine->injectController(1, 20, 127);
        QCOMPARE(engine->takeLearnedTrigger(),
                 (engine::MidiTrigger{.kind = engine::MidiTrigger::ControlChange, .channel = 0, .number = 20}));

        engine::PanelTriggers panel;
        panel.sounds.at(1) = engine::MidiTrigger{.kind = engine::MidiTrigger::Note, .channel = 9, .number = 37};
        engine->setPanelControls(panel);
        engine->injectNote(10, 37, 100);
        QCOMPARE(engine->takePanelPresses(), (std::vector<engine::PanelPress>{{.kind = engine::PanelPress::Sound, .index = 1}}));
        QCOMPARE(engine->keyboardActivity().velocity.at(37), uint8_t{0}); // never played
        engine->injectNote(10, 37, 0);
        QVERIFY(engine->takePanelPresses().empty());
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

    // The demo's own instruments behave as the real ones: a type chosen
    // brings its own settings (Felt Piano: its tape on, its sidechain off), a
    // setting stays as set; a reading for the window (not a setting) is
    // shown as the demo sets it.
    void theAppsOwnInstrumentsKeepTheirSettingsAsTheRealOnes()
    {
        const auto engine = engine::createFakeEngine();
        core::Patch patch = core::makePatch(u"Drop"_s);
        core::Channel channel = core::makeChannel(u"Piano Pro"_s);
        channel.instrument = core::PluginSlot{.pluginId = u"builtin:piano-pro"_s, .displayName = u"Piano Pro"_s, .bypass = false, .state = {}, .shareId = {}};
        patch.channels.push_back(channel);
        engine->applyPatch(patch);
        const auto parameters = engine->pluginParameters(channel.id, -1);
        const auto idOf = [&](const QString& name) {
            const auto found = std::ranges::find_if(parameters, [&](const engine::PluginParameter& p) { return p.name == name; });
            return found != parameters.end() ? found->id : std::numeric_limits<uint32_t>::max();
        };
        const engine::PluginParameter& type = parameters.front();
        QCOMPARE(type.name, u"Type"_s);
        QCOMPARE(engine->pluginParameterValue(channel.id, -1, idOf(u"Tape"_s)), 0.0); // House Piano: no tape
        const auto felt = static_cast<double>(type.choices.indexOf(u"Felt Piano"_s)) / static_cast<double>(type.choices.size() - 1);
        QVERIFY(engine->setPluginParameter(channel.id, -1, type.id, felt).has_value());
        QVERIFY(engine->pluginParameterValue(channel.id, -1, idOf(u"Tape"_s)) > 0.3);
        QCOMPARE(engine->pluginParameterValue(channel.id, -1, idOf(u"Sidechain On"_s)), 0.0);
        QVERIFY(engine->setPluginParameter(channel.id, -1, idOf(u"Tone"_s), 0.25).has_value());
        QVERIFY(std::abs(engine->pluginParameterValue(channel.id, -1, idOf(u"Tone"_s)) - 0.25) < 1e-6);
        // A reading the window shows (where the sidechain is), as the demo sets it.
        QVERIFY(engine->setPluginParameter(channel.id, -1, engine::sidechain::kPositionParameter, 0.4).has_value());
        QCOMPARE(engine->pluginParameterValue(channel.id, -1, engine::sidechain::kPositionParameter), 0.4);
        engine->applyPatch(patch); // the same channel again: its settings kept
        QVERIFY(std::abs(engine->pluginParameterValue(channel.id, -1, idOf(u"Tone"_s)) - 0.25) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestFakeEngine)
#include "tst_fake_engine.moc"
