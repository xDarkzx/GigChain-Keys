// Integration tests of the real engine on this machine's audio device and
// installed plugins. Master volume is set to silence first, so nothing is
// heard; the channel meters are measured before the master fader.
#include "PluginCatalog.h"
#include "gigchain/core/Limits.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QtTest>

#include <chrono>
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

        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty()); // plugin loaded without problems

        engine.injectNote(1, 60, 110);
        pump(engine, 400);
        const float peak = engine.channelLevel(patch.channels[0].id).peak;
        engine.injectNote(1, 60, 0);
        pump(engine, 50);
        QVERIFY2(peak > 0.001F, "piano channel stayed silent");
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
        unsigned int other = 0;
        for (const unsigned int rate : current->sampleRates) {
            if (rate != before.sampleRate) {
                other = rate; // the lowest other rate: 44.1 kHz when running at 48
                break;
            }
        }
        if (other == 0) QSKIP("The system output offers only one sample rate");

        AudioSetup wanted = before;
        wanted.sampleRate = other;
        wanted.bufferFrames = 512;
        const auto changed = engine.setAudioSetup(wanted);
        QVERIFY2(changed.has_value(), changed ? "" : qPrintable(changed.error().message));
        QCOMPARE(engine.audioSetup().sampleRate, other);

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

    void arturiaReloadsAtTheSizeThatFits()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        const QString prefs = u"C:/ProgramData/Arturia/Piano V2/tmp/plugin.pref.xml"_s;
        QFile original(prefs);
        if (!original.open(QIODevice::ReadOnly)) QSKIP("Piano V2 has no settings file yet");
        const QByteArray saved = original.readAll();
        original.close();
        // Put the user's own Arturia setting back whatever happens.
        const auto restore = qScopeGuard([&] {
            QFile back(prefs);
            if (back.open(QIODevice::WriteOnly | QIODevice::Truncate)) back.write(saved);
        });

        auto created = createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb); // silent test
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());

        // Piano V2 at 80 % is 1280x1006; a 1700x1300 area fits 100 % (1600x1258).
        const QSize at80(1280, 1006);
        const QSize area(1700, 1300);
        {
            QFile check(prefs);
            QVERIFY(check.open(QIODevice::ReadOnly));
            if (!check.readAll().contains(R"(name="GUI Size" value="0.300000")")) QSKIP("Piano V2 is not at 80 % here");
        }
        const auto reloaded = engine.fitEditorToArea(patch.channels[0].id, at80, area);
        QVERIFY2(reloaded.has_value(), reloaded ? "" : qPrintable(reloaded.error().message));
        QVERIFY(*reloaded);
        auto editor = engine.createEditor(patch.channels[0].id);
        QVERIFY(editor.has_value() && *editor != nullptr); // the new instance has an editor

        // Already the best fit: nothing more happens.
        const auto again = engine.fitEditorToArea(patch.channels[0].id, QSize(1600, 1258), area);
        QVERIFY(again.has_value());
        QVERIFY(!*again);

        // The reloaded piano still plays.
        engine.injectNote(1, 60, 110);
        pump(engine, 400);
        const float peak = engine.channelLevel(patch.channels[0].id).peak;
        engine.injectNote(1, 60, 0);
        pump(engine, 50);
        QVERIFY2(peak > 0.001F, "piano went silent after the reload");
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
        QVERIFY2(notices[0].contains(u"Kotelnikov"_s), qPrintable(notices[0]));
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1}); // still playing, at its defaults

        engine.preload(setlist); // good settings: reloaded with them, quietly
        QVERIFY(engine.poll().empty());
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
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
        QVERIFY(notices[0].contains(u"Ghost"_s));
    }
};

QTEST_GUILESS_MAIN(TestRealEngine)
#include "tst_real_engine.moc"
