// Integration tests of the real engine on this machine's audio device and
// installed plugins. Master volume is set to silence first, so nothing is
// heard; the channel meters are measured before the master fader.
#include "PluginCatalog.h"
#include "PluginLoadGuard.h"
#include "gigchain/core/Limits.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kVst3Folder = u"C:/Program Files/Common Files/VST3"_s;
const QString kPiano = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;

core::Patch pianoPatch()
{
    core::Patch patch = core::makePatch(u"Verse"_s);
    core::Channel channel = core::makeChannel(u"Piano"_s);
    channel.instrument = core::PluginSlot{kPiano, u"Piano V2"_s, false};
    patch.channels.push_back(channel);
    return patch;
}

void pump(IEngine& engine, int milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        (void)engine.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace

class TestRealEngine : public QObject
{
    Q_OBJECT

private slots:
    void catalogFindsInstalledPlugins()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        const auto entries = PluginCatalog::scan(kVst3Folder);
        const auto piano = std::find_if(entries.begin(), entries.end(), [](const PluginInfo& p) { return p.id == kPiano; });
        QVERIFY(piano != entries.end());
        QCOMPARE(piano->name, u"Piano V2"_s);
        QVERIFY(piano->kind == PluginKind::Instrument);
        QVERIFY(!piano->vendor.isEmpty());
        QVERIFY(piano->website.contains(u"arturia"_s, Qt::CaseInsensitive)); // for the info panel
        QVERIFY(piano->sdkVersion.startsWith(u"VST"_s));
    }

    void catalogOfMissingFolderIsEmpty()
    {
        QVERIFY(PluginCatalog::scan(u"C:/no/such/folder"_s).empty());
    }

    void playsAnInstrumentFromAnInjectedNote()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY2(created.has_value(), created ? "" : qPrintable(created.error().message));
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb); // silent test
        QVERIFY(!engine.statusText().isEmpty());
        // Parts separated by a middle dot (U+00B7), not its garbled UTF-8 bytes "Â·".
        QVERIFY2(engine.statusText().contains(u" · "_s) && !engine.statusText().contains(u'Â'),
                 qPrintable(engine.statusText()));

        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty()); // plugin loaded without problems

        engine.setMasterVolume(-90.0); // still inaudible, but measurable
        (void)engine.masterLevel();
        engine.injectNote(1, 60, 110);
        pump(engine, 400);
        const float peak = engine.channelLevel(patch.channels[0].id).peak;
        const LevelReading master = engine.masterLevel();
        QCOMPARE(int(engine.keyboardActivity().velocity.at(60)), 110); // lit on the screen's keyboard
        engine.injectNote(1, 60, 0);
        pump(engine, 50);
        QCOMPARE(int(engine.keyboardActivity().velocity.at(60)), 0);
        QVERIFY2(peak > 0.001F, "piano channel stayed silent");
        // The master meter shows what leaves the app: after the master fader.
        QVERIFY2(master.peak > 0.0F, "master meter stayed empty");
        QVERIFY(master.peak < peak);
        QVERIFY(engine.cpuLoad() > 0.0F && engine.cpuLoad() < 1.0F);
    }

    void changingTheSampleRateKeepsPlaying()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb); // silent test
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());

        const AudioSetup before = engine.audioSetup();
        QVERIFY(before.driver == AudioDriver::System);
        QVERIFY(!before.device.isEmpty()); // the device actually open, by name
        const auto outputs = engine.audioOutputs();
        const auto current = std::find_if(outputs.begin(), outputs.end(), [&](const AudioOutput& o) {
            return o.driver == before.driver && o.name == before.device;
        });
        QVERIFY(current != outputs.end());
        for (const unsigned int rate : current->sampleRates) {
            QVERIFY2(rate >= 44100 && rate <= 96000, "only live-safe rates are offered"); // 192 kHz crashed Piano V2
        }
        // The lowest other rate: 44.1 kHz when running at 48.
        const auto other = std::ranges::find_if(current->sampleRates,
                                                [&](unsigned int rate) { return rate != before.sampleRate; });
        if (other == current->sampleRates.end()) QSKIP("The system output offers only one sample rate");

        AudioSetup wanted = before;
        wanted.sampleRate = *other;
        wanted.bufferFrames = 512;
        const auto changed = engine.setAudioSetup(wanted);
        QVERIFY2(changed.has_value(), changed ? "" : qPrintable(changed.error().message));
        QCOMPARE(engine.audioSetup().sampleRate, *other);

        // The same plugin, re-prepared for the new rate, still plays.
        engine.injectNote(1, 64, 110);
        pump(engine, 400);
        const float peak = engine.channelLevel(patch.channels[0].id).peak;
        engine.injectNote(1, 64, 0);
        pump(engine, 50);
        QVERIFY2(peak > 0.001F, "piano went silent after the rate change");
    }

    void unusableAudioSetupKeepsTheCurrentOne()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        const AudioSetup before = engine.audioSetup();
        AudioSetup wanted = before;
        wanted.device = u"No Such Device"_s;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No audio output named \"No Such Device\""_s));
        const auto changed = engine.setAudioSetup(wanted);
        QVERIFY(!changed);
        QVERIFY(changed.error().message.contains(u"No Such Device"_s));
        QCOMPARE(engine.audioSetup().device, before.device); // still playing on the old one
    }

    void midiInputsCanBeSwitchedOff()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        const auto inputs = engine.midiInputs();
        if (inputs.size() < 2) QSKIP("Needs two MIDI inputs (e.g. an Impact GXP61 plugged in)");
        QVERIFY(inputs[0].enabled);  // by default only the first port
        QVERIFY(!inputs[1].enabled);
        MidiSetup second;
        second.configured = true;
        second.enabled = {inputs[1].name};
        QVERIFY(engine.setMidiSetup(second).has_value());
        QVERIFY(!engine.midiInputs()[0].enabled);
        QVERIFY(engine.midiInputs()[1].enabled);
    }

    void aSongsPatchesShareTheirPluginsAndPreloadPrunes()
    {
        // A small plugin keeps this quick; any plugin behaves the same.
        const QString kSmall = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
        if (!QFileInfo::exists(kSmall)) QSKIP("TDR Kotelnikov not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        const auto channelWith = [&](const QString& name) {
            core::Channel channel = core::makeChannel(name);
            channel.instrument = core::PluginSlot{kSmall, u"Kotelnikov"_s, false};
            return channel;
        };
        core::Setlist setlist;
        core::Song ballad = core::makeSong(u"Ballad"_s);
        ballad.patches[0].channels = {channelWith(u"Piano"_s)};                           // Intro
        ballad.patches.push_back(core::makePatch(u"Chorus"_s));
        ballad.patches[1].channels = {channelWith(u"Piano"_s), channelWith(u"Layer"_s)};  // same piano + a layer
        core::Song funk = core::makeSong(u"Funk"_s);
        funk.patches[0].channels = {channelWith(u"Keys"_s)};
        setlist.songs = {ballad, funk};

        std::vector<std::pair<int, int>> steps;
        engine.setProgressHandler([&](LoadStage stage, const QString&, int done, int total) {
            if (stage == LoadStage::LoadingSounds) steps.emplace_back(done, total);
        });
        engine.preload(setlist);
        // Ballad: one shared piano (Intro and Chorus) + the Chorus layer; Funk: its own.
        QCOMPARE(engine.loadedPluginCount(), std::size_t{3});
        QVERIFY(!steps.empty());
        QCOMPARE(steps.back(), std::make_pair(3, 3)); // the overlay knows when it is done

        // Switching patches loads nothing: everything was loaded up front.
        engine.applyPatch(ballad.id, ballad.patches[1]);
        engine.applyPatch(ballad.id, ballad.patches[0]);
        engine.applyPatch(funk.id, funk.patches[0]);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{3});

        // Another setlist: plugins it does not use are unloaded (memory freed).
        core::Setlist onlyFunk;
        onlyFunk.songs = {funk};
        engine.preload(onlyFunk);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
        QVERIFY(engine.poll().empty());
    }

    void pluginSettingsAreStoredAndComeBack()
    {
        const QString kSmall = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
        if (!QFileInfo::exists(kSmall)) QSKIP("TDR Kotelnikov not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.instrument = core::PluginSlot{kSmall, u"Kotelnikov"_s, false};
        core::Song song = core::makeSong(u"Ballad"_s);
        song.patches[0].channels = {channel};
        song.patches.push_back(core::makePatch(u"Chorus"_s));
        song.patches[1].channels = {channel}; // the same shared instance
        core::Setlist setlist;
        setlist.songs = {song};
        engine.preload(setlist);
        QVERIFY(engine.poll().empty());
        QVERIFY(!engine.takePluginEdits()); // loading is not an edit

        QVERIFY(engine.storePluginStates(setlist).empty());
        const QByteArray stored = setlist.songs[0].patches[0].channels[0].instrument->state;
        QVERIFY(!stored.isEmpty());
        QCOMPARE(setlist.songs[0].patches[1].channels[0].instrument->state, stored); // one instance, one state

        // Opening a setlist whose settings differ reloads the plugin with them;
        // settings it cannot take are reported, not ignored.
        core::Setlist broken = setlist;
        broken.songs[0].patches[0].channels[0].instrument->state = "GCS1 garbage";
        broken.songs[0].patches[1].channels[0].instrument->state = "GCS1 garbage";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Kotelnikov.*saved settings"_s));
        engine.preload(broken);
        const auto notices = engine.poll();
        QCOMPARE(notices.size(), std::size_t{1});
        QVERIFY2(notices.front().text.contains(u"Kotelnikov"_s), qPrintable(notices.front().text));
        QVERIFY(notices.front().level == Notice::Level::Warning); // it still plays
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1}); // still playing, at its defaults

        engine.preload(setlist); // good settings: reloaded with them, quietly
        QVERIFY(engine.poll().empty());
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
    }

    void anEffectsWindowOpensWhileItIsOn()
    {
        const QString kSmall = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
        if (!QFileInfo::exists(kSmall)) QSKIP("TDR Kotelnikov not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Patch patch = core::makePatch(u"Verse"_s);
        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.effects = {core::PluginSlot{kSmall, u"Kotelnikov"_s, false}, core::PluginSlot{kSmall, u"Kotelnikov"_s, true}};
        patch.channels.push_back(channel);
        engine.applyPatch(patch);

        auto editor = engine.createEffectEditor(channel.id, 0);
        QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error().message));
        QVERIFY(*editor != nullptr);
        QVERIFY(!(*editor)->preferredSize().isEmpty());

        const auto off = engine.createEffectEditor(channel.id, 1); // switched off: not loaded
        QVERIFY(!off);
        QVERIFY2(off.error().message.contains(u"switched off"_s), qPrintable(off.error().message));
        QVERIFY(!engine.createEffectEditor(channel.id, 2));                     // no such effect
        QVERIFY(!engine.createEffectEditor(core::ChannelId(u"gone"_s), 0)); // no such channel
    }

    void masterEffectsStayWhateverSetlistIsOpen()
    {
        const QString kSmall = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
        if (!QFileInfo::exists(kSmall)) QSKIP("TDR Kotelnikov not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        std::vector<core::PluginSlot> master{core::PluginSlot{kSmall, u"Kotelnikov"_s, false},
                                             core::PluginSlot{kSmall, u"Kotelnikov"_s, true}};
        engine.setMasterEffects(master);
        QVERIFY(engine.poll().empty());
        QVERIFY(!engine.takeMasterEdits()); // loading is not an edit
        auto editor = engine.createMasterEffectEditor(0);
        QVERIFY2(editor.has_value() && *editor, editor ? "no editor" : qPrintable(editor.error().message));
        editor->reset();
        QVERIFY(!engine.createMasterEffectEditor(1)); // switched off: not loaded
        QVERIFY(!engine.createMasterEffectEditor(2));

        // Opening another setlist does not touch the master bus.
        engine.preload(core::Setlist{});
        QVERIFY(engine.createMasterEffectEditor(0).has_value());

        QVERIFY(engine.storeMasterEffectStates(master).empty());
        QVERIFY(!master[0].state.isEmpty());
        QVERIFY(master[1].state.isEmpty()); // not loaded: kept as it was

        engine.setOutputLimiter(true, -3.0);
        (void)engine.takeLimiterActivity();
        engine.setMasterEffects({});
        QVERIFY(!engine.createMasterEffectEditor(0));
    }

    void aPluginThatCrashedTheAppIsNotLoadedAgain()
    {
        const QString kSmall = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
        if (!QFileInfo::exists(kSmall)) QSKIP("TDR Kotelnikov not installed");
        QTemporaryDir guardFolder;
        RealEngineOptions options;
        options.pluginGuardFolder = guardFolder.path();
        // The last run "died" while loading it: its marker is still there.
        PluginLoadGuard lastRun(guardFolder.path());
        const auto loading = lastRun.loading(kSmall);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"crashed the app while loading last time"_s));
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);
        QCOMPARE(engine.blockedPlugins(), QStringList{kSmall});
        auto notices = engine.poll();
        QVERIFY(!notices.empty());
        QVERIFY2(notices.back().text.contains(u"TDR Kotelnikov"_s), qPrintable(notices.back().text));
        QVERIFY(notices.back().level == Notice::Level::Warning);

        core::Patch patch = core::makePatch(u"Verse"_s);
        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.instrument = core::PluginSlot{kSmall, u"Kotelnikov"_s, false};
        patch.channels.push_back(channel);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Kotelnikov is switched off"_s));
        engine.applyPatch(patch);
        notices = engine.poll();
        QCOMPARE(notices.size(), std::size_t{1}); // said, not silently missing
        QVERIFY(notices.front().text.contains(u"switched off"_s));
        QVERIFY(notices.front().level == Notice::Level::Warning);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{0});

        engine.unblockPlugin(kSmall); // "Try again"
        QVERIFY(engine.blockedPlugins().isEmpty());
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
    }

    void aLearnedPadSwitchesSongsAndIsNotPlayed()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());

        // "Learn": the next press is remembered.
        (void)engine.takeLearnedTrigger();
        engine.injectNote(1, 36, 100);
        engine.injectNote(1, 36, 0);
        pump(engine, 100);
        const MidiTrigger learned = engine.takeLearnedTrigger();
        QCOMPARE(learned, (MidiTrigger{MidiTrigger::Note, 0, 36}));

        ControlTriggers triggers{};
        triggers[static_cast<std::size_t>(ControlAction::NextSong)] = learned;
        engine.setControlTriggers(triggers);
        // The learned hit played (learning takes nothing away): silence its tail.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Panic: every sound stopped"_s));
        engine.panic();
        pump(engine, 200);
        (void)engine.channelLevel(patch.channels[0].id);
        engine.injectNote(1, 36, 100); // the pad
        pump(engine, 300);
        const auto actions = engine.takeControlActions();
        QCOMPARE(actions, std::vector<ControlAction>{ControlAction::NextSong});
        QVERIFY(engine.takeControlActions().empty()); // once
        QCOMPARE(engine.channelLevel(patch.channels[0].id).peak, 0.0F); // the piano never heard it
        engine.injectNote(1, 36, 0);

        engine.injectNote(1, 60, 110); // any other key still plays
        pump(engine, 300);
        QVERIFY(engine.channelLevel(patch.channels[0].id).peak > 0.001F);
        engine.injectNote(1, 60, 0);
    }

    void panicStopsTheSoundAndPlaysOnAfter()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        engine.injectNote(1, 60, 110); // held: never released by the "player"
        pump(engine, 300);
        QVERIFY(engine.channelLevel(patch.channels[0].id).peak > 0.001F);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Panic: every sound stopped"_s));
        engine.panic();
        pump(engine, 300);
        (void)engine.channelLevel(patch.channels[0].id);
        pump(engine, 200);
        QVERIFY2(engine.channelLevel(patch.channels[0].id).peak < 0.0005F, "still sounding after panic");
        QVERIFY(engine.poll().empty());

        engine.injectNote(1, 64, 110); // and it plays again straight away
        pump(engine, 300);
        QVERIFY(engine.channelLevel(patch.channels[0].id).peak > 0.001F);
        engine.injectNote(1, 64, 0);
    }

    void unknownPluginIsReportedNotIgnored()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        core::Patch patch = core::makePatch(u"Broken"_s);
        core::Channel channel = core::makeChannel(u"Ghost"_s);
        channel.instrument = core::PluginSlot{u"C:/no/such/Ghost.vst3"_s, u"Ghost"_s, false};
        patch.channels.push_back(channel);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Plugin not found: C:/no/such/Ghost\\.vst3"_s));
        engine.applyPatch(patch);
        const auto notices = engine.poll();
        QCOMPARE(notices.size(), std::size_t{1});
        QVERIFY(notices.front().text.contains(u"Ghost"_s));
        QVERIFY(notices.front().level == Notice::Level::Error); // it does not play
    }

    void theTempoIsSetAndOutOfRangeIsRefused()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        QCOMPARE(engine.tempo(), 120.0);
        engine.setTempo(92.5);
        QCOMPARE(engine.tempo(), 92.5);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Tempo 900 ignored"_s));
        engine.setTempo(900.0);
        QCOMPARE(engine.tempo(), 92.5);
    }

    void theClickSoundsOnTheBeat()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s)); // nothing else makes sound
        pump(engine, 100);
        (void)engine.masterLevel();
        pump(engine, 600);
        QCOMPARE(engine.masterLevel().peak, 0.0F);

        engine.setTempo(240.0); // a beat every quarter second
        engine.setClick(true, -60.0); // measurable, not heard
        QVERIFY(engine.clickOn());
        pump(engine, 600);
        const float peak = engine.masterLevel().peak;
        QVERIFY2(peak > 0.0001F && peak < 0.002F, qPrintable(QString::number(peak))); // 0.5 at -60 dB
        engine.setClick(false, -60.0);
    }

    void aBackingTrackPlaysThroughTheMasterFader()
    {
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s));
        engine.setMasterVolume(-60.0);

        // Half a second of a 0.5 sine.
        const QTemporaryDir dir;
        const QString path = dir.filePath(u"backing.wav"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream out(&file);
            out.setByteOrder(QDataStream::LittleEndian);
            const quint32 frames = 24000;
            out.writeRawData("RIFF", 4);
            out << quint32{36 + (frames * 2)};
            out.writeRawData("WAVEfmt ", 8);
            out << quint32{16} << quint16{1} << quint16{1} << quint32{48000} << quint32{96000} << quint16{2} << quint16{16};
            out.writeRawData("data", 4);
            out << frames * 2;
            for (quint32 i = 0; i < frames; ++i) {
                out << static_cast<qint16>(std::lround(0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * 440.0 * i / 48000.0)));
            }
        }
        engine.setBackingTrack(path);
        for (int i = 0; i < 300 && !engine.backingTrack().loaded; ++i) pump(engine, 10);
        const BackingTrackState ready = engine.backingTrack();
        QVERIFY2(ready.loaded, "the backing track was not read");
        QVERIFY(std::abs(ready.length - 0.5) < 0.02);
        QVERIFY(!ready.playing);

        (void)engine.masterLevel();
        pump(engine, 200);
        QCOMPARE(engine.masterLevel().peak, 0.0F); // loaded, not playing: silent

        engine.playBackingTrack(true);
        pump(engine, 150);
        const float peak = engine.masterLevel().peak;
        QVERIFY2(std::abs(peak - 0.0005F) < 0.0001F, qPrintable(QString::number(peak))); // 0.5 at -60 dB
        QVERIFY(engine.backingTrack().position > 0.05);
        pump(engine, 600); // past its end: it stops by itself
        QVERIFY(!engine.backingTrack().playing);

        // A file that is not there is said, not ignored.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Backing track not found"_s)); // logged once
        engine.setBackingTrack(dir.filePath(u"missing.wav"_s));
        bool reported = false;
        for (int i = 0; i < 300 && !reported; ++i) {
            for (const Notice& notice : engine.poll()) reported = reported || notice.text.contains(u"not found"_s);
            if (!reported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        QVERIFY(reported);
    }

    void aHeldChordRingsOnAcrossAPatchChange()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0); // inaudible, measurable
        const core::SongId song = core::SongId::generate();
        const core::Patch verse = pianoPatch();
        engine.applyPatch(song, verse);
        QVERIFY(engine.poll().empty());
        engine.injectNote(1, 60, 110);
        engine.injectNote(1, 64, 110);
        pump(engine, 300);

        // The chorus has no piano: the held chord goes on until it is let go.
        engine.applyPatch(song, core::makePatch(u"Chorus"_s));
        pump(engine, 100);
        (void)engine.masterLevel();
        pump(engine, 300);
        QVERIFY2(engine.masterLevel().peak > 0.0F, "the held chord stopped at the patch change");

        engine.injectNote(1, 60, 0);
        engine.injectNote(1, 64, 0);
        pump(engine, 3000); // the piano's release, then a quiet second
        (void)engine.masterLevel();
        pump(engine, 300);
        QCOMPARE(engine.masterLevel().peak, 0.0F);
    }

    void aPluginsParametersAreListedForKnobs()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        const core::ChannelId& piano = patch.channels.front().id;
        const std::vector<PluginParameter> parameters = engine.pluginParameters(piano, -1);
        QVERIFY2(parameters.size() > 10, qPrintable(QString::number(parameters.size())));
        QVERIFY(std::ranges::all_of(parameters, [](const PluginParameter& p) { return !p.name.isEmpty(); }));
        QVERIFY(engine.pluginParameters(piano, 0).empty());                        // no effect there
        QVERIFY(engine.pluginParameters(core::ChannelId::generate(), -1).empty()); // no such channel
        QVERIFY(!engine.takeTouchedParameter(piano, -1).has_value());               // nothing moved
    }

    void bundledPluginsAreFoundWithTheInstalledOnes()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        // A "bundled" folder holding a copy of an installed plugin (same name
        // and maker): the installed one is kept, not listed twice.
        const QTemporaryDir bundled;
        QVERIFY(QFile::copy(kPiano, bundled.filePath(u"Piano V2.vst3"_s)));
        RealEngineOptions options;
        options.bundledPluginFolder = bundled.path();
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        const auto plugins = (*created)->availablePlugins();
        QCOMPARE(std::ranges::count_if(plugins, [](const PluginInfo& p) { return p.name == u"Piano V2"_s; }), 1);
        const auto piano = std::ranges::find_if(plugins, [](const PluginInfo& p) { return p.name == u"Piano V2"_s; });
        QCOMPARE(piano->id, kPiano);
    }
};

QTEST_GUILESS_MAIN(TestRealEngine)
#include "tst_real_engine.moc"
