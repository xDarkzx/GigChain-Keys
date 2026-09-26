#include "Metronome.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace gigchain::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Renders `seconds` of the click at `bpm`, block by block as the engine
// does, and returns the samples where a click starts (silence before it).
std::vector<int> clickStarts(Metronome& click, double bpm, double seconds, std::vector<float>* all = nullptr)
{
    std::vector<int> starts;
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    double ppq = 0.0;
    float previous = 0.0F;
    int silentRun = 1000;
    const int blocks = static_cast<int>(seconds * kRate / kBlock);
    for (int b = 0; b < blocks; ++b) {
        std::ranges::fill(left, 0.0F);
        std::ranges::fill(right, 0.0F);
        const TimeInfo time{.tempo = bpm, .sampleRate = kRate, .samplePosition = static_cast<int64_t>(b) * kBlock,
                            .ppqPosition = ppq, .barStartPpq = std::floor(ppq / 4.0) * 4.0, .timeSigNumerator = 4,
                            .timeSigDenominator = 4};
        click.process(AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, time);
        for (int i = 0; i < kBlock; ++i) {
            const float v = left.at(static_cast<std::size_t>(i));
            if (all != nullptr) all->push_back(v);
            // The first sample of a click is sin(0) = 0: a click starts at the
            // sample before its first non-zero one.
            if (v != 0.0F && previous == 0.0F && silentRun > 100) starts.push_back(b * kBlock + i - 1);
            silentRun = v == 0.0F ? silentRun + 1 : 0;
            previous = v;
        }
        ppq += kBlock * bpm / 60.0 / kRate;
    }
    return starts;
}

} // namespace

class TestMetronome : public QObject
{
    Q_OBJECT

private slots:
    void offIsSilent()
    {
        Metronome click;
        QVERIFY(clickStarts(click, 120.0, 2.0).empty());
    }

    void clicksOnEveryBeatAtTheTempo()
    {
        Metronome click;
        click.setOn(true);
        // 120 BPM: a beat every half second, 24000 samples.
        const std::vector<int> starts = clickStarts(click, 120.0, 2.1);
        QCOMPARE(starts.size(), std::size_t{5}); // 0, 0.5, 1.0, 1.5, 2.0 s
        for (std::size_t i = 0; i < starts.size(); ++i) {
            QVERIFY2(std::abs(starts.at(i) - (static_cast<int>(i) * 24000)) <= 1, qPrintable(QString::number(starts.at(i))));
        }
        // 90 BPM: every 32000 samples.
        Metronome slower;
        slower.setOn(true);
        const std::vector<int> slow = clickStarts(slower, 90.0, 1.5);
        QCOMPARE(slow.size(), std::size_t{3});
        QVERIFY(std::abs(slow.at(1) - 32000) <= 1);
    }

    void theBarsFirstBeatIsHigher()
    {
        Metronome click;
        click.setOn(true);
        std::vector<float> samples;
        const std::vector<int> starts = clickStarts(click, 120.0, 2.1, &samples);
        // Count zero crossings in each click's first 10 ms: pitch.
        const auto crossings = [&samples](int from) {
            int n = 0;
            for (int i = from + 1; i < from + 480; ++i) {
                if ((samples.at(static_cast<std::size_t>(i)) > 0.0F) != (samples.at(static_cast<std::size_t>(i - 1)) > 0.0F)) ++n;
            }
            return n;
        };
        QVERIFY(crossings(starts.at(0) + 1) > crossings(starts.at(1) + 1)); // beat 1 of the bar vs beat 2
        QCOMPARE(crossings(starts.at(1) + 1), crossings(starts.at(2) + 1));
    }

    void volumeScalesTheClick()
    {
        Metronome loud;
        loud.setOn(true);
        std::vector<float> a;
        clickStarts(loud, 120.0, 0.1, &a);
        Metronome quiet;
        quiet.setOn(true);
        quiet.setVolumeDb(-20.0);
        std::vector<float> b;
        clickStarts(quiet, 120.0, 0.1, &b);
        const auto peak = [](const std::vector<float>& v) {
            return std::accumulate(v.begin(), v.end(), 0.0F, [](float p, float s) { return std::max(p, std::abs(s)); });
        };
        QVERIFY(peak(a) > 0.3F);
        QVERIFY(std::abs(peak(b) / peak(a) - 0.1F) < 0.01F);
    }
};

QTEST_GUILESS_MAIN(TestMetronome)
#include "tst_metronome.moc"
