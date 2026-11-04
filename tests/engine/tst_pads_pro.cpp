// Pads Pro, measured: every pad sounds, they are equally loud, they bloom
// in and hold steady whatever the touch, the shimmer puts energy an octave up
// and never runs away, and Motion really moves them. With GIGCHAIN_PADS_RENDER
// set to a folder, every pad is rendered there to listen to.
#include "PadsPro.h"
#include "VideoSounds.h"

#include <QDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <numeric>
#include <vector>

using namespace gigchain::engine;
using namespace gigchain::engine::pads;
using namespace Qt::StringLiterals;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

MidiEvent noteOn(int key, int velocity = 100) { return {.status = 0x90, .data1 = static_cast<uint8_t>(key), .data2 = static_cast<uint8_t>(velocity)}; }
MidiEvent noteOff(int key) { return {.status = 0x80, .data1 = static_cast<uint8_t>(key), .data2 = 0}; }

// Plays `seconds`, the notes pressed at the start (and let go at `releaseAt`, if not negative).
struct Take
{
    std::vector<float> left, right;
};
Take play(Synth& synth, const std::vector<int>& keys, double seconds, double releaseAt = -1.0, int velocity = 100)
{
    Take t;
    const auto total = static_cast<int64_t>(seconds * kRate);
    std::vector<float> l(kBlock), r(kBlock);
    const auto releaseSample = releaseAt < 0.0 ? int64_t{-1} : static_cast<int64_t>(releaseAt * kRate);
    for (int64_t at = 0; at < total; at += kBlock) {
        std::vector<MidiEvent> events;
        if (at == 0) {
            std::ranges::transform(keys, std::back_inserter(events), [velocity](int k) { return noteOn(k, velocity); });
        }
        if (releaseSample >= at && releaseSample < at + kBlock) {
            for (const int k : keys) {
                MidiEvent off = noteOff(k);
                off.sampleOffset = static_cast<int32_t>(releaseSample - at);
                events.push_back(off);
            }
        }
        synth.process(events, AudioBlock{.left = l.data(), .right = r.data(), .frames = kBlock});
        t.left.insert(t.left.end(), l.begin(), l.end());
        t.right.insert(t.right.end(), r.begin(), r.end());
    }
    return t;
}

// The level (dB, both ears) from `from` to `to` seconds.
double rmsDb(const Take& t, double from, double to)
{
    const auto a = static_cast<std::size_t>(from * kRate);
    const auto b = std::min(static_cast<std::size_t>(to * kRate), t.left.size());
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i) sum += 0.5 * (t.left.at(i) * t.left.at(i) + t.right.at(i) * t.right.at(i));
    return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<std::size_t>(b - a, 1)), 1e-20));
}

// How much of `hz` is in the left channel from `from` to `to` (Goertzel), dB.
double toneDb(const Take& t, double hz, double from, double to)
{
    const auto a = static_cast<std::size_t>(from * kRate);
    const auto b = std::min(static_cast<std::size_t>(to * kRate), t.left.size());
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = a; i < b; ++i) {
        const double s0 = t.left.at(i) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0 * std::log10(std::max(power / static_cast<double>((b - a) * (b - a)), 1e-30));
}

std::unique_ptr<Synth> padOf(int type)
{
    auto synth = std::make_unique<Synth>();
    synth->prepare(kRate, kBlock);
    synth->setParam(paramId(Param::Type), typeValue(type));
    return synth;
}

// C: root, fifth, octave (what Sustain Pads holds).
std::vector<int> drone() { return {48, 55, 60}; }

} // namespace

class TestPadsPro : public QObject
{
    Q_OBJECT

private slots:
    void thereAreEightPadsWithStableNames()
    {
        QCOMPARE(padTypeCount(), 8);
        const QStringList names{u"Warm"_s, u"Lush"_s, u"Dark"_s, u"Air"_s, u"Shimmer"_s, u"Choir"_s, u"Strings"_s, u"Organ"_s};
        for (int i = 0; i < padTypeCount(); ++i) {
            QCOMPARE(QString::fromLatin1(padType(i).name), names.at(i));
            QCOMPARE(typeIndex(typeValue(i)), i);
        }
        // Saved settings are keyed by name: these never change.
        QCOMPARE(paramKey(paramId(Param::Tone)), u"tone"_s);
        QCOMPARE(paramKey(paramId(Param::Shimmer)), u"shimmer"_s);
    }

