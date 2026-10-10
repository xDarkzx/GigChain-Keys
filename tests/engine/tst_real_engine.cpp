// Integration tests of the real engine on this machine's audio device and
// installed plugins. Master volume is set to silence first, so nothing is
// heard; the channel meters are measured before the master fader.
#include "PluginCatalog.h"
#include "PluginLoadGuard.h"
#include "RealEngine.h"
#include "gigchain/core/Chords.h"
#include "gigchain/core/Editing.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/PluginSharing.h"
#include "gigchain/engine/RealEngineFactory.h"

#include "TestPlugins.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// This system's test plugins (TestPlugins.h): Piano V2 and Kotelnikov on
// Windows, Surge XT and Surge XT Effects on Linux.
const QString kVst3Folder = test::kVst3Folder;
const QString kPiano = test::kInstrument.path; // the test instrument
const QString kSmall = test::kEffect.path;     // a small effect

core::PluginSlot slot(const QString& pluginId, const QString& name, bool bypass = false)
{
    return core::PluginSlot{.pluginId = pluginId, .displayName = name, .bypass = bypass, .state = {}};
}

// The instrument of the first song's patch `patch`, first channel (the tests
// put one there).
core::PluginSlot& instrumentOf(core::Setlist& setlist, std::size_t patch)
{
    core::Channel& channel = setlist.songs.at(0).patches.at(patch).channels.at(0);
    if (!channel.instrument) throw std::logic_error("the test's channel has no instrument");
    return *channel.instrument; // written through by callers
}

core::Patch pianoPatch()
{
    core::Patch patch = core::makePatch(u"Verse"_s);
    core::Channel channel = core::makeChannel(u"Piano"_s);
    channel.instrument = slot(kPiano, u"Piano V2"_s);
    patch.channels.push_back(channel);
    return patch;
}

// The real engine without MIDI inputs: tests measuring sound and silence
// must not hear a keyboard someone happens to be playing (notes come in
// through injectNote).
core::Result<std::unique_ptr<IEngine>> createQuietEngine()
{
    RealEngineOptions options;
    options.midiInputs = false;
    return createRealEngine(options);
}

// A mono 48 kHz WAV of a sine at `amplitude`, `frames` long.
bool writeSineWav(const QString& path, quint32 frames, double amplitude = 0.5)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + (frames * 2)};
    out.writeRawData("WAVEfmt ", 8);
    out << quint32{16} << quint16{1} << quint16{1} << quint32{48000} << quint32{96000} << quint16{2} << quint16{16};
    out.writeRawData("data", 4);
    out << frames * 2;
    for (quint32 i = 0; i < frames; ++i) {
        out << static_cast<qint16>(std::lround(amplitude * 32767.0 * std::sin(2.0 * std::numbers::pi * 440.0 * i / 48000.0)));
    }
    return out.status() == QDataStream::Ok;
}

void pump(IEngine& engine, int milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        (void)engine.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Keeps the engine going until `done` says so, for at most `limitMs`: what
// the audio thread does is waited for, not assumed to be done in a few
// milliseconds (some sound systems, WSLg's PulseAudio among them, run the
// audio in bursts). True when it happened.
// (`done` is asked once each time round, never again after it said yes: it
// may take what it found.)
template <typename Done>
bool waitUntil(IEngine& engine, const Done& done, int limitMs = 2000)
{
    for (int waited = 0;; waited += 20) {
        if (done()) return true;
        if (waited >= limitMs) return false;
        pump(engine, 20);
    }
}

// The loudest of `read()` (a level meter's peak since its last reading)
// until it goes over `above`, for at most `limitMs`: a sound waited for,
// not assumed to be there after a fixed time.
template <typename Read>
float loudestUntilAbove(IEngine& engine, const Read& read, float above, int limitMs = 2000)
{
    float loudest = 0.0F;
    (void)waitUntil(engine, [&] {
        loudest = std::max(loudest, read());
        return loudest > above;
    }, limitMs);
    return loudest;
}

// Whether `read()` (a level meter's peak since its last reading) stays at or
// under `below` for a whole `windowMs`, within `limitMs`: silence waited for
// (a sound system running late stops later), a whole stretch of it (never
// one empty moment of a late sound system, or a quiet moment of a loop,
// taken for silence).
template <typename Read>
bool quietWithin(IEngine& engine, const Read& read, float below, int windowMs = 200, int limitMs = 3000)
{
    for (int waited = 0; waited < limitMs; waited += windowMs) {
        (void)read();
        pump(engine, windowMs);
        if (read() <= below) return true;
    }
    return false;
}

} // namespace

