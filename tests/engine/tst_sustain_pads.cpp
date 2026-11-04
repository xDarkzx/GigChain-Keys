// Sustain Pads (Sunday Keys' Tonic): a Pads Pro pad held on one key under the
// song. It is silent until switched on, fades in and out over the fade time,
// moves from key to key without a gap or a bump, holds root, fifth and octave
// in a comfortable range, and never clips.
#include "SustainPads.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <vector>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Take
{
    std::vector<float> left, right;
};

// Runs the pads for `seconds` (added onto silence, as the engine adds them to the mix).
void run(SustainPads& pads, Take& t, double seconds, float gain = 1.0F)
{
    std::vector<float> l(kBlock), r(kBlock);
    const auto total = static_cast<int64_t>(seconds * kRate);
    for (int64_t at = 0; at < total; at += kBlock) {
        std::ranges::fill(l, 0.0F);
        std::ranges::fill(r, 0.0F);
        pads.process(AudioBlock{.left = l.data(), .right = r.data(), .frames = kBlock}, gain);
        t.left.insert(t.left.end(), l.begin(), l.end());
        t.right.insert(t.right.end(), r.begin(), r.end());
    }
}

double rmsDb(const Take& t, double from, double to)
{
    const auto a = static_cast<std::size_t>(from * kRate);
    const auto b = std::min(static_cast<std::size_t>(to * kRate), t.left.size());
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i) sum += 0.5 * (t.left.at(i) * t.left.at(i) + t.right.at(i) * t.right.at(i));
    return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<std::size_t>(b - a, 1)), 1e-20));
}

float peakOf(const Take& t)
{
    float peak = 0.0F;
    for (std::size_t i = 0; i < t.left.size(); ++i) peak = std::max({peak, std::abs(t.left.at(i)), std::abs(t.right.at(i))});
    return peak;
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

double hzOf(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

// A VST instrument stand-in: it records every note it is sent (with the
// block it came in), and plays a 1 kHz sine while it holds any note.
class FakePadInstrument final : public INode
{
public:
    struct Received
    {
        int block = 0;
        bool on = false;
        int note = 0;
    };
    std::vector<Received> received;
    std::vector<int> held;
    int blocks = 0;
    double phase = 0.0;
    int preparedBlock = 0;
    int burstBlock = -1; // >= 0: it sounds in that block only
    bool mute = false;   // it plays nothing (a sound that has ended, a plugin loading)
    // A new sound chosen in it, as many synths do: the notes it held stop.
    void dropAllNotes() { held.clear(); }
    [[nodiscard]] int pressesOf(int note) const
    {
        return static_cast<int>(std::ranges::count_if(received, [note](const Received& r) { return r.on && r.note == note; }));
    }

    gigchain::core::Result<void> prepare(double /*sampleRate*/, int maxBlock) override
    {
        preparedBlock = maxBlock;
        return {};
    }
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& /*time*/) override
    {
        for (const MidiEvent& e : events) {
            const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
            received.push_back({.block = blocks, .on = on, .note = e.data1});
            if (on) held.push_back(e.data1);
            else std::erase(held, static_cast<int>(e.data1));
        }
        // (A burst: only that block sounds, the rest is silent. Mute: never a sound.)
        const bool sounds = !held.empty() && !mute && (burstBlock < 0 || blocks == burstBlock);
        for (int i = 0; i < io.frames; ++i) {
            const float s = sounds ? 0.3F * static_cast<float>(std::sin(phase)) : 0.0F;
            phase += 2.0 * std::numbers::pi * 1000.0 / kRate;
            *std::next(io.left, i) = s;
            *std::next(io.right, i) = s;
        }
        ++blocks;
    }
    [[nodiscard]] std::vector<int> heldSorted() const
    {
        std::vector<int> h = held;
        std::ranges::sort(h);
        return h;
    }
};

SustainPadsSettings on(int key, double fadeSeconds = 2.0)
{
    SustainPadsSettings s;
    s.active = true;
    s.key = key;
    s.fadeSeconds = fadeSeconds;
    s.volumeDb = 0.0;
    return s;
}

} // namespace

