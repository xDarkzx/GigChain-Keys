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
