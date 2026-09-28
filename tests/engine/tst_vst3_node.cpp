// Integration tests against real installed plugins. Each test skips when its
// plugin is not installed, so the suite still passes on other machines.
#include "ComponentHandler.h"
#include "PluginModules.h"
#include "Vst3Node.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kInstrument = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;
const QString kEffect = u"C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-Q 3.vst3"_s;
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
const TimeInfo kTime{.tempo = 120.0, .sampleRate = kRate, .samplePosition = 0, .ppqPosition = 0.0,
                     .barStartPpq = 0.0, .timeSigNumerator = 4, .timeSigDenominator = 4};

float blockPeak(const std::vector<float>& left, const std::vector<float>& right)
{
    float peak = 0.0F;
    for (std::size_t i = 0; i < left.size(); ++i) peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
    return peak;
}

// Plays `blocks` blocks on a fresh instrument, each block's events from
// `at(block)`, and returns the loudest sample from block `from` on.
template <typename EventsAt>
float loudestFrom(Vst3Node& node, int blocks, int from, EventsAt at)
{
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    float loudest = 0.0F;
    for (int block = 0; block < blocks; ++block) {
        const std::vector<MidiEvent> events = at(block);
        node.process(std::span<const MidiEvent>(events), AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock},
                     kTime);
        if (block >= from) loudest = std::max(loudest, blockPeak(left, right));
    }
    return loudest;
}

} // namespace

