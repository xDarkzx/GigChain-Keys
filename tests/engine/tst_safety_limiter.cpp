#include "SafetyLimiter.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace gigchain::engine;

namespace {

constexpr double kRate = 48000.0;

struct Buffer
{
    std::vector<float> left;
    std::vector<float> right;
    AudioBlock block() { return AudioBlock{left.data(), right.data(), static_cast<int>(left.size())}; }
};

Buffer sine(double amplitude, int frames, double hz = 440.0)
{
    Buffer b;
    for (int i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / kRate));
        b.left.push_back(v);
        b.right.push_back(v * 0.5F);
    }
    return b;
}

float peakOf(const Buffer& b)
{
    float peak = 0.0F;
    for (std::size_t i = 0; i < b.left.size(); ++i) peak = std::max({peak, std::abs(b.left[i]), std::abs(b.right[i])});
    return peak;
}

} // namespace

class TestSafetyLimiter : public QObject
{
    Q_OBJECT

private slots:
    void quietSoundPassesUntouched()
    {
        SafetyLimiter limiter;
        limiter.setSampleRate(kRate);
        Buffer quiet = sine(0.5, 4800); // about -6 dB: under the -1 dB ceiling
        const Buffer original = quiet;
        limiter.process(quiet.block());
        QCOMPARE(quiet.left, original.left); // sample for sample
        QCOMPARE(quiet.right, original.right);
        QVERIFY(!limiter.takeActivity());
    }

    void nothingLeavesAboveTheCeiling()
    {
        SafetyLimiter limiter;
        limiter.setSampleRate(kRate);
        const float ceiling = static_cast<float>(std::pow(10.0, -1.0 / 20.0));
        for (const double amplitude : {1.0, 2.0, 8.0, 50.0}) { // up to +34 dB: a runaway synth
            Buffer loud = sine(amplitude, 9600);
            limiter.process(loud.block());
            QVERIFY2(peakOf(loud) <= ceiling * 1.00001F, qPrintable(QString::number(peakOf(loud))));
        }
        QVERIFY(limiter.takeActivity()); // the LIM light
        QVERIFY(!limiter.takeActivity()); // reported once

        limiter.setCeilingDb(-6.0);
        Buffer loud = sine(1.0, 4800);
        limiter.process(loud.block());
        QVERIFY(peakOf(loud) <= static_cast<float>(std::pow(10.0, -6.0 / 20.0)) * 1.00001F);
    }

    void comesBackToFullLevelAfterTheLoudPart()
    {
        SafetyLimiter limiter;
        limiter.setSampleRate(kRate);
        Buffer loud = sine(4.0, 4800);
        limiter.process(loud.block());
        (void)limiter.takeActivity();
        Buffer silence = sine(0.0, static_cast<int>(kRate)); // one second
        limiter.process(silence.block());
        Buffer quiet = sine(0.5, 4800);
        const Buffer original = quiet;
        limiter.process(quiet.block());
        QCOMPARE(quiet.left, original.left); // fully released
        QVERIFY(!limiter.takeActivity());
    }

    void switchedOffPassesLoudSound()
    {
        SafetyLimiter limiter;
        limiter.setSampleRate(kRate);
        limiter.setEnabled(false);
        Buffer loud = sine(2.0, 4800);
        limiter.process(loud.block());
        QVERIFY(peakOf(loud) > 1.9F);
    }

    void garbageFromAPluginIsSilenced()
    {
        SafetyLimiter limiter;
        limiter.setSampleRate(kRate);
        limiter.setEnabled(false); // even when switched off
        Buffer bad = sine(0.5, 16);
        bad.left[3] = std::numeric_limits<float>::quiet_NaN();
        bad.right[7] = std::numeric_limits<float>::infinity();
        limiter.process(bad.block());
        for (std::size_t i = 0; i < bad.left.size(); ++i) {
            QVERIFY(std::isfinite(bad.left[i]) && std::isfinite(bad.right[i]));
        }
        QCOMPARE(bad.left[3], 0.0F);
        QVERIFY(limiter.takeActivity());
    }
};

QTEST_GUILESS_MAIN(TestSafetyLimiter)
#include "tst_safety_limiter.moc"
