// Integration tests of the real engine on this machine's audio device and
// installed plugins. Master volume is set to silence first, so nothing is
// heard; the channel meters are measured before the master fader.
#include "PluginCatalog.h"
#include "openstage/core/Limits.h"
#include "openstage/engine/RealEngineFactory.h"

#include <QFileInfo>
#include <QtTest>

#include <chrono>
#include <thread>

using namespace openstage;
using namespace openstage::engine;
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