class TestSustainPads : public QObject
{
    Q_OBJECT

private slots:
    // Root, fifth and octave, the root between G2 and F#3 (never muddy,
    // never thin), an octave up or down with the octave switch.
    void theChordIsRootFifthOctaveInAComfortableRange()
    {
        QCOMPARE(SustainPads::chord(0, 0), (std::array<int, 3>{48, 55, 60}));  // C3
        QCOMPARE(SustainPads::chord(7, 0), (std::array<int, 3>{43, 50, 55}));  // G2
        QCOMPARE(SustainPads::chord(6, 0), (std::array<int, 3>{54, 61, 66}));  // F#3
        QCOMPARE(SustainPads::chord(0, 1), (std::array<int, 3>{60, 67, 72}));
        QCOMPARE(SustainPads::chord(0, -1), (std::array<int, 3>{36, 43, 48}));
        // Out of range values are kept in range.
        QCOMPARE(SustainPads::chord(14, 5), SustainPads::chord(2, 1));
    }

    // The screen's names for the pads are Pads Pro's, in its order.
    void thePresetsArePadsPros()
    {
        QCOMPARE(static_cast<int>(kSustainPadPresets.size()), pads::padTypeCount());
        for (int i = 0; i < pads::padTypeCount(); ++i) {
            QCOMPARE(QString::fromLatin1(kSustainPadPresets.at(static_cast<std::size_t>(i))), QString::fromLatin1(pads::padType(i).name));
        }
    }

