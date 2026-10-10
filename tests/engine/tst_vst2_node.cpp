// VST2 hosting against real installed plugins. Each test skips when its
// plugin is not installed, so the suite still passes on other machines (CI
// has none: these run on a player's machine, as the VST3 ones do).
#include "PluginCatalog.h"
#include "PluginNode.h"
#include "Vst2Node.h"

#include <QFileInfo>
#include <QtTest>

#include <cmath>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

#ifdef Q_OS_WIN
// A free VST2 instrument (Crow Hill / UJAM Pocket Strings) and effect (Tokyo Dawn Labs' Kotelnikov).
const QString kInstrument = u"C:/Program Files/VSTPlugins/Crow Hill/Pocket Strings.dll"_s;
const QString kEffect = u"C:/Program Files/Steinberg/VSTPlugins/TDR Kotelnikov.dll"_s;
const QString kNotAPlugin = u"C:/Windows/System32/version.dll"_s;
#else
const QString kInstrument;
const QString kEffect;
const QString kNotAPlugin;
#endif
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
const TimeInfo kTime{.tempo = 120.0, .sampleRate = kRate, .samplePosition = 0, .ppqPosition = 0.0,
                     .barStartPpq = 0.0, .timeSigNumerator = 4, .timeSigDenominator = 4};

// The loudest sample of `blocks` blocks, the first with `events`.
float play(PluginNode& node, int blocks, const std::vector<MidiEvent>& events = {})
{
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    float loudest = 0.0F;
    for (int block = 0; block < blocks; ++block) {
        node.process(block == 0 ? std::span<const MidiEvent>(events) : std::span<const MidiEvent>(),
                     AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, kTime);
        for (std::size_t i = 0; i < left.size(); ++i) loudest = std::max({loudest, std::abs(left.at(i)), std::abs(right.at(i))});
    }
    return loudest;
}

} // namespace

class TestVst2Node : public QObject
{
    Q_OBJECT

private slots:
    // A library that is not a VST2 plugin is refused with why, not run.
    void aLibraryThatIsNotAPluginIsRefused()
    {
        if (kNotAPlugin.isEmpty() || !QFileInfo::exists(kNotAPlugin)) QSKIP("No such library here");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not a VST2 plugin"_s));
        const auto node = PluginNode::load(kNotAPlugin, kRate, kBlock);
        QVERIFY(!node);
        QVERIFY2(node.error().message.contains(u"not a VST2 plugin"_s), qPrintable(node.error().message));
    }

    // The catalog finds a folder's VST2 plugins (and only those) and says
    // what each is, as the browser lists it.
    void theCatalogFindsVst2Plugins()
    {
        if (kEffect.isEmpty() || !QFileInfo::exists(kEffect)) QSKIP("The test VST2 effect is not installed");
        const QString folder = QFileInfo(kEffect).absolutePath();
        const std::vector<PluginInfo> found = PluginCatalog::scan(folder, {}, nullptr, {}, nullptr, {}, PluginFormat::Vst2);
        const auto kotelnikov = std::ranges::find_if(found, [](const PluginInfo& p) { return p.name == u"TDR Kotelnikov"_s; });
        QVERIFY(kotelnikov != found.end());
        QCOMPARE(kotelnikov->format, PluginFormat::Vst2);
        QCOMPARE(kotelnikov->kind, PluginKind::Effect);
        QCOMPARE(kotelnikov->vendor, u"Tokyo Dawn Labs"_s);
        QVERIFY(QFileInfo(kotelnikov->id) == QFileInfo(kEffect));
        QVERIFY(std::ranges::none_of(found, [](const PluginInfo& p) { return p.id.endsWith(u".vst3"_s); }));
    }