class TestVst3Node : public QObject
{
    Q_OBJECT

private slots:
    void missingBundleIsAnError()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Plugin not found: C:/nowhere/Nothing.vst3"_s));
        const auto node = Vst3Node::load(u"C:/nowhere/Nothing.vst3"_s, kRate, kBlock);
        QVERIFY(!node);
        QVERIFY(node.error().code == core::ErrorCode::FileNotFound);
    }

    void notAPluginIsAnError()
    {
        // Rejected before the Windows loader ever sees it, and logged.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not a \\.vst3 plugin"_s));
        const auto node = Vst3Node::load(QFINDTESTDATA("tst_vst3_node.cpp"), kRate, kBlock);
        QVERIFY(!node);
        QVERIFY(node.error().code == core::ErrorCode::InvalidData);
        QVERIFY2(node.error().message.contains(u"not a .vst3"_s), qPrintable(node.error().message));
    }

    void corruptVst3FailsWithoutWindowsDialog()
    {
        // A file named .vst3 that is not a DLL reaches LoadLibrary. Windows
        // must not show its "Bad Image" dialog (it would block a gig); the
        // failure comes back as an error and the thread error mode is restored.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Corrupt.vst3"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("MZ this is not a real plugin");
        }
        // The exact Windows loader reason is both returned and logged.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Corrupt\\.vst3.*LoadLibraryW failed"_s));
        const DWORD before = GetThreadErrorMode();
        const auto node = Vst3Node::load(path, kRate, kBlock);
        QVERIFY(!node);
        QVERIFY(node.error().code == core::ErrorCode::InvalidData);
        QVERIFY2(node.error().message.contains(u"LoadLibraryW failed"_s), qPrintable(node.error().message));
        QCOMPARE(GetThreadErrorMode(), before);
    }

    void instrumentPlaysANote()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kInstrument, kRate, kBlock);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));
        QVERIFY((*node)->isInstrument());
        QVERIFY(!(*node)->name().isEmpty());

        std::vector<float> left(kBlock);
        std::vector<float> right(kBlock);
        const MidiEvent on[] = {MidiEvent{0x90, 60, 110, 0}};
        float loudest = 0.0F;
        for (int block = 0; block < 40; ++block) {
            (*node)->process(block == 0 ? std::span<const MidiEvent>(on) : std::span<const MidiEvent>(),
                             AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, kTime);
            loudest = std::max(loudest, blockPeak(left, right));
        }
        QVERIFY2(loudest > 0.001F, "piano produced silence");
        QVERIFY(!(*node)->takeProblems().any());

        // A block bigger than prepared is refused (silence) and counted, not ignored.
        std::vector<float> bigLeft(static_cast<std::size_t>(kBlock) * 2);
        std::vector<float> bigRight(static_cast<std::size_t>(kBlock) * 2);
        (*node)->process({}, AudioBlock{.left = bigLeft.data(), .right = bigRight.data(), .frames = kBlock * 2}, kTime);
        const auto problems = (*node)->takeProblems();
        QCOMPARE(problems.oversizedBlocks, uint64_t{1});
        QVERIFY(!(*node)->takeProblems().any()); // taking resets
    }

    // Velocity reaches the plugin: the same key, softly and hard (as the
    // keyboard sends them: measured 13 to 85 from an Impact GXP61).
    void softNotesPlaySofterThanHardOnes()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        auto play = [](uint8_t velocity) {
            auto node = Vst3Node::load(kInstrument, kRate, kBlock);
            if (!node) return -1.0F;
            return loudestFrom(**node, 40, 0, [velocity](int block) {
                return block == 0 ? std::vector<MidiEvent>{MidiEvent{.status = 0x90, .data1 = 60, .data2 = velocity}}
                                  : std::vector<MidiEvent>{};
            });
        };
        const float soft = play(20);
        const float hard = play(120);
        qInfo() << "velocity 20 peak" << soft << "velocity 120 peak" << hard;
        QVERIFY(soft > 0.0F && hard > 0.0F);
        QVERIFY2(soft < hard * 0.7F, qPrintable(u"soft %1, hard %2"_s.arg(soft).arg(hard)));
    }

    // The sustain pedal (controller 64, as the keyboard sends it) holds a
    // released note: it still sounds long after the key is let go.
    void theSustainPedalHoldsReleasedNotes()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        constexpr int kRelease = 10;     // key let go (~53 ms at 256 frames, 48 kHz)
        constexpr int kListenFrom = 120; // ~0.6 s after
        auto afterRelease = [](bool pedal) {
            auto node = Vst3Node::load(kInstrument, kRate, kBlock);
            if (!node) return -1.0F;
            return loudestFrom(**node, 160, kListenFrom, [pedal](int block) {
                std::vector<MidiEvent> events;
                if (block == 0) {
                    if (pedal) events.push_back(MidiEvent{.status = 0xB0, .data1 = 64, .data2 = 127}); // sustain on
                    events.push_back(MidiEvent{.status = 0x90, .data1 = 60, .data2 = 100});
                }
                if (block == kRelease) events.push_back(MidiEvent{.status = 0x80, .data1 = 60, .data2 = 0});
                return events;
            });
        };
        const float damped = afterRelease(false);
        const float held = afterRelease(true);
        qInfo() << "after release: without pedal" << damped << "with pedal" << held;
        QVERIFY(damped >= 0.0F && held >= 0.0F);
        QVERIFY2(held > damped * 3.0F && held > 0.001F, qPrintable(u"without pedal %1, with pedal %2"_s.arg(damped).arg(held)));
    }

    void stateMovesToAFreshInstance()
    {
        // How a plugin is reloaded (e.g. at another window size) without losing its sound.
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        auto first = Vst3Node::load(kInstrument, kRate, kBlock);
        QVERIFY(first.has_value());
        QCOMPARE((*first)->bundlePath(), kInstrument);
        const auto saved = (*first)->saveState();
        QVERIFY2(saved.has_value(), saved ? "" : qPrintable(saved.error().message));
        QVERIFY(!saved->component.isEmpty());

        auto second = Vst3Node::load(kInstrument, kRate, kBlock);
        QVERIFY(second.has_value());
        const auto restored = (*second)->restoreState(*saved);
        QVERIFY2(restored.has_value(), restored ? "" : qPrintable(restored.error().message));
        const auto again = (*second)->saveState();
        QVERIFY(again.has_value());
        QCOMPARE(again->component.size(), saved->component.size());
    }

    void stateIsStoredCompactly()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kInstrument, kRate, kBlock);
        QVERIFY(node.has_value());
        const auto saved = (*node)->saveState();
        QVERIFY(saved.has_value());
        const QByteArray bytes = saved->encode();
        QVERIFY(bytes.size() < saved->component.size()); // compressed; Arturia's two identical copies stored once
        const auto decoded = Vst3Node::State::decode(bytes);
        QVERIFY2(decoded.has_value(), decoded ? "" : qPrintable(decoded.error().message));
        QCOMPARE(decoded->component, saved->component);
        QCOMPARE(decoded->controller, saved->controller);
    }

    void differentControllerStateSurvivesEncoding()
    {
        const Vst3Node::State state{QByteArray("component bytes"), QByteArray("controller bytes")};
        const auto decoded = Vst3Node::State::decode(state.encode());
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->component, state.component);
        QCOMPARE(decoded->controller, state.controller);
    }

    void brokenStateIsAnError()
    {
        for (const QByteArray& junk : {QByteArray(), QByteArray("junk"), QByteArray("GCS1 not compressed")}) {
            const auto decoded = Vst3Node::State::decode(junk);
            QVERIFY(!decoded);
            QVERIFY(decoded.error().code == core::ErrorCode::InvalidData);
        }
    }

    void pluginEditsAreNoticed()
    {
        ComponentHandler handler;
        QVERIFY(!handler.takeEdited());
        handler.beginEdit(1);
        handler.performEdit(1, 0.5); // a knob turned in the plugin's window
        handler.endEdit(1);
        QVERIFY(handler.takeEdited());
        QVERIFY(!handler.takeEdited()); // reported once

        handler.restartComponent(Steinberg::Vst::kLatencyChanged); // not a settings change
        QVERIFY(!handler.takeEdited());
        handler.restartComponent(Steinberg::Vst::kParamValuesChanged); // a preset chosen in the plugin
        QVERIFY(handler.takeEdited());

        void* second = nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a VST3 interface id is a raw TUID array by design
        QCOMPARE(handler.queryInterface(Steinberg::Vst::IComponentHandler2::iid, &second), Steinberg::kResultOk);
        static_cast<Steinberg::Vst::IComponentHandler2*>(second)->setDirty(true);
        QVERIFY(handler.takeEdited());
    }

    void instancesOfAPluginShareOneLibrary()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("Arturia Piano V2 not installed");
        const std::size_t before = PluginModules::loadedCount();
        {
            auto first = Vst3Node::load(kInstrument, kRate, kBlock);
            auto second = Vst3Node::load(kInstrument, kRate, kBlock);
            QVERIFY(first.has_value() && second.has_value());
            QCOMPARE(PluginModules::loadedCount(), before + 1); // one library, two instances

            first->reset(); // one goes: the other still plays
            std::vector<float> left(kBlock), right(kBlock);
            float peak = 0.0F;
            const MidiEvent on[] = {MidiEvent{0x90, 60, 110, 0}};
            for (int block = 0; block < 40; ++block) {
                (*second)->process(block == 0 ? std::span<const MidiEvent>(on) : std::span<const MidiEvent>(),
                                   AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, kTime);
                peak = std::max(peak, blockPeak(left, right));
            }
            QVERIFY2(peak > 0.001F, "the remaining instance went silent");
        }
        QCOMPARE(PluginModules::loadedCount(), before); // the last one went: the library is unloaded
    }

    void sidechainEffectLoads()
    {
        // FabFilter Pro-DS (has a sidechain bus) crashed during activation when
        // setupProcessing ran before the buses were activated.
        const QString proDs = u"C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-DS.vst3"_s;
        if (!QFileInfo::exists(proDs)) QSKIP("FabFilter Pro-DS not installed");
        auto node = Vst3Node::load(proDs, kRate, kBlock);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));
        std::vector<float> left(kBlock, 0.1F);
        std::vector<float> right(kBlock, 0.1F);
        (*node)->process({}, AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, kTime);
        QVERIFY(!(*node)->takeProblems().any());
    }

    void effectPassesAudioThrough()
    {
        if (!QFileInfo::exists(kEffect)) QSKIP("FabFilter Pro-Q 3 not installed");
        auto node = Vst3Node::load(kEffect, kRate, kBlock);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));
        QVERIFY(!(*node)->isInstrument());

        std::vector<float> left(kBlock);
        std::vector<float> right(kBlock);
        float loudest = 0.0F;
        for (int block = 0; block < 20; ++block) {
            for (int i = 0; i < kBlock; ++i) {
                const double t = static_cast<double>(block * kBlock + i) / kRate;
                left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] =
                    static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * t));
            }
            (*node)->process({}, AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, kTime);
            loudest = std::max(loudest, blockPeak(left, right));
        }
        QVERIFY2(loudest > 0.1F, "EQ swallowed the signal");
    }
};

QTEST_GUILESS_MAIN(TestVst3Node)
#include "tst_vst3_node.moc"