    void silentUntilSwitchedOn()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        SustainPadsSettings s = on(0);
        s.active = false;
        pads.set(s);
        Take t;
        run(pads, t, 2.0);
        QCOMPARE(peakOf(t), 0.0F);
    }

    // Switched on, it comes in over the fade time (no jump): quieter half
    // way, steady once faded in.
    void itFadesInOverTheFadeTime()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        pads.set(on(0, 4.0));
        Take t;
        run(pads, t, 10.0);
        const double early = rmsDb(t, 0.0, 0.5);
        const double half = rmsDb(t, 1.8, 2.2);
        const double full = rmsDb(t, 7.0, 10.0);
        qInfo().noquote() << u"fading in: first half second %1 dB, at 2 s %2 dB, faded in %3 dB"_s.arg(early, 0, 'f', 1).arg(half, 0, 'f', 1).arg(full, 0, 'f', 1);
        QVERIFY(full > -30.0);       // it sounds
        QVERIFY(early < full - 15.0); // it does not jump in
        QVERIFY(half < full - 1.0);   // still coming in half way
        QVERIFY(half > early + 6.0);
        QVERIFY(peakOf(t) < 1.0F);
    }

    // Switched off, it fades out over the fade time, to silence.
    void itFadesOutToSilence()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        pads.set(on(0, 2.0));
        Take t;
        run(pads, t, 6.0);
        SustainPadsSettings off = on(0, 2.0);
        off.active = false;
        pads.set(off);
        run(pads, t, 4.0);
        (void)pads.level(); // the meter from here on
        run(pads, t, 1.0);
        const double before = rmsDb(t, 4.0, 6.0);
        const double during = rmsDb(t, 6.9, 7.1);
        const double after = rmsDb(t, 8.5, 10.0);
        qInfo().noquote() << u"fading out: before %1 dB, half way %2 dB, after %3 dB"_s.arg(before, 0, 'f', 1).arg(during, 0, 'f', 1).arg(after, 0, 'f', 1);
        QVERIFY(during < before - 1.0);
        QVERIFY(during > before - 20.0); // not cut off
        QVERIFY(after < -100.0);         // silent
        QVERIFY(pads.level().peak < 1e-5F);
    }

    // A new key fades over from the old one: never a gap (no dip deeper than
    // 4 dB) or a bump (no more than 2 dB louder), and the new chord is there.
    void aNewKeyCrossfadesWithoutAGap()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        pads.set(on(0, 2.0)); // C
        Take t;
        run(pads, t, 8.0);
        pads.set(on(7, 2.0)); // to G
        run(pads, t, 8.0);
        const double steady = rmsDb(t, 5.0, 8.0);
        double lowest = 0.0;
        double highest = -200.0;
        for (int quarter = 32; quarter < 48; ++quarter) { // 8 s to 12 s, a quarter second at a time
            const double at = quarter * 0.25;
            const double l = rmsDb(t, at, at + 0.25);
            lowest = std::min(lowest, l - steady);
            highest = std::max(highest, l - steady);
        }
        const double gBefore = toneDb(t, hzOf(50), 5.0, 8.0);
        const double gAfter = toneDb(t, hzOf(50), 13.0, 16.0);
        qInfo().noquote() << u"key change: dips %1 dB, rises %2 dB; D3 (the new fifth) before %3 dB, after %4 dB"_s.arg(lowest, 0, 'f', 1).arg(highest, 0, 'f', 1).arg(gBefore, 0, 'f', 1).arg(gAfter, 0, 'f', 1);
        QVERIFY(lowest > -4.0);
        QVERIFY(highest < 2.0);
        QVERIFY(gAfter > gBefore + 15.0); // G major is playing now
        QVERIFY2(peakOf(t) < 1.0F, qPrintable(u"peaks at %1"_s.arg(peakOf(t))));
    }

    // A new pad fades over too (same way as a new key).
    void aNewPadCrossfadesToo()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        pads.set(on(0, 2.0));
        Take t;
        run(pads, t, 8.0);
        SustainPadsSettings organ = on(0, 2.0);
        organ.preset = 7;
        pads.set(organ);
        run(pads, t, 8.0);
        const double steady = rmsDb(t, 5.0, 8.0);
        double lowest = 0.0;
        for (int quarter = 32; quarter < 48; ++quarter) lowest = std::min(lowest, rmsDb(t, quarter * 0.25, (quarter + 1) * 0.25) - steady);
        qInfo().noquote() << u"pad change: dips %1 dB"_s.arg(lowest, 0, 'f', 1);
        QVERIFY(lowest > -4.0);
    }

    // The volume is in dB, and the gain given (the master fader) scales it.
    void volumeAndTheMasterScaleIt()
    {
        const auto levelAt = [](double volumeDb, float gain) {
            SustainPads pads;
            pads.prepare(kRate, kBlock);
            SustainPadsSettings s = on(0, 0.5);
            s.volumeDb = volumeDb;
            pads.set(s);
            Take t;
            run(pads, t, 6.0, gain);
            return rmsDb(t, 4.0, 6.0);
        };
        const double unity = levelAt(0.0, 1.0F);
        QVERIFY(std::abs(levelAt(-12.0, 1.0F) - (unity - 12.0)) < 0.5);
        QVERIFY(std::abs(levelAt(0.0, 0.5F) - (unity - 6.02)) < 0.5);
        QVERIFY(levelAt(-60.0, 1.0F) < unity - 55.0);
    }

    // The meter shows what it plays.
    void theMeterReadsItsLevel()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        pads.set(on(0, 0.5));
        Take t;
        run(pads, t, 4.0);
        const LevelReading reading = pads.level();
        QVERIFY(reading.peak > 0.01F);
        QVERIFY(reading.rms > 0.001F);
        QVERIFY(reading.peak < 1.0F);
    }

    // Every pad, full shimmer and bright, an octave up, at full volume: never clips.
    void neverClips()
    {
        for (int preset = 0; preset < 8; ++preset) {
            SustainPads pads;
            pads.prepare(kRate, kBlock);
            SustainPadsSettings s = on(5, 0.5);
            s.preset = preset;
            s.octave = 1;
            s.shimmer = 1.0;
            s.tone = 1.0;
            s.volumeDb = 0.0;
            pads.set(s);
            Take t;
            run(pads, t, 8.0);
            QVERIFY2(peakOf(t) <= 1.0F, qPrintable(u"pad %1 peaks at %2"_s.arg(preset).arg(peakOf(t))));
        }
    }

    // With a VST instrument loaded, it holds the chord instead of Pads Pro:
    // root, fifth and octave sent to it, its sound faded in over the fade
    // time, and nothing of Pads Pro's.
    void aVstInstrumentHoldsTheChordInsteadOfPadsPro()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(7, 2.0)); // G
        Take t;
        run(pads, t, 6.0);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{43, 50, 55}));
        const double early = rmsDb(t, 0.0, 0.25);
        const double full = rmsDb(t, 4.0, 6.0);
        const double sine = toneDb(t, 1000.0, 4.0, 6.0);
        qInfo().noquote() << u"a VST: first quarter second %1 dB, faded in %2 dB, its 1 kHz %3 dB"_s.arg(early, 0, 'f', 1).arg(full, 0, 'f', 1).arg(sine, 0, 'f', 1);
        QVERIFY(full > -30.0);
        QVERIFY(early < full - 15.0); // faded in, not jumped in
        // All of it is the VST's sine (a sine's power: its amplitude squared over two, here in one ear's bin).
        QVERIFY2(std::abs(full - (sine + 3.01)) < 1.0, "something besides the VST plays");
    }

    // A new key on a VST: the new chord is pressed before the old one is let
    // go (in the same block), so the instrument's own release carries it
    // over: no gap.
    void aNewKeyOnAVstPressesTheNewChordBeforeLettingGo()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 1.0)); // C
        Take t;
        run(pads, t, 3.0);
        const std::size_t before = vst->received.size();
        pads.set(on(2, 1.0)); // D
        run(pads, t, 1.0);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{50, 57, 62}));
        const std::vector<FakePadInstrument::Received> change(vst->received.begin() + static_cast<std::ptrdiff_t>(before), vst->received.end());
        QCOMPARE(static_cast<int>(change.size()), 6);
        for (std::size_t i = 0; i < change.size(); ++i) {
            QCOMPARE(change.at(i).block, change.front().block); // all at once
            QCOMPARE(change.at(i).on, i < 3);                   // presses first
        }
        // Never a gap: the level stays up through the change.
        const double steady = rmsDb(t, 2.0, 3.0);
        for (int quarter = 12; quarter < 16; ++quarter) QVERIFY(rmsDb(t, quarter * 0.25, (quarter + 1) * 0.25) > steady - 3.0);

        // C to F share a C (60): it stays held, never pressed again and let go.
        pads.set(on(0, 1.0));
        run(pads, t, 0.5);
        pads.set(on(5, 1.0));
        run(pads, t, 0.5);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{53, 60, 65}));
    }

    // Switched off, a VST fades out over the fade time, and its notes are
    // let go only then (its own release would cut the fade short).
    void aVstFadesOutThenLetsGo()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 2.0));
        Take t;
        run(pads, t, 4.0);
        SustainPadsSettings off = on(0, 2.0);
        off.active = false;
        pads.set(off);
        run(pads, t, 1.0);
        QCOMPARE(vst->held.size(), std::size_t{3}); // half way: still held
        QVERIFY(rmsDb(t, 4.9, 5.0) < rmsDb(t, 3.0, 4.0) - 1.0);
        run(pads, t, 2.0);
        QVERIFY(vst->held.empty()); // faded out: let go
        QVERIFY(rmsDb(t, 6.2, 7.0) < -100.0);
    }

    // The VST taken out while it plays: Pads Pro takes over, faded in.
    void withoutTheVstPadsProPlaysAgain()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 0.5));
        Take t;
        run(pads, t, 2.0);
        pads.setInstrument(nullptr);
        pads.collectGarbage();
        run(pads, t, 4.0);
        const double padsPro = rmsDb(t, 4.0, 6.0);
        const double sine = toneDb(t, 1000.0, 4.0, 6.0);
        qInfo().noquote() << u"after the VST: %1 dB, its sine %2 dB"_s.arg(padsPro, 0, 'f', 1).arg(sine, 0, 'f', 1);
        QVERIFY(padsPro > -30.0);
        QVERIFY(sine < padsPro - 20.0); // not the VST
    }

    // A new sound chosen in the VST while it plays: the chord is pressed
    // again at once (let go, then pressed, in one block), so the new sound
    // plays (a synth plays a new sound on new notes only, or stops the old).
    void aNewSoundInTheVstIsPlayedAtOnce()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 0.5));
        Take t;
        run(pads, t, 1.0);
        QCOMPARE(vst->pressesOf(48), 1);
        const std::size_t before = vst->received.size();
        vst->dropAllNotes(); // a new sound: its notes stopped
        pads.restrike();
        run(pads, t, 0.1);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{48, 55, 60}));
        const std::vector<FakePadInstrument::Received> change(vst->received.begin() + static_cast<std::ptrdiff_t>(before), vst->received.end());
        QCOMPARE(static_cast<int>(change.size()), 6);
        for (std::size_t i = 0; i < change.size(); ++i) {
            QCOMPARE(change.at(i).block, change.front().block);
            QCOMPARE(change.at(i).on, i >= 3); // let go first, then pressed
        }
        QVERIFY(rmsDb(t, 1.05, 1.1) > -40.0); // sounding again straight away
        // Off: nothing is pressed again.
        SustainPadsSettings off = on(0, 0.5);
        off.active = false;
        pads.set(off);
        run(pads, t, 1.0);
        const int presses = vst->pressesOf(48);
        pads.restrike();
        run(pads, t, 0.5);
        QCOMPARE(vst->pressesOf(48), presses);
    }

    // Switched off and on again before it has faded out: the chord is
    // pressed again (the VST may have dropped it meanwhile, a new sound
    // chosen in it): never back up on notes that may not be sounding.
    void onAgainWhileFadingOutPressesTheChordAgain()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 4.0));
        Take t;
        run(pads, t, 5.0);
        vst->dropAllNotes(); // (a new sound chosen; the plugin said nothing)
        SustainPadsSettings off = on(0, 4.0);
        off.active = false;
        pads.set(off);
        run(pads, t, 0.5); // half a second into a four-second fade
        pads.set(on(0, 4.0));
        run(pads, t, 0.1);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{48, 55, 60}));
        QCOMPARE(vst->pressesOf(48), 2);
    }

    // A VST gone silent while it should sound (a sound changed without a
    // word, a plugin dropping its notes) is pressed again, within a second;
    // one that stays silent (a sound that has died away) at most every three.
    void aVstGoneSilentIsPressedAgain()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 0.2));
        Take t;
        run(pads, t, 1.0);
        vst->dropAllNotes();
        run(pads, t, 1.2);
        QCOMPARE(vst->heldSorted(), (std::vector<int>{48, 55, 60}));
        QCOMPARE(vst->pressesOf(48), 2);
        QVERIFY(rmsDb(t, 2.0, 2.2) > -40.0);

        vst->mute = true; // silent whatever it is sent
        const int before = vst->pressesOf(48);
        run(pads, t, 10.0);
        const int presses = vst->pressesOf(48) - before;
        QVERIFY2(presses >= 2 && presses <= 4, qPrintable(QString::number(presses)));
    }

    // Panic: silent at once, no echo or hall ringing on, the VST's notes
    // let go; Pads Pro or a VST. Switched on again, it plays.
    void aPanicSilencesItAtOnce()
    {
        for (const bool vstPlays : {false, true}) {
            SustainPads pads;
            pads.prepare(kRate, kBlock);
            auto vst = std::make_shared<FakePadInstrument>();
            if (vstPlays) pads.setInstrument(vst);
            SustainPadsSettings s = on(0, 2.0);
            pads.set(s);
            Take t;
            run(pads, t, 4.0);
            QVERIFY(rmsDb(t, 3.0, 4.0) > -40.0);
            pads.cut();
            run(pads, t, 2.0);
            const double after = rmsDb(t, 4.0 + 2.0 * kBlock / kRate, 6.0); // from the block after
            qInfo().noquote() << (vstPlays ? u"a VST"_s : u"Pads Pro"_s) << u"after a panic: %1 dB"_s.arg(after, 0, 'f', 1);
            QVERIFY2(after < -100.0, qPrintable(QString::number(after)));
            if (vstPlays) QVERIFY(vst->held.empty());
            pads.set(s); // on again
            run(pads, t, 3.0);
            QVERIFY(rmsDb(t, 8.0, 9.0) > -40.0);
        }
    }

    // Its strip's effects play on the pad (Pads Pro or a VST), in order,
    // before its fader; taken out, the pad is as it was.
    void itsStripsEffectsPlayAfterThePadInOrder()
    {
        // A stand-in effect: a gain, and the loudest sample it was given.
        class Gain final : public INode
        {
        public:
            explicit Gain(float gain) : m_gain(gain) {}
            float loudestIn = 0.0F;
            gigchain::core::Result<void> prepare(double, int) override { return {}; }
            void process(std::span<const MidiEvent>, AudioBlock io, const TimeInfo&) override
            {
                for (int i = 0; i < io.frames; ++i) {
                    float& l = *std::next(io.left, i);
                    float& r = *std::next(io.right, i);
                    loudestIn = std::max({loudestIn, std::abs(l), std::abs(r)});
                    l *= m_gain;
                    r *= m_gain;
                }
            }

        private:
            float m_gain;
        };
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        auto vst = std::make_shared<FakePadInstrument>();
        pads.setInstrument(vst);
        pads.set(on(0, 0.2));
        Take t;
        run(pads, t, 2.0);
        const double dry = rmsDb(t, 1.0, 2.0);
        auto half = std::make_shared<Gain>(0.5F);
        auto after = std::make_shared<Gain>(1.0F);
        pads.setEffects({half, after});
        run(pads, t, 2.0);
        const double wet = rmsDb(t, 3.0, 4.0);
        qInfo().noquote() << u"dry %1 dB, through the strip's effects %2 dB"_s.arg(dry, 0, 'f', 1).arg(wet, 0, 'f', 1);
        QVERIFY(std::abs(wet - (dry - 6.02)) < 0.3); // halved
        QVERIFY(after->loudestIn > 0.0F && after->loudestIn <= 0.16F); // the second hears the first's (the 0.3 sine, halved)
        pads.setEffects({});
        pads.collectGarbage();
        run(pads, t, 2.0);
        QVERIFY(std::abs(rmsDb(t, 5.0, 6.0) - dry) < 0.3);
    }

    // Its strip's mute: silent, the volume kept.
    void itsMuteSilencesIt()
    {
        SustainPads pads;
        pads.prepare(kRate, kBlock);
        SustainPadsSettings s = on(0, 0.2);
        s.mute = true;
        pads.set(s);
        Take t;
        run(pads, t, 2.0);
        QVERIFY(rmsDb(t, 1.0, 2.0) < -100.0);
        s.mute = false;
        pads.set(s);
        run(pads, t, 1.0);
        QVERIFY(rmsDb(t, 2.5, 3.0) > -40.0);
    }

    // Not prepared (no device yet): it plays nothing rather than touching
    // anything it has not set up.
    void unpreparedItPlaysNothing()
    {
        SustainPads pads;
        pads.set(on(0, 0.5));
        Take t;
        run(pads, t, 1.0);
        QCOMPARE(peakOf(t), 0.0F);
    }
};

QTEST_MAIN(TestSustainPads)
#include "tst_sustain_pads.moc"