    // Through the format-neutral door: a VST2 instrument plays a note, holds
    // it, and lets it go when its notes are released (a patch change).
    void aVst2InstrumentPlaysAndLetsGo()
    {
        if (kInstrument.isEmpty() || !QFileInfo::exists(kInstrument)) QSKIP("The test VST2 instrument is not installed");
        auto loaded = PluginNode::load(kInstrument, kRate, kBlock);
        QVERIFY2(loaded, loaded ? "" : qPrintable(loaded.error().message));
        PluginNode& node = **loaded;
        QVERIFY(node.isInstrument());
        QCOMPARE(node.name(), u"Pocket Strings"_s);

        const float silent = play(node, 4);
        QCOMPARE(silent, 0.0F);
        // A sampler reads its samples in the background after loading: given time, as a gig's start-up does.
        QTest::qWait(3000);
        const float playing = play(node, 80, {MidiEvent{.status = 0x90, .data1 = 60, .data2 = 110, .sampleOffset = 0}});
        QVERIFY2(playing > 0.001F, qPrintable(QString::number(playing)));
        QVERIFY(node.holdsNotes());

        node.releaseAllNotes();
        (void)play(node, 1);
        QVERIFY(!node.holdsNotes());

        // The pedal holds what was played after the key is up: still held,
        // and let go with the notes (the pedal up too), not left ringing.
        (void)play(node, 1, {MidiEvent{.status = 0xB0, .data1 = 64, .data2 = 127, .sampleOffset = 0},
                             MidiEvent{.status = 0x90, .data1 = 64, .data2 = 100, .sampleOffset = 0}});
        (void)play(node, 1, {MidiEvent{.status = 0x80, .data1 = 64, .data2 = 0, .sampleOffset = 0}});
        QVERIFY(node.holdsNotes()); // the key is up, the pedal is not
        node.releaseAllNotes();
        (void)play(node, 1);
        QVERIFY(!node.holdsNotes());
    }

    // An effect's settings come back in a fresh instance (what a setlist does
    // when it opens): a parameter moved, saved, and found again.
    void aVst2EffectsSettingsComeBack()
    {
        if (kEffect.isEmpty() || !QFileInfo::exists(kEffect)) QSKIP("The test VST2 effect is not installed");
        auto first = PluginNode::load(kEffect, kRate, kBlock);
        QVERIFY2(first, first ? "" : qPrintable(first.error().message));
        QVERIFY(!(*first)->isInstrument());
        const std::vector<PluginNode::Parameter> parameters = (*first)->parameters();
        QVERIFY(!parameters.empty());
        const uint32_t id = parameters.front().id;
        const double before = (*first)->parameterValue(id);
        const double moved = before < 0.5 ? 0.83 : 0.17;
        (*first)->queueParameter(id, moved, 0);
        (void)play(**first, 2);
        QVERIFY(std::abs((*first)->parameterValue(id) - moved) < 0.02);

        const auto saved = (*first)->saveEncodedState();
        QVERIFY2(saved, saved ? "" : qPrintable(saved.error().message));
        QVERIFY(saved->startsWith("GCV2"));

        auto fresh = PluginNode::load(kEffect, kRate, kBlock);
        QVERIFY(fresh);
        QVERIFY(std::abs((*fresh)->parameterValue(id) - before) < 0.02); // starts at its default
        const auto restored = (*fresh)->restoreEncodedState(*saved);
        QVERIFY2(restored, restored ? "" : qPrintable(restored.error().message));
        QVERIFY2(std::abs((*fresh)->parameterValue(id) - moved) < 0.02, qPrintable(QString::number((*fresh)->parameterValue(id))));

        // A VST3 plugin's settings are not taken by a VST2 one: said, not tried.
        const auto wrong = (*fresh)->restoreEncodedState(QByteArray("GCS1") + QByteArray(16, 'x'));
        QVERIFY(!wrong);
        QVERIFY(wrong.error().message.contains(u"VST3"_s));
    }
};

QTEST_MAIN(TestVst2Node)
#include "tst_vst2_node.moc"