    // Every pad sounds, and they are equally loud (within 1.5 dB of their
    // average): switching pads never jumps in level.
    void theTypesAreEquallyLoud()
    {
        std::vector<double> levels;
        QStringList report;
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            const Take t = play(*synth, drone(), 9.0);
            const double db = rmsDb(t, 5.0, 9.0);
            levels.push_back(db);
            report << u"%1 %2 dB"_s.arg(QString::fromLatin1(padType(i).name)).arg(db, 0, 'f', 1);
            QVERIFY2(db > -40.0, qPrintable(u"%1 is silent: %2 dB"_s.arg(QString::fromLatin1(padType(i).name)).arg(db)));
        }
        qInfo().noquote() << report.join(u", "_s);
        const double average = std::accumulate(levels.begin(), levels.end(), 0.0) / static_cast<double>(levels.size());
        for (std::size_t i = 0; i < levels.size(); ++i) {
            QVERIFY2(std::abs(levels.at(i) - average) <= 1.5,
                     qPrintable(u"%1 is %2 dB from the average (%3)"_s.arg(QString::fromLatin1(padType(static_cast<int>(i)).name))
                                    .arg(levels.at(i) - average, 0, 'f', 1)
                                    .arg(report.join(u", "_s))));
        }
    }

    // A pad blooms in (quiet in its first tenth of a second, full by its
    // attack), and never clips however many notes.
    void padsBloomInAndNeverClip()
    {
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            const Take t = play(*synth, {48, 52, 55, 60, 64, 67, 72, 76}, 6.0);
            const double first = rmsDb(t, 0.0, 0.1);
            const double full = rmsDb(t, 4.0, 6.0);
            QVERIFY2(full - first > 10.0, qPrintable(u"%1: first 0.1 s %2 dB, full %3 dB"_s.arg(QString::fromLatin1(padType(i).name)).arg(first).arg(full)));
            float peak = 0.0F;
            for (std::size_t s = 0; s < t.left.size(); ++s) peak = std::max({peak, std::abs(t.left.at(s)), std::abs(t.right.at(s))});
            QVERIFY2(peak < 1.0F, qPrintable(u"%1 peaks at %2"_s.arg(QString::fromLatin1(padType(i).name)).arg(peak)));
        }
    }

    // Worship pads sit steady under the piano: playing softly or hard is the
    // same level (unless Velocity is turned up).
    void touchDoesNotChangeTheLevelByDefault()
    {
        auto soft = padOf(1);
        auto hard = padOf(1);
        const double softDb = rmsDb(play(*soft, drone(), 5.0, -1.0, 25), 3.0, 5.0);
        const double hardDb = rmsDb(play(*hard, drone(), 5.0, -1.0, 127), 3.0, 5.0);
        QVERIFY2(std::abs(softDb - hardDb) < 0.5, qPrintable(u"soft %1 dB, hard %2 dB"_s.arg(softDb).arg(hardDb)));
        auto touchy = padOf(1);
        touchy->setParam(paramId(Param::Velocity), 1.0);
        const double touchySoft = rmsDb(play(*touchy, drone(), 5.0, -1.0, 25), 3.0, 5.0);
        QVERIFY2(hardDb - touchySoft > 6.0, qPrintable(u"with Velocity up, soft is %1 dB, hard %2 dB"_s.arg(touchySoft).arg(hardDb)));
    }

    // Let go: it rings on (the release, then the reverb), then falls silent.
    void letGoItRingsOnThenStops()
    {
        auto synth = padOf(0);
        const Take t = play(*synth, drone(), 26.0, 4.0);
        const double held = rmsDb(t, 3.0, 4.0);
        QVERIFY2(rmsDb(t, 4.5, 5.0) > held - 12.0, "it should ring on just after the keys are let go");
        QVERIFY2(rmsDb(t, 24.0, 26.0) < held - 50.0, qPrintable(u"still %1 dB 20 s later"_s.arg(rmsDb(t, 24.0, 26.0))));
        QVERIFY(!synth->holdsNotes());
    }

    // The shimmer: energy an octave (and two) above the note, growing with
    // the knob; none without it.
    void theShimmerClimbsAnOctave()
    {
        const auto octaveUp = [](double shimmer) {
            auto synth = padOf(4); // Shimmer
            synth->setParam(paramId(Param::Shimmer), shimmer);
            synth->setParam(paramId(Param::Reverb), 0.6);
            const Take t = play(*synth, {57}, 10.0, 3.0); // A3 (220 Hz) for 3 s, then the tail
            return toneDb(t, 880.0, 4.0, 10.0) - toneDb(t, 220.0, 4.0, 10.0); // two octaves up, relative to the note
        };
        const double none = octaveUp(0.0);
        const double full = octaveUp(1.0);
        qInfo() << "two octaves up, in the tail: no shimmer" << none << "dB, full" << full << "dB (relative to the note)";
        QVERIFY2(full - none > 6.0, qPrintable(u"shimmer adds only %1 dB two octaves up"_s.arg(full - none)));
    }

    // At its most (shimmer and reverb full, a chord held a minute) it stays
    // bounded: the tail does not keep growing.
    void theShimmerNeverRunsAway()
    {
        auto synth = padOf(4);
        synth->setParam(paramId(Param::Shimmer), 1.0);
        synth->setParam(paramId(Param::Reverb), 1.0);
        const Take t = play(*synth, drone(), 60.0);
        const double middle = rmsDb(t, 20.0, 30.0);
        const double late = rmsDb(t, 50.0, 60.0);
        QVERIFY2(late < middle + 2.0, qPrintable(u"the tail grew from %1 to %2 dB"_s.arg(middle).arg(late)));
        const float peak = std::accumulate(t.left.begin(), t.left.end(), 0.0F, [](float most, float s) { return std::max(most, std::abs(s)); });
        QVERIFY(peak < 1.0F);
    }

    // Motion: the slow movement (the filter breathing, the drift) is there,
    // and Motion at 0 stills it. Measured as brightness: the share of the
    // sound above 1.5 kHz, a second at a time (one frequency alone swings
    // anyway as the detuned saws beat against each other).
    void motionMovesThePad()
    {
        const auto movement = [](double motion) {
            auto synth = padOf(1); // Lush: its filter breathes at 0.1 Hz
            synth->setParam(paramId(Param::Motion), motion);
            synth->setParam(paramId(Param::Reverb), 0.0);
            const Take t = play(*synth, drone(), 24.0);
            sawstack::Svf high;
            high.set(1500.0, 0.7071, kRate);
            std::vector<float> top(t.left.size());
            for (std::size_t i = 0; i < t.left.size(); ++i) top.at(i) = high.highPass(t.left.at(i));
            double low = 1000.0;
            double most = -1000.0;
            for (int second = 4; second < 24; ++second) {
                const auto at = static_cast<double>(second);
                const auto a = static_cast<std::size_t>(at * kRate);
                const auto b = static_cast<std::size_t>((at + 1.0) * kRate);
                double all = 0.0;
                double bright = 0.0;
                for (std::size_t i = a; i < b; ++i) {
                    all += t.left.at(i) * t.left.at(i);
                    bright += top.at(i) * top.at(i);
                }
                const double share = 10.0 * std::log10(std::max(bright, 1e-20) / std::max(all, 1e-20));
                low = std::min(low, share);
                most = std::max(most, share);
            }
            return most - low;
        };
        const double still = movement(0.0);
        const double moving = movement(1.0);
        qInfo() << "brightness swing: still" << still << "dB, moving" << moving << "dB";
        QVERIFY2(moving > still + 1.5, qPrintable(u"Motion swings the brightness %1 dB, still %2 dB"_s.arg(moving).arg(still)));
    }

    // A new pad brings its own knobs, as a preset does; Level is the player's.
    void aNewPadBringsItsKnobs()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        synth.setParam(paramId(Param::Level), 0.9);
        synth.setParam(paramId(Param::Type), typeValue(4)); // Shimmer
        QVERIFY(std::abs(synth.param(paramId(Param::Shimmer)) - padType(4).shimmer) < 1e-6); // (kept as a float)
        QVERIFY(std::abs(synth.param(paramId(Param::Attack)) - Ranges::fromSeconds(padType(4).attack)) < 1e-6);
        QVERIFY(std::abs(synth.param(paramId(Param::Level)) - 0.9) < 1e-6);
    }

    // For listening: every pad, the drone in C then a chord, written to GIGCHAIN_PADS_RENDER.
    void renderEveryPadForListening()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_PADS_RENDER");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_PADS_RENDER to a folder to render every pad");
        QVERIFY(QDir().mkpath(folder));
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            Take t = play(*synth, drone(), 10.0, 8.0);
            const Take chord = play(*synth, {45, 52, 57, 60, 64}, 12.0, 8.0); // A minor, then the tail
            t.left.insert(t.left.end(), chord.left.begin(), chord.left.end());
            t.right.insert(t.right.end(), chord.right.begin(), chord.right.end());
            std::vector<float> both;
            both.reserve(t.left.size() * 2);
            for (std::size_t s = 0; s < t.left.size(); ++s) {
                both.push_back(t.left.at(s));
                both.push_back(t.right.at(s));
            }
            const QString path = u"%1/pads-pro-%2-%3.wav"_s.arg(folder).arg(i + 1).arg(QString::fromLatin1(padType(i).key));
            QVERIFY2(video::writeWav(path, both), qPrintable(path));
            qInfo().noquote() << path;
        }
    }
};

QTEST_GUILESS_MAIN(TestPadsPro)
#include "tst_pads_pro.moc"