class TestRealEngine : public QObject
{
    Q_OBJECT

private slots:
    void catalogFindsInstalledPlugins()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        const auto entries = PluginCatalog::scan(kVst3Folder);
        const auto piano = std::find_if(entries.begin(), entries.end(), [](const PluginInfo& p) { return p.id == kPiano; });
        QVERIFY(piano != entries.end());
        QCOMPARE(piano->name, test::kInstrument.name);
        QVERIFY(piano->kind == PluginKind::Instrument);
        QVERIFY(!piano->vendor.isEmpty());
        QVERIFY2(piano->website.contains(test::kInstrument.website, Qt::CaseInsensitive), qPrintable(piano->website)); // for the info panel
        QVERIFY(piano->sdkVersion.startsWith(u"VST"_s));
    }

    // The output the MIDI clock goes to, pulled out and plugged in again: the
    // clock starts again by itself.
    void theMidiClockComesBackWhenItsOutputIsPluggedIn()
    {
        const QStringList outputs = MidiClockOut::listPorts();
        if (outputs.isEmpty()) QSKIP("No MIDI outputs on this machine");
        RealEngineOptions options;
        options.midiInputs = false;
        options.midi.clockOutput = outputs.first();
        auto created = RealEngine::create(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY2(created.has_value(), created ? "" : qPrintable(created.error().message));
        RealEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb); // silent test
        QCOMPARE(engine.m_clockOut.portName(), outputs.first());

        // Pulled out: sending failed and the clock stopped (as poll() does then),
        // and the last look found the output gone.
        engine.m_clockOut.close();
        engine.m_midiOutputs.clear();
        engine.m_lastMidiCheck = {}; // due for another look
        const std::vector<Notice> notices = engine.poll();
        QCOMPARE(engine.m_clockOut.portName(), outputs.first());
        QVERIFY(std::ranges::any_of(notices, [](const Notice& n) { return n.text.contains(u"MIDI clock"_s); }));
        engine.m_clockOut.close();
    }

    void catalogOfMissingFolderIsEmpty()
    {
        QVERIFY(PluginCatalog::scan(u"C:/no/such/folder"_s).empty());
    }

    void playsAnInstrumentFromAnInjectedNote()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
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
        float masterPeak = 0.0F;
        const float peak = loudestUntilAbove(engine, [&] {
            masterPeak = std::max(masterPeak, engine.masterLevel().peak);
            return engine.channelLevel(patch.channels.at(0).id).peak;
        }, 0.001F);
        QCOMPARE(int(engine.keyboardActivity().velocity.at(60)), 110); // lit on the screen's keyboard
        engine.injectNote(1, 60, 0);
        // Let go on the screen too (waited for: some sound systems run in bursts).
        for (int i = 0; i < 40 && engine.keyboardActivity().velocity.at(60) != 0; ++i) pump(engine, 50);
        QCOMPARE(int(engine.keyboardActivity().velocity.at(60)), 0);
        QVERIFY2(peak > 0.001F, "piano channel stayed silent");
        // The master meter shows what leaves the app: after the master fader.
        QVERIFY2(masterPeak > 0.0F, "master meter stayed empty");
        QVERIFY(masterPeak < peak);
        QVERIFY(engine.cpuLoad() > 0.0F && engine.cpuLoad() < 1.0F);
    }

    void changingTheSampleRateKeepsPlaying()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
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
        const float peak = loudestUntilAbove(engine, [&] { return engine.channelLevel(patch.channels.at(0).id).peak; }, 0.001F);
        engine.injectNote(1, 64, 0);
        pump(engine, 50);
        QVERIFY2(peak > 0.001F, "piano went silent after the rate change");
    }

    void unusableAudioSetupKeepsTheCurrentOne()
    {
        auto created = createQuietEngine();
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
        auto created = createRealEngine(); // (with the MIDI inputs: they are what is tested)
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
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        const auto channelWith = [&](const QString& name) {
            core::Channel channel = core::makeChannel(name);
            channel.instrument = slot(kSmall, u"Kotelnikov"_s);
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

    // An instrument shared by several songs (linked, as MainStage's aliases)
    // is loaded once: a big piano in twenty songs must not fill the memory
    // twenty times. Songs switch without loading; its settings, stored, go to
    // every song; a song that takes its own copy loads one of its own.
    void anInstrumentSharedBySongsIsLoadedOnce()
    {
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Setlist setlist;
        for (const QString& name : {u"Ballad"_s, u"Funk"_s, u"Blues"_s}) {
            core::Song song = core::makeSong(name);
            core::Channel channel = core::makeChannel(u"Keys"_s);
            core::PluginSlot keys = slot(kSmall, u"Kotelnikov"_s);
            keys.shareId = u"the-keys"_s;
            channel.instrument = keys;
            song.patches.at(0).channels = {channel};
            setlist.songs.push_back(song);
        }
        // The first song's (only) channel's instrument.
        const auto keysOf = [](core::Song& song) -> core::PluginSlot& { return song.patches.at(0).channels.at(0).instrument.value(); };
        engine.preload(setlist);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
        // Applying a song announces only a plugin it has to load.
        int loads = 0;
        engine.setProgressHandler([&](LoadStage stage, const QString& what, int, int) {
            if (stage == LoadStage::LoadingSounds && !what.isEmpty()) ++loads;
        });
        for (const core::Song& song : setlist.songs) engine.applyPatch(song.id, song.patches.at(0));
        QCOMPARE(loads, 0); // switching songs loads nothing

        QVERIFY(engine.storePluginStates(setlist).empty());
        const QByteArray stored = keysOf(setlist.songs.at(0)).state;
        QVERIFY(!stored.isEmpty());
        for (core::Song& song : setlist.songs) QCOMPARE(keysOf(song).state, stored);
        engine.preload(setlist); // opened again: still one
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});

        // The third song takes its own copy (loaded when it is applied, in Edit).
        loads = 0;
        keysOf(setlist.songs.at(2)).shareId.clear();
        engine.applyPatch(setlist.songs.at(2).id, setlist.songs.at(2).patches.at(0));
        QCOMPARE(engine.loadedPluginCount(), std::size_t{2});
        QCOMPARE(loads, 1);
        engine.preload(setlist);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{2});
        QVERIFY(engine.poll().empty());
    }

    // A song whose instrument is loaded gets linked (duplicated, or shared
    // with another song): the loaded instance is kept under its new key, so
    // playing either song loads nothing. Without the relink it would load a
    // second one (the control).
    void linkingASongKeepsItsLoadedInstrument()
    {
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Setlist setlist;
        core::Song ballad = core::makeSong(u"Ballad"_s);
        core::Channel keys = core::makeChannel(u"Keys"_s);
        keys.instrument = slot(kSmall, u"Kotelnikov"_s);
        ballad.patches.at(0).channels = {keys};
        setlist.songs = {ballad};
        engine.preload(setlist);
        engine.applyPatch(setlist.songs.at(0).id, setlist.songs.at(0).patches.at(0));
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});
        int loads = 0;
        engine.setProgressHandler([&](LoadStage stage, const QString& what, int, int) {
            if (stage == LoadStage::LoadingSounds && !what.isEmpty()) ++loads;
        });

        // The control: linked, not relinked, the song loads its instrument again.
        core::Setlist unrelinked = setlist;
        core::linkForSharing(unrelinked.songs.at(0));
        engine.applyPatch(unrelinked.songs.at(0).id, unrelinked.songs.at(0).patches.at(0));
        QCOMPARE(loads, 1);
        engine.preload(setlist); // back as it was (the extra one unloaded)
        engine.applyPatch(setlist.songs.at(0).id, setlist.songs.at(0).patches.at(0));
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});

        // Duplicated (linked) and relinked: both songs play the loaded one.
        loads = 0;
        QVERIFY(core::duplicateSong(setlist, 0).has_value());
        engine.relinkInstances(setlist);
        for (const core::Song& song : setlist.songs) engine.applyPatch(song.id, song.patches.at(0));
        QCOMPARE(loads, 0);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{1});

        // The copy takes its own: one more, and undoing it (unlinked back to
        // the shared one) moves nothing that is still played.
        core::unlinkFromSharing(setlist.songs.at(1), setlist.songs.at(1).patches.at(0).channels.at(0).instrument.value_or(core::PluginSlot{}).shareId);
        engine.relinkInstances(setlist);
        engine.applyPatch(setlist.songs.at(1).id, setlist.songs.at(1).patches.at(0));
        QCOMPARE(loads, 1);
        QCOMPARE(engine.loadedPluginCount(), std::size_t{2});
        engine.applyPatch(setlist.songs.at(0).id, setlist.songs.at(0).patches.at(0));
        QCOMPARE(loads, 1);
        QVERIFY(engine.poll().empty());
    }

    void pluginSettingsAreStoredAndComeBack()
    {
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.instrument = slot(kSmall, u"Kotelnikov"_s);
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
        const QByteArray stored = instrumentOf(setlist, 0).state;
        QVERIFY(!stored.isEmpty());
        QCOMPARE(instrumentOf(setlist, 1).state, stored); // one instance, one state

        // Opening a setlist whose settings differ reloads the plugin with them;
        // settings it cannot take are reported, not ignored.
        core::Setlist broken = setlist;
        instrumentOf(broken, 0).state = "GCS1 garbage";
        instrumentOf(broken, 1).state = "GCS1 garbage";
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
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        core::Patch patch = core::makePatch(u"Verse"_s);
        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.effects = {slot(kSmall, u"Kotelnikov"_s), slot(kSmall, u"Kotelnikov"_s, true)};
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
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);

        std::vector<core::PluginSlot> master{slot(kSmall, u"Kotelnikov"_s), slot(kSmall, u"Kotelnikov"_s, true)};
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
        if (!QFileInfo::exists(kSmall)) QSKIP("The test effect is not installed");
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
        QVERIFY2(notices.back().text.contains(test::kEffect.name), qPrintable(notices.back().text));
        QVERIFY(notices.back().level == Notice::Level::Warning);

        core::Patch patch = core::makePatch(u"Verse"_s);
        core::Channel channel = core::makeChannel(u"Keys"_s);
        channel.instrument = slot(kSmall, u"Kotelnikov"_s);
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
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
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
        MidiTrigger learned;
        QVERIFY(waitUntil(engine, [&] {
            learned = engine.takeLearnedTrigger();
            return learned != MidiTrigger{};
        }));
        QCOMPARE(learned, (MidiTrigger{MidiTrigger::Note, 0, 36}));

        ControlTriggers triggers{};
        triggers[static_cast<std::size_t>(ControlAction::NextSong)] = learned;
        engine.setControlTriggers(triggers);
        // The learned hit played (learning takes nothing away): silence its tail.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Panic: every sound stopped"_s));
        engine.panic();
        // (Silent before the pad, however late the sound system is: Mac run 8
        // measured the hit's last whisper, -99 dB, 200 ms after the panic.)
        QVERIFY2(quietWithin(engine, [&] { return engine.channelLevel(patch.channels.at(0).id).peak; }, 0.0F),
                 "the learned hit still sounded after the panic");
        engine.injectNote(1, 36, 100); // the pad
        pump(engine, 300);
        std::vector<ControlAction> actions;
        QVERIFY(waitUntil(engine, [&] {
            std::ranges::copy(engine.takeControlActions(), std::back_inserter(actions));
            return !actions.empty();
        }));
        QCOMPARE(actions, std::vector<ControlAction>{ControlAction::NextSong});
        QVERIFY(engine.takeControlActions().empty()); // once
        // The piano never heard it: no note sounded (a note is over 0.001, as below; the Mac's
        // Surge can leave a whisper after its reset: -84 dB, 6.6e-05, measured on Mac CI).
        const float padPeak = engine.channelLevel(patch.channels.at(0).id).peak;
        QVERIFY2(padPeak < 0.001F, qPrintable(u"the pad played the piano: peak %1"_s.arg(padPeak)));
        engine.injectNote(1, 36, 0);

        engine.injectNote(1, 60, 110); // any other key still plays
        QVERIFY(waitUntil(engine, [&] { return engine.channelLevel(patch.channels.at(0).id).peak > 0.001F; }));
        engine.injectNote(1, 60, 0);
    }

    void panicStopsTheSoundAndPlaysOnAfter()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(core::limits::kMinVolumeDb);
        const core::Patch patch = pianoPatch();
        engine.applyPatch(patch);
        const auto piano = [&] { return engine.channelLevel(patch.channels.at(0).id).peak; };
        engine.injectNote(1, 60, 110); // held: never released by the "player"
        QVERIFY(loudestUntilAbove(engine, piano, 0.001F) > 0.001F);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Panic: every sound stopped"_s));
        engine.panic();
        QVERIFY2(quietWithin(engine, piano, 0.0005F), "still sounding after panic");
        QVERIFY(engine.poll().empty());

        engine.injectNote(1, 64, 110); // and it plays again straight away
        QVERIFY(loudestUntilAbove(engine, piano, 0.001F) > 0.001F);
        engine.injectNote(1, 64, 0);
    }

    // A library put in a plugin folder is not a VST2 plugin because a setlist
    // names it: loading one runs its code, so only what the (sandboxed) scan
    // read as a VST2 plugin loads; anything else is refused unloaded.
    void anUnscannedLibraryInAPluginFolderIsNeverLoaded()
    {
#ifndef Q_OS_WIN
        QSKIP("Uses a Windows system library as the stranger");
#else
        QTemporaryDir folder;
        const QString stranger = folder.filePath(u"Stranger.dll"_s);
        QVERIFY(QFile::copy(u"C:/Windows/System32/version.dll"_s, stranger));
        RealEngineOptions options;
        options.midiInputs = false;
        options.pluginFolder = folder.path(); // installed there, as far as folders go
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        const std::size_t before = engine.loadedPluginCount();

        core::Patch patch = core::makePatch(u"Strange"_s);
        core::Channel channel = core::makeChannel(u"Stranger"_s);
        channel.instrument = slot(stranger, u"Stranger"_s);
        patch.channels.push_back(channel);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Stranger .* is not a VST2 plugin the scan found"_s));
        engine.applyPatch(patch);
        QCOMPARE(engine.loadedPluginCount(), before);
        const auto notices = engine.poll();
        QVERIFY(std::ranges::any_of(notices, [](const Notice& n) { return n.text.contains(u"the scan found"_s); }));
        // (Never loaded: had it been, it would have been opened and found not to be a plugin.)
#endif
    }

    void unknownPluginIsReportedNotIgnored()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        core::Patch patch = core::makePatch(u"Broken"_s);
        core::Channel channel = core::makeChannel(u"Ghost"_s);
        channel.instrument = slot(u"C:/no/such/Ghost.vst3"_s, u"Ghost"_s);
        patch.channels.push_back(channel);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ghost is not one of the installed plugins"_s));
        engine.applyPatch(patch);
        const auto notices = engine.poll();
        QCOMPARE(notices.size(), std::size_t{1});
        QVERIFY(notices.front().text.contains(u"Ghost"_s));
        QVERIFY(notices.front().level == Notice::Level::Error); // it does not play
    }

    // A setlist names its plugins by file: one from anywhere else (a shared
    // setlist naming a DLL in Downloads) is not loaded, so opening a setlist
    // can never run a program that was not installed as a plugin.
    void onlyInstalledPluginsLoad()
    {
        const QString installed = kSmall;
        if (!QFileInfo::exists(installed)) QSKIP("The test effect is not installed");
        QTemporaryDir elsewhere;
        const QString copy = elsewhere.filePath(u"Kotelnikov.vst3"_s);
        QVERIFY(test::copyPlugin(installed, copy));
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        const std::size_t before = engine.loadedPluginCount();

        core::Patch patch = core::makePatch(u"Shared"_s);
        core::Channel channel = core::makeChannel(u"Copy"_s);
        channel.instrument = slot(copy, u"Kotelnikov copy"_s);
        patch.channels.push_back(channel);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Kotelnikov copy is not one of the installed plugins"_s));
        engine.applyPatch(patch);
        QCOMPARE(engine.loadedPluginCount(), before); // never loaded
        const auto notices = engine.poll();
        QVERIFY(std::ranges::any_of(notices, [](const Notice& n) {
            return n.level == Notice::Level::Error && n.text.contains(u"not one of the installed plugins"_s);
        }));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Kotelnikov is not one of the installed plugins"_s));
        QVERIFY(!engine.createEditorForPlugin(copy).has_value()); // nor as an editor

        // Named as if in the plugin folder, climbing out of it to the root
        // (three folders deep on Windows and Linux, four on the Mac): still
        // refused.
        const QString climbing = kVst3Folder + u"/"_s + u"../"_s.repeated(static_cast<int>(kVst3Folder.count(u'/'))) +
                                 QFileInfo(copy).canonicalFilePath().mid(QDir::rootPath().size());
        QVERIFY2(QFileInfo::exists(climbing), qPrintable(climbing));
        core::Patch sneaky = core::makePatch(u"Sneaky"_s);
        core::Channel up = core::makeChannel(u"Up"_s);
        up.instrument = slot(climbing, u"Climbing copy"_s);
        sneaky.channels.push_back(up);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Climbing copy is not one of the installed plugins"_s));
        engine.applyPatch(sneaky);
        QCOMPARE(engine.loadedPluginCount(), before);
        (void)engine.poll();

        // The installed one loads (on Windows also named the way Windows may
        // write it: other case, backslashes).
        core::Patch real = core::makePatch(u"Installed"_s);
        core::Channel same = core::makeChannel(u"Kotelnikov"_s);
#ifdef Q_OS_WIN
        same.instrument = slot(QString(installed).replace(u'/', u'\\').toUpper(), u"Kotelnikov"_s);
#else
        same.instrument = slot(installed, u"Kotelnikov"_s);
#endif
        real.channels.push_back(same);
        engine.applyPatch(real);
        QCOMPARE(engine.loadedPluginCount(), before + 1);
    }

    // A plugin folder that is a link to another drive (plugins kept on a
    // music drive, linked into the VST3 folder, as Valhalla's portable
    // package does): installed, it loads. (It stopped loading when links
    // were followed out of the plugin folder: 28 September.)
    void aPluginInALinkedFolderLoads()
    {
        const QString installed = kSmall;
        if (!QFileInfo::exists(installed)) QSKIP("The test effect is not installed");
        // The plugin's own folder, linked into an otherwise empty plugin folder.
        QTemporaryDir pluginFolder;
        const QFileInfo musicDrive(QFileInfo(installed).absolutePath());
        const QString linked = pluginFolder.filePath(u"Linked"_s);
#ifdef Q_OS_WIN
        // A directory symbolic link, as `mklink /D` makes (Windows allows it
        // to administrators and in Developer Mode only).
        QProcess mklink;
        mklink.start(u"cmd.exe"_s, {u"/c"_s, u"mklink"_s, u"/D"_s, QDir::toNativeSeparators(linked),
                                    QDir::toNativeSeparators(musicDrive.absoluteFilePath())});
        QVERIFY(mklink.waitForFinished(10000));
        if (mklink.exitCode() != 0) QSKIP("This Windows account may not make symbolic links (Developer Mode is off)");
#else
        QVERIFY(QFile::link(musicDrive.absoluteFilePath(), linked));
#endif
        const QString plugin = linked + u'/' + QFileInfo(installed).fileName();
        // (Really elsewhere: outside the plugin folder once the link is followed.)
        QVERIFY(!QFileInfo(plugin).canonicalFilePath().startsWith(QFileInfo(pluginFolder.path()).canonicalFilePath()));

        RealEngineOptions options;
        options.midiInputs = false;
        options.pluginFolder = pluginFolder.path();
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        const std::size_t before = engine.loadedPluginCount();
        core::Patch patch = core::makePatch(u"Linked"_s);
        core::Channel channel = core::makeChannel(u"Kotelnikov"_s);
        channel.instrument = slot(plugin, u"Kotelnikov"_s);
        patch.channels.push_back(channel);
        engine.applyPatch(patch);
        QCOMPARE(engine.loadedPluginCount(), before + 1);
    }

    void theTempoIsSetAndOutOfRangeIsRefused()
    {
        auto created = createQuietEngine();
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
        auto created = createQuietEngine();
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
        // (The click waited for, however late the sound system is; then
        // another beat, still at -60 dB.)
        const float peak = loudestUntilAbove(engine, [&engine] { return engine.masterLevel().peak; }, 0.0001F);
        pump(engine, 300);
        const float louder = std::max(peak, engine.masterLevel().peak);
        QVERIFY2(louder > 0.0001F && louder < 0.002F, qPrintable(QString::number(louder))); // 0.5 at -60 dB
        engine.setClick(false, -60.0);
    }

    void aBackingTrackPlaysThroughTheMasterFader()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s));
        engine.setMasterVolume(-60.0);

        // Half a second of a 0.5 sine.
        const QTemporaryDir dir;
        const QString path = dir.filePath(u"backing.wav"_s);
        QVERIFY(writeSineWav(path, 24000));
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
        // Its first 0.05 s played, however late the sound system is.
        float peak = 0.0F;
        QVERIFY(waitUntil(engine, [&engine, &peak] {
            peak = std::max(peak, engine.masterLevel().peak);
            return engine.backingTrack().position > 0.05;
        }));
        peak = std::max(peak, engine.masterLevel().peak);
        QVERIFY2(std::abs(peak - 0.0005F) < 0.0001F, qPrintable(QString::number(peak))); // 0.5 at -60 dB
        // Past its end (half a second long): it stops by itself.
        QVERIFY(waitUntil(engine, [&engine] { return !engine.backingTrack().playing; }));

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

    // A stem plays locked to the track: the set lasts as long as its longest
    // file, a marker jumps both, and muting a stem is heard at once without
    // reading anything again.
    void stemsPlayLockedToTheTrackAndMuteAtOnce()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s));
        engine.setMasterVolume(-60.0);

        const QTemporaryDir dir;
        const QString track = dir.filePath(u"track.wav"_s); // half a second
        const QString stem = dir.filePath(u"stem.wav"_s);   // a whole second, quieter
        QVERIFY(writeSineWav(track, 24000, 0.5));
        QVERIFY(writeSineWav(stem, 48000, 0.25));
        engine.setBackingTrack(track);
        engine.setBackingStems({BackingStemFile{.path = stem, .volumeDb = 0.0, .mute = false, .outputPair = 0}});
        QVERIFY2(waitUntil(engine, [&engine] { return engine.backingTrack().loaded; }, 3000), "the track and stem were not read");
        QVERIFY2(std::abs(engine.backingTrack().length - 1.0) < 0.02, qPrintable(QString::number(engine.backingTrack().length)));

        // To 0.6 s: the track has ended there, the stem plays on alone.
        engine.seekBackingTrack(0.6);
        engine.playBackingTrack(true);
        (void)engine.masterLevel();
        const float stemOnly = loudestUntilAbove(engine, [&engine] { return engine.masterLevel().peak; }, 0.0002F);
        QVERIFY2(std::abs(stemOnly - 0.00025F) < 0.00005F, qPrintable(QString::number(stemOnly))); // 0.25 at -60 dB
        QVERIFY(engine.backingTrack().position >= 0.6);

        // Muted: silent at once, still loaded and playing (nothing read again).
        engine.setBackingStems({BackingStemFile{.path = stem, .volumeDb = 0.0, .mute = true, .outputPair = 0}});
        QVERIFY(!engine.backingTrack().loading);
        pump(engine, 60); // (a block already playing finishes)
        (void)engine.masterLevel();
        pump(engine, 100);
        QCOMPARE(engine.masterLevel().peak, 0.0F);
        QVERIFY(engine.backingTrack().playing);
        engine.playBackingTrack(false);
    }

    void aHeldChordRingsOnAcrossAPatchChange()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
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
        const auto master = [&engine] { return engine.masterLevel().peak; };
        QVERIFY2(loudestUntilAbove(engine, master, 0.0F) > 0.0F, "the held chord stopped at the patch change");

        engine.injectNote(1, 60, 0);
        engine.injectNote(1, 64, 0);
        pump(engine, 3000); // the piano's release
        QVERIFY2(quietWithin(engine, master, 0.0F), "the let-go chord still sounded");
    }

    // Song sections: each channel takes notes only in its sections; the
    // count moves through them at the tempo.
    void songSectionsSendNotesToTheirChannels()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0); // inaudible, measurable
        const core::SongId song = core::SongId::generate();
        core::Patch patch = pianoPatch();
        patch.channels.push_back(pianoPatch().channels.front()); // a second piano, its own instance
        const core::ChannelId verse = patch.channels.at(0).id;
        const core::ChannelId chorus = patch.channels.at(1).id;
        engine.applyPatch(song, patch);
        QVERIFY(engine.poll().empty());
        engine.setTempo(240.0); // a 4/4 bar a second
        engine.setSongSections(SongSections{.patch = patch.id,
                                            .sections = {{.bars = 1, .live = {verse}}, {.bars = 1, .live = {chorus}}},
                                            .switchEarly = false});
        const auto playNote = [&engine] {
            pump(engine, 100);
            engine.injectNote(1, 60, 110);
            pump(engine, 300);
            engine.injectNote(1, 60, 0);
        };
        const auto levelsAfter = [&engine, &verse, &chorus](const auto& play) {
            (void)engine.channelLevel(verse);
            (void)engine.channelLevel(chorus);
            play();
            return std::pair{engine.channelLevel(verse).peak, engine.channelLevel(chorus).peak};
        };

        // Stopped: the first section is in force.
        const auto [verseFirst, chorusFirst] = levelsAfter(playNote);
        QVERIFY2(verseFirst > 0.0F, "the verse's piano stayed silent in the verse");
        QCOMPARE(chorusFirst, 0.0F);
        QCOMPARE(engine.songPosition().section, 0);
        pump(engine, 3000); // the note fades away

        // Selecting the chorus sends the notes there.
        engine.jumpToSection(1);
        const auto [verseSecond, chorusSecond] = levelsAfter(playNote);
        QCOMPARE(verseSecond, 0.0F);
        QVERIFY2(chorusSecond > 0.0F, "the chorus's piano stayed silent in the chorus");
        QCOMPARE(engine.songPosition().section, 1);

        // Played: the verse, then a second later the chorus, then the end.
        engine.playSong(0, false);
        pump(engine, 200);
        SongPosition at = engine.songPosition();
        QVERIFY(at.playing);
        QCOMPARE(at.section, 0);
        QCOMPARE(at.bar, 1);
        QCOMPARE(at.bars, 1);
        for (int i = 0; i < 150 && engine.songPosition().section != 1; ++i) pump(engine, 10);
        QCOMPARE(engine.songPosition().section, 1);
        for (int i = 0; i < 150 && engine.songPosition().playing; ++i) pump(engine, 10);
        at = engine.songPosition();
        QVERIFY(!at.playing);
        QCOMPARE(at.section, 1); // the last one stays

        // Sections worked out for another patch count as none: both play.
        pump(engine, 3000);
        engine.setSongSections(SongSections{.patch = core::PatchId::generate(),
                                            .sections = {{.bars = 1, .live = {verse}}, {.bars = 1, .live = {chorus}}},
                                            .switchEarly = false});
        const auto [verseAll, chorusAll] = levelsAfter(playNote);
        QVERIFY(verseAll > 0.0F && chorusAll > 0.0F);
        QCOMPARE(engine.songPosition().section, -1);

        // No sections, one channel at a time: only the selected one plays.
        pump(engine, 3000);
        engine.setSongSections(SongSections{.patch = patch.id, .sections = {}, .switchEarly = false, .unsectioned = {{chorus}}});
        const auto [verseHeld, chorusPicked] = levelsAfter(playNote);
        QCOMPARE(verseHeld, 0.0F);
        QVERIFY2(chorusPicked > 0.0F, "the selected piano stayed silent");
        // Every channel again.
        pump(engine, 3000);
        engine.setSongSections(SongSections{.patch = patch.id, .sections = {}, .switchEarly = false, .unsectioned = std::nullopt});
        const auto [verseBack, chorusBack] = levelsAfter(playNote);
        QVERIFY(verseBack > 0.0F && chorusBack > 0.0F);

        // The song's flow: the chorus first, then the verse. Play from the
        // top is the chorus (its first part), and its piano plays.
        pump(engine, 3000);
        engine.setSongSections(SongSections{.patch = patch.id,
                                            .sections = {{.bars = 1, .live = {verse}}, {.bars = 1, .live = {chorus}}},
                                            .switchEarly = false,
                                            .parts = {1, 0},
                                            .unsectioned = std::nullopt});
        engine.playSong(-1, false);
        pump(engine, 100);
        QCOMPARE(engine.songPosition().part, 0);
        QCOMPARE(engine.songPosition().section, 1);
        const auto [verseInChorus, chorusFirstPart] = levelsAfter(playNote);
        QCOMPARE(verseInChorus, 0.0F);
        QVERIFY2(chorusFirstPart > 0.0F, "the chorus's piano stayed silent in the flow's first part");
        engine.stopSong();
    }

    // A setup saved on another system (ASIO on Linux, JACK on Windows): the
    // app starts on system audio and says why, once; Settings shows system
    // audio (not a driver this system cannot offer).
    void aDriverThisSystemLacksFallsBackToSystem()
    {
        RealEngineOptions options;
        options.midiInputs = false;
#ifdef Q_OS_WIN
        options.audio.driver = AudioDriver::Jack;
        const QString foreign = u"JACK"_s;
#else
        options.audio.driver = AudioDriver::Asio;
        const QString foreign = u"ASIO"_s;
#endif
        options.audio.device = u"Studio Interface"_s;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(foreign + u" is not available on this system"_s));
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        QVERIFY(engine.audioSetup().driver == AudioDriver::System);
        const auto notices = engine.poll();
        QCOMPARE(std::ranges::count_if(notices, [&foreign](const Notice& n) { return n.text.contains(foreign); }), 1);
    }

    // What is played never moves the song: a chord that is also the
    // chorus's leaves the verse in force (only the player moves it).
    void playingNeverMovesTheSong()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0);
        core::Patch patch = core::makePatch(u"Two"_s);
        patch.channels.push_back(core::makeChannel(u"Verse"_s));
        patch.channels.push_back(core::makeChannel(u"Chorus"_s));
        engine.applyPatch(patch);
        engine.setSongSections(SongSections{.patch = patch.id,
                                            .sections = {{.bars = 4, .live = {patch.channels.at(0).id}},
                                                         {.bars = 4, .live = {patch.channels.at(1).id}}},
                                            .switchEarly = false});
        // What the audio thread does, waited for (some sound systems run the audio in bursts).
        const auto until = [&engine](int section) {
            for (int i = 0; i < 100 && engine.songPosition().section != section; ++i) pump(engine, 50);
            return engine.songPosition().section == section;
        };
        engine.jumpToSection(0);
        QVERIFY(until(0));
        for (const int key : {53, 57, 60}) engine.injectNote(1, key, 100); // F: the chorus's chord
        pump(engine, 600);
        for (const int key : {53, 57, 60}) engine.injectNote(1, key, 0);
        pump(engine, 200);
        QCOMPARE(engine.songPosition().section, 0);
        engine.jumpToSection(1); // the player moves it
        QVERIFY(until(1));
    }

    void aCountInKeepsTheBackingTrackWaiting()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-60.0);
        core::Patch patch = core::makePatch(u"Empty"_s);
        engine.applyPatch(patch);
        const QTemporaryDir dir;
        const QString path = dir.filePath(u"backing.wav"_s);
        QVERIFY(writeSineWav(path, 96000)); // two seconds
        engine.setBackingTrack(path);
        for (int i = 0; i < 300 && !engine.backingTrack().loaded; ++i) pump(engine, 10);
        QVERIFY2(engine.backingTrack().loaded, "the backing track was not read");
        engine.setTempo(240.0); // the count-in bar is a second
        engine.setSongSections(SongSections{.patch = patch.id, .sections = {{.bars = 4, .live = {}}}, .switchEarly = false});

        engine.playSong(0, true);
        pump(engine, 500);
        QVERIFY(engine.songPosition().countingIn);
        QVERIFY(!engine.backingTrack().playing); // waiting for bar 1
        QCOMPARE(engine.backingTrack().position, 0.0);
        // The count-in's second is the audio's (a sound system running late,
        // as a virtual one does, takes longer in wall-clock time).
        QVERIFY(waitUntil(engine, [&engine] { return !engine.songPosition().countingIn; }, 3000));
        QVERIFY(waitUntil(engine, [&engine] { return engine.backingTrack().playing; }));
        const double position = engine.backingTrack().position;
        QVERIFY2(position < 0.5, qPrintable(QString::number(position))); // just started: at bar 1, not before
        engine.stopSong();
        QVERIFY(waitUntil(engine, [&engine] { return !engine.backingTrack().playing; }));
        QVERIFY(!engine.songPosition().playing);
    }

    // A loop records its channel and plays on alone, through a patch change.
    void aLoopPlaysOnAfterAPatchChangeAndClears()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0); // inaudible, measurable
        engine.setTempo(240.0);        // a bar a second
        const core::SongId song = core::SongId::generate();
        const core::Patch patch = pianoPatch();
        const core::ChannelId piano = patch.channels.front().id;
        engine.applyPatch(song, patch);
        QVERIFY(engine.poll().empty());
        const auto stateOf = [&engine, &piano] {
            const std::vector<ChannelLoop> loops = engine.loops();
            const auto it = std::ranges::find_if(loops, [&piano](const ChannelLoop& loop) { return loop.channel == piano; });
            return it != loops.end() ? it->state : LoopState::Empty;
        };
        const auto waitFor = [&engine, &stateOf](LoopState state) {
            for (int i = 0; i < 300 && stateOf() != state; ++i) pump(engine, 10);
            return stateOf() == state;
        };

        engine.loopCommand(piano, LoopCommand::Record); // from the next bar
        QVERIFY(waitFor(LoopState::Recording));
        engine.injectNote(1, 60, 110);
        pump(engine, 500);
        engine.injectNote(1, 60, 0);
        engine.loopCommand(piano, LoopCommand::Record); // closes on the next bar: a one-bar loop
        QVERIFY(waitFor(LoopState::Playing));
        const auto loops = engine.loops();
        QCOMPARE(loops.size(), std::size_t{1});
        QCOMPARE(loops.front().bars, 1);

        // Another patch, without the piano: the loop plays on.
        engine.applyPatch(song, core::makePatch(u"Chorus"_s));
        pump(engine, 4000); // the live piano has long died away
        (void)engine.masterLevel();
        pump(engine, 1200);
        QVERIFY2(engine.masterLevel().peak > 0.0F, "the loop stopped at the patch change");
        QCOMPARE(stateOf(), LoopState::Playing);

        // Loop stops it at once (not on the next bar, a second away; waited
        // for up to half a second, as some sound systems run in bursts).
        engine.loopCommand(piano, LoopCommand::PlayStop);
        QVERIFY(waitUntil(engine, [&stateOf] { return stateOf() == LoopState::Stopped; }, 500));
        (void)engine.masterLevel();
        pump(engine, 300);
        QCOMPARE(engine.masterLevel().peak, 0.0F);
        engine.loopCommand(piano, LoopCommand::PlayStop); // starts again on the next bar
        // (Armed within half a second: the bar, a second long, is still to come.)
        QVERIFY(waitUntil(engine, [&stateOf] { return stateOf() == LoopState::StartArmed; }, 500));
        QVERIFY(waitFor(LoopState::Playing));

        engine.clearAllLoops();
        // (Quiet for 1.2 s: longer than the one-bar loop, a second.)
        QVERIFY2(quietWithin(engine, [&engine] { return engine.masterLevel().peak; }, 0.0F, 1200, 4000),
                 "a cleared loop still sounded");
        QVERIFY(engine.loops().empty()); // and its room given back
    }

    // A set loop length: recording counts "bar 1 of 2", "2 of 2", and the
    // loop closes by itself at the end.
    void aLoopOfASetLengthCountsItsBarsAndClosesItself()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0);
        engine.setTempo(240.0); // a bar a second
        engine.setLoopBars(2);
        const core::Patch patch = pianoPatch();
        const core::ChannelId piano = patch.channels.front().id;
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());
        const auto loop = [&engine] {
            const std::vector<ChannelLoop> loops = engine.loops();
            return loops.empty() ? ChannelLoop{} : loops.front();
        };
        engine.loopCommand(piano, LoopCommand::Record);
        for (int i = 0; i < 200 && loop().state != LoopState::Recording; ++i) pump(engine, 10);
        QCOMPARE(loop().state, LoopState::Recording);
        // Part way through the first bar (waited for: under load the audio
        // runs in bursts, so a set wait can land anywhere).
        for (int i = 0; i < 150 && loop().bar == 1 && loop().progress < 0.1; ++i) pump(engine, 10);
        QCOMPARE(loop().bar, 1);
        QCOMPARE(loop().bars, 2); // "1/2"
        QVERIFY(loop().progress >= 0.1);
        for (int i = 0; i < 150 && loop().bar != 2 && loop().state == LoopState::Recording; ++i) pump(engine, 10);
        QCOMPARE(loop().bar, 2); // "2/2"
        for (int i = 0; i < 150 && loop().state != LoopState::Playing; ++i) pump(engine, 10);
        QCOMPARE(loop().state, LoopState::Playing); // closed by itself, no second press
        QCOMPARE(loop().bars, 2);
    }

    // One bar played with the length set to four: a loop of four bars (the
    // bar four times), which takes a layer, its recording kept whole (the
    // take and what rang on past it): no warning that it did not fit.
    void aShortLoopFillsItsSetLengthAndTakesALayer()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0);
        engine.setTempo(240.0); // a bar a second
        engine.setLoopBars(4);
        const core::Patch patch = pianoPatch();
        const core::ChannelId piano = patch.channels.front().id;
        engine.applyPatch(patch);
        QVERIFY(engine.poll().empty());
        const auto loop = [&engine] {
            const std::vector<ChannelLoop> loops = engine.loops();
            return loops.empty() ? ChannelLoop{} : loops.front();
        };
        // Every warning the engine gives while it runs (pump() drops them).
        QStringList warnings;
        const auto watch = [&engine, &warnings](int milliseconds) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            while (std::chrono::steady_clock::now() < end) {
                for (const Notice& notice : engine.poll()) {
                    if (notice.level != Notice::Level::Info) warnings << notice.text;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        engine.loopCommand(piano, LoopCommand::Record);
        for (int i = 0; i < 200 && loop().state != LoopState::Recording; ++i) pump(engine, 10);
        QCOMPARE(loop().state, LoopState::Recording);
        engine.injectNote(1, 60, 100); // a chord held over the bar line
        for (int i = 0; i < 150 && loop().bar == 1 && loop().progress < 0.7; ++i) pump(engine, 10);
        QCOMPARE(loop().bar, 1);
        engine.loopCommand(piano, LoopCommand::Record); // stopped in bar 1: a take of one bar
        for (int i = 0; i < 150 && loop().state != LoopState::Playing; ++i) watch(10);
        engine.injectNote(1, 60, 0);
        QCOMPARE(loop().state, LoopState::Playing);
        QCOMPARE(loop().bars, 4);
        watch(800); // what rang on past the bar is in; the layers are given
        engine.loopCommand(piano, LoopCommand::Record); // a layer, from the next bar
        for (int i = 0; i < 200 && loop().state != LoopState::Overdubbing; ++i) watch(10);
        QCOMPARE(loop().state, LoopState::Overdubbing);
        watch(300);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u" | "_s)));

        // An open length, stopped a little late: a loop of the bar, with
        // what was played past it kept (and nothing said: it fits).
        engine.loopCommand(piano, LoopCommand::Clear);
        for (int i = 0; i < 200 && !engine.loops().empty(); ++i) pump(engine, 10);
        engine.setLoopBars(0);
        engine.loopCommand(piano, LoopCommand::Record);
        for (int i = 0; i < 200 && loop().state != LoopState::Recording; ++i) pump(engine, 10);
        QCOMPARE(loop().state, LoopState::Recording);
        engine.injectNote(1, 64, 100);
        for (int i = 0; i < 200 && !(loop().bar == 2 && loop().progress > 0.1); ++i) pump(engine, 10);
        engine.loopCommand(piano, LoopCommand::Record); // a little into bar 2: closes on it
        for (int i = 0; i < 150 && loop().state != LoopState::Playing; ++i) watch(10);
        engine.injectNote(1, 64, 0);
        QCOMPARE(loop().state, LoopState::Playing);
        QCOMPARE(loop().bars, 1);
        watch(800);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u" | "_s)));
    }

    // The looper's buttons (here two pads): pressed, and both held = clear;
    // the instruments never hear them.
    void looperButtonsArePressedAndHeldTogetherClear()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s));
        LoopTriggers buttons{};
        buttons.at(static_cast<std::size_t>(LoopAction::Record)) = MidiTrigger{.kind = MidiTrigger::Note, .channel = 0, .number = 36};
        buttons.at(static_cast<std::size_t>(LoopAction::PlayStop)) = MidiTrigger{.kind = MidiTrigger::Note, .channel = 0, .number = 37};
        engine.setLoopControls(buttons, SelectorKnob{});
        // What the buttons did, waited for (up to a second: some sound
        // systems, WSLg's PulseAudio among them, run the audio in bursts).
        const auto actions = [&engine] {
            std::vector<LoopAction> taken;
            for (int i = 0; i < 20 && taken.empty(); ++i) {
                pump(engine, 50);
                std::ranges::copy(engine.takeLoopActions(), std::back_inserter(taken));
            }
            return taken;
        };

        engine.injectNote(1, 36, 100);
        QCOMPARE(actions(), std::vector<LoopAction>{LoopAction::Record});
        QCOMPARE(int(engine.keyboardActivity().velocity.at(36)), 0); // a control, not a note
        engine.injectNote(1, 36, 0);
        QVERIFY(actions().empty()); // letting go does nothing

        engine.injectNote(1, 37, 100); // PlayStop held...
        QCOMPARE(actions(), std::vector<LoopAction>{LoopAction::PlayStop});
        engine.injectNote(1, 36, 100); // ... and Record with it: clear
        QCOMPARE(actions(), std::vector<LoopAction>{LoopAction::Clear});
        engine.injectNote(1, 36, 0);
        engine.injectNote(1, 37, 0);
        QVERIFY(actions().empty());
        engine.injectNote(1, 36, 100); // alone again: Record
        QCOMPARE(actions(), std::vector<LoopAction>{LoopAction::Record});
    }

    // A knob learned for the app (the master fader here: the mod wheel's
    // CC 1) reports where it is turned to and the instruments never hear
    // it; an unlearned controller still reaches them (a fader for
    // expression, CC 11).
    void anAppKnobIsReadAndNeverPlayed()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.applyPatch(core::makePatch(u"Empty"_s));
        AppKnobs knobs{};
        knobs.at(0) = MidiTrigger{.kind = MidiTrigger::ControlChange, .channel = 0, .number = 1};
        engine.setAppKnobs(knobs);
        const auto value = [&engine] {
            for (int i = 0; i < 20; ++i) {
                pump(engine, 50);
                if (const int v = engine.takeAppKnobValues().at(0); v >= 0) return v;
            }
            return -1;
        };
        engine.injectController(1, 1, 90);
        QCOMPARE(value(), 90);
        QCOMPARE(engine.keyboardActivity().modWheel, 0); // the instruments did not hear it
        engine.setAppKnobs(AppKnobs{}); // forgotten: the mod wheel plays again
        engine.injectController(1, 1, 64);
        for (int i = 0; i < 20 && engine.keyboardActivity().modWheel != 64; ++i) pump(engine, 50);
        QCOMPARE(engine.keyboardActivity().modWheel, 64);
        QCOMPARE(value(), -1);
    }

    // Each key pressed is kept with when it came (to the millisecond, for
    // scoring a warm-up), once; a key let go is not a press.
    void keyPressesAreTimed()
    {
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        (void)engine.takeKeyPresses();
        const auto before = std::chrono::steady_clock::now().time_since_epoch();
        engine.injectNote(1, 60, 90);
        QTest::qSleep(20);
        engine.injectNote(1, 60, 0);
        engine.injectNote(1, 64, 70);
        const auto after = std::chrono::steady_clock::now().time_since_epoch();
        const std::vector<KeyPress> presses = engine.takeKeyPresses();
        QCOMPARE(presses.size(), std::size_t{2});
        QCOMPARE(presses.at(0).note, 60);
        QCOMPARE(presses.at(0).velocity, 90);
        QCOMPARE(presses.at(1).note, 64);
        QVERIFY(presses.at(0).timeNs >= std::chrono::duration_cast<std::chrono::nanoseconds>(before).count());
        QVERIFY(presses.at(1).timeNs <= std::chrono::duration_cast<std::chrono::nanoseconds>(after).count());
        QVERIFY(presses.at(1).timeNs - presses.at(0).timeNs >= 15'000'000); // the 20 ms between them
        QVERIFY(engine.takeKeyPresses().empty()); // taken once
    }

    void aPluginsParametersAreListedForKnobs()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        auto created = createQuietEngine();
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
        if (!QFileInfo::exists(kPiano)) QSKIP("The test instrument is not installed");
        // A "bundled" folder holding a copy of an installed plugin (same name
        // and maker): the installed one is kept, not listed twice.
        const QTemporaryDir bundled;
        QVERIFY(test::copyPlugin(kPiano, bundled.filePath(QFileInfo(kPiano).fileName())));
        RealEngineOptions options;
        options.bundledPluginFolder = bundled.path();
        auto created = createRealEngine(options);
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        const auto plugins = (*created)->availablePlugins();
        const auto isIt = [](const PluginInfo& p) { return p.name == test::kInstrument.name; };
        QCOMPARE(std::ranges::count_if(plugins, isIt), 1);
        const auto piano = std::ranges::find_if(plugins, isIt);
        QCOMPARE(piano->id, kPiano);
    }
};

QTEST_GUILESS_MAIN(TestRealEngine)
#include "tst_real_engine.moc"
