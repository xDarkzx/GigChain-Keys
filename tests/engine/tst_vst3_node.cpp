// Integration tests against real installed plugins. Each test skips when its
// plugin is not installed, so the suite still passes on other machines.
#include "Vst3Node.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace openstage;
using namespace openstage::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kInstrument = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;
const QString kEffect = u"C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-Q 3.vst3"_s;
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

float blockPeak(const std::vector<float>& left, const std::vector<float>& right)
{
    float peak = 0.0F;
    for (std::size_t i = 0; i < left.size(); ++i) peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
    return peak;
}

} // namespace

class TestVst3Node : public QObject
{
    Q_OBJECT

private slots:
    void missingBundleIsAnError()
    {
        const auto node = Vst3Node::load(u"C:/nowhere/Nothing.vst3"_s, kRate, kBlock);
        QVERIFY(!node);
        QVERIFY(node.error().code == core::ErrorCode::FileNotFound);
    }

    void notAPluginIsAnError()
    {
        // Rejected before the Windows loader ever sees it.
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
        const DWORD before = GetThreadErrorMode();
        const auto node = Vst3Node::load(path, kRate, kBlock);
        QVERIFY(!node);
        QVERIFY(node.error().code == core::ErrorCode::InvalidData);
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
                             AudioBlock{left.data(), right.data(), kBlock});
            loudest = std::max(loudest, blockPeak(left, right));
        }
        QVERIFY2(loudest > 0.001F, "piano produced silence");
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
            (*node)->process({}, AudioBlock{left.data(), right.data(), kBlock});
            loudest = std::max(loudest, blockPeak(left, right));
        }
        QVERIFY2(loudest > 0.1F, "EQ swallowed the signal");
    }
};

QTEST_GUILESS_MAIN(TestVst3Node)
#include "tst_vst3_node.moc"
