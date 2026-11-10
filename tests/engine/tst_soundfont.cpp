// The SoundFont Player, measured against FL Studio's SoundFont Player: a
// .sf2 is read and its presets listed; every knob and slider fully down plays
// the SoundFont's own value and, turned up, sets what FL's sets (the volume
// envelope, the pitch LFO, the filter and its modulation); Reverb and Chorus
// switch the built-in effects; the options (quality, mode, velocity to
// cutoff) do what FL's do; and the file, the preset and every control come
// back as saved. The SoundFont is made for it (MakeSoundFont.h): one tone
// (480 Hz with a 12 kHz overtone), its attack 0.3 s and release 0.05 s, as a
// preset in bank 0 and a kit in bank 128.
#include "SoundFont.h"
#include "SoundFontNode.h"

#include "../common/MakeSoundFont.h"

#include "gigchain/engine/BuiltinReadings.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

using namespace gigchain::engine;
using namespace gigchain::engine::sf2;
using namespace Qt::StringLiterals;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kRoot = gigchain::testing::kSoundFontRoot; // the tone's key: played there, it is not transposed
using gigchain::testing::makeSoundFont;
using gigchain::testing::timecents;

// ---------------------------------------------------------------- playing it
MidiEvent noteOn(int key, int velocity = 127) { return {.status = 0x90, .data1 = static_cast<uint8_t>(key), .data2 = static_cast<uint8_t>(velocity)}; }
MidiEvent noteOff(int key) { return {.status = 0x80, .data1 = static_cast<uint8_t>(key), .data2 = 0}; }

struct Timed
{
    Timed(double when, MidiEvent what) : at(when), event(what) {}
    double at = 0.0; // seconds
    MidiEvent event;
};

// The control at the setting that gives `seconds` (the inverse of the conversions).
double vFor(double seconds, double range) { return std::log(seconds / 0.001) / std::log(range); }

double rms(const std::vector<float>& x, double from, double to)
{
    const auto a = static_cast<std::size_t>(std::clamp(from * kRate, 0.0, static_cast<double>(x.size())));
    const auto b = static_cast<std::size_t>(std::clamp(to * kRate, 0.0, static_cast<double>(x.size())));
    if (b <= a) return 0.0;
    const auto from_ = x.begin() + static_cast<std::ptrdiff_t>(a);
    const auto to_ = x.begin() + static_cast<std::ptrdiff_t>(b);
    const double sum = std::accumulate(from_, to_, 0.0, [](double s, float v) { return s + static_cast<double>(v) * v; });
    return std::sqrt(sum / static_cast<double>(b - a));
}
double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }
// The 12 kHz overtone's share: the energy of the difference between samples
// (which the 480 Hz tone barely has) over the signal's.
double brightness(const std::vector<float>& x, double from, double to)
{
    const auto a = static_cast<std::size_t>(from * kRate);
    const auto b = std::min(x.size(), static_cast<std::size_t>(to * kRate));
    double diff = 0.0;
    double all = 0.0;
    for (std::size_t i = a + 1; i < b; ++i) {
        const double d = static_cast<double>(x.at(i)) - x.at(i - 1);
        diff += d * d;
        all += static_cast<double>(x.at(i)) * x.at(i);
    }
    return all > 0.0 ? diff / all : 0.0;
}
// The pitch over time (Hz, every 40 ms), from the zero crossings of the tone
// with its overtone averaged away (4 samples: one period of 12 kHz).
std::vector<double> pitches(const std::vector<float>& x, double from, double to)
{
    std::vector<double> smooth(x.size(), 0.0);
    for (std::size_t i = 3; i < x.size(); ++i) smooth.at(i) = (x.at(i) + x.at(i - 1) + x.at(i - 2) + x.at(i - 3)) / 4.0;
    std::vector<double> out;
    const int windows = static_cast<int>(std::floor((to - from) / 0.04 + 1e-9));
    for (int w = 0; w < windows; ++w) {
        const double t = from + 0.04 * w;
        const auto a = static_cast<std::size_t>(t * kRate);
        const auto b = static_cast<std::size_t>((t + 0.04) * kRate);
        double first = -1.0;
        double last = -1.0;
        int crossings = 0;
        for (std::size_t i = a + 1; i < b && i < smooth.size(); ++i) {
            const double before = smooth.at(i - 1);
            const double now = smooth.at(i);
            if (before < 0.0 && now >= 0.0) {
                const double exact = static_cast<double>(i - 1) + before / (before - now);
                if (first < 0.0) first = exact;
                last = exact;
                ++crossings;
            }
        }
        if (crossings >= 3) out.push_back((crossings - 1) * kRate / (last - first));
    }
    return out;
}
double spread(const std::vector<double>& values)
{
    if (values.empty()) return 0.0;
    const auto [lo, hi] = std::ranges::minmax(values);
    return hi - lo;
}

class Rig
{
public:
    explicit Rig(const QString& path)
    {
        player.prepare(kRate);
        player.setFile(path);
        waitReady();
    }
    void waitReady()
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 10000) {
            player.service();
            if (player.fileState() == Player::FileState::Ready || player.fileState() == Player::FileState::Failed) break;
            QThread::msleep(5);
        }
        player.service(); // the synth built, and handed over
    }
    void set(Param p, double v)
    {
        player.setParam(paramId(p), v);
        player.service(); // (velocity to cutoff makes another synth)
    }
    // No reverb or chorus: the note alone, to measure.
    void dry()
    {
        set(Param::ReverbOn, 0.0);
        set(Param::ChorusOn, 0.0);
    }
    void choose(Mode m) { set(Param::Mode, stepValue(static_cast<int>(m), kModes)); }
    // Plays `seconds` with these events; the left channel.
    std::vector<float> play(std::vector<Timed> events, double seconds)
    {
        std::ranges::sort(events, [](const Timed& a, const Timed& b) { return a.at < b.at; });
        const auto total = static_cast<int>(seconds * kRate);
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(total));
        std::vector<float> l(kBlock), r(kBlock);
        std::size_t next = 0;
        for (int start = 0; start < total; start += kBlock) {
            std::vector<MidiEvent> block;
            while (next < events.size() && events.at(next).at * kRate < start + kBlock) {
                MidiEvent e = events.at(next).event;
                e.sampleOffset = std::max(0, static_cast<int>(events.at(next).at * kRate) - start);
                block.push_back(e);
                ++next;
            }
            player.process(block, {.left = l.data(), .right = r.data(), .frames = kBlock});
            out.insert(out.end(), l.begin(), l.end());
        }
        out.resize(static_cast<std::size_t>(total));
        return out;
    }
    // The first time the level reaches `fraction` of its loudest (5 ms windows).
    static double timeTo(const std::vector<float>& x, double fraction, double upTo)
    {
        const int windows = static_cast<int>(std::ceil(upTo / 0.005));
        double peak = 0.0;
        for (int w = 0; w < windows; ++w) peak = std::max(peak, rms(x, 0.005 * w, 0.005 * (w + 1)));
        for (int w = 0; w < windows; ++w) {
            if (rms(x, 0.005 * w, 0.005 * (w + 1)) >= fraction * peak) return 0.005 * w;
        }
        return upTo;
    }

    Player player;
};

} // namespace

class TestSoundFont : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_path = makeSoundFont(m_dir.path());
        QVERIFY(!m_path.isEmpty());
    }

    // ---- The file.
    void readsTheFileAndListsItsPresets()
    {
        Rig rig(m_path);
        QCOMPARE(rig.player.fileState(), Player::FileState::Ready);
        const std::vector<PresetInfo> presets = rig.player.presets();
        QCOMPARE(presets.size(), std::size_t{2});
        QCOMPARE(presets.at(0).name, u"Tone"_s);
        QCOMPARE(presets.at(0).bank, 0);
        QCOMPARE(presets.at(1).name, u"Kit"_s);
        QCOMPARE(presets.at(1).bank, 128);
        QCOMPARE(rig.player.chosen(), std::make_pair(0, 0)); // the first, as none was asked for
        QVERIFY(rig.player.takeMessages().isEmpty());
    }

    void aMissingFileSaysSo()
    {
        Rig rig(m_dir.path() + u"/nowhere.sf2"_s);
        QCOMPARE(rig.player.fileState(), Player::FileState::Failed);
        const QStringList said = rig.player.takeMessages();
        QCOMPARE(said.size(), 1);
        QVERIFY2(said.front().contains(u"not there"_s), qPrintable(said.front()));
        // And nothing plays (no crash, silence).
        const auto out = rig.play({{0.0, noteOn(kRoot)}}, 0.2);
        QCOMPARE(rms(out, 0.0, 0.2), 0.0);
    }

    void aFileThatIsNotASoundFontSaysSo()
    {
        const QString bad = m_dir.path() + u"/not a soundfont.sf2"_s;
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();
        Rig rig(bad);
        QCOMPARE(rig.player.fileState(), Player::FileState::Failed);
        const QStringList said = rig.player.takeMessages();
        QCOMPARE(said.size(), 1);
        QVERIFY2(said.front().contains(u"could not be read"_s), qPrintable(said.front()));
    }

    void playsAtAGoodLevelAndStops()
    {
        Rig rig(m_path);
        const auto out = rig.play({{0.0, noteOn(kRoot)}, {0.6, noteOff(kRoot)}}, 1.0);
        const double held = rms(out, 0.4, 0.6);
        const float peak = std::accumulate(out.begin(), out.end(), 0.0F, [](float p, float s) { return std::max(p, std::abs(s)); });
        qInfo("held %.1f dB, peak %.3f", db(held), static_cast<double>(peak));
        QVERIFY2(held > 0.03 && peak < 0.9F, "plays clearly, without clipping");
        // The release is 0.05 s: silent well before 0.9 s (only the reverb's tail is left: switched off here).
        Rig dry(m_path);
        dry.set(Param::ReverbOn, 0.0);
        dry.set(Param::ChorusOn, 0.0);
        const auto d = dry.play({{0.0, noteOn(kRoot)}, {0.6, noteOff(kRoot)}}, 1.0);
        QVERIFY(rms(d, 0.8, 1.0) < 1e-5);
        QVERIFY(!dry.player.holdsNotes());
    }

    // ---- The envelope set by the controls plays exactly as the same envelope
    // made in the SoundFont: from the note's first sample (a short attack
    // reaches full level before the decay, as FL's does).
    void theControlsEnvelopeIsTheSameAsTheSoundFontsOwn()
    {
        const auto levels = [](const std::vector<float>& x) {
            std::vector<double> out;
            out.reserve(20);
            for (int w = 0; w < 20; ++w) out.push_back(db(rms(x, 0.05 * w, 0.05 * (w + 1)))); // (1 s, 50 ms at a time)
            return out;
        };
        for (double attack : {0.002, 0.02, 0.1}) {
            // Made in the SoundFont: this attack, a 4 s decay to -30 dB.
            Rig made(makeSoundFont(m_dir.path(), u"made %1"_s.arg(attack), attack, {{36, timecents(4.0)}, {37, 300}}));
            made.dry();
            const auto want = levels(made.play({{0.0, noteOn(kRoot)}}, 1.0));
            // Set by the controls over the plain tone (its 0.3 s attack, no decay).
            Rig set(m_path);
            set.dry();
            set.set(Param::Attack, vFor(attack, 10000.0));
            set.set(Param::Decay, vFor(4.0, 20000.0));
            set.set(Param::Sustain, 0.5);
            const auto got = levels(set.play({{0.0, noteOn(kRoot)}}, 1.0));
            double worst = 0.0;
            for (std::size_t i = 0; i < want.size(); ++i) worst = std::max(worst, std::abs(want.at(i) - got.at(i)));
            qInfo("attack %.3f: the controls' envelope within %.2f dB of the SoundFont's own", attack, worst);
            QVERIFY(worst < 1.0);
        }
    }

    // ---- Envelope: each slider down is the SoundFont's; up sets FL's stage.
    void attackDownIsTheSoundFontsAndUpSetsIt()
    {
        Rig own(m_path);
        own.dry();
        const double ownRise = Rig::timeTo(own.play({{0.0, noteOn(kRoot)}}, 0.8), 0.9, 0.8);
        qInfo("the SoundFont's attack: 90%% at %.3f s (it says 0.3 s)", ownRise);
        QVERIFY(ownRise > 0.2 && ownRise < 0.35);

        Rig fast(m_path);
        fast.dry();
        fast.set(Param::Attack, vFor(0.02, 10000.0));
        QCOMPARE(attackSeconds(fast.player.param(paramId(Param::Attack))), 0.02);
        const double fastRise = Rig::timeTo(fast.play({{0.0, noteOn(kRoot)}}, 0.8), 0.9, 0.8);
        Rig slow(m_path);
        slow.dry();
        slow.set(Param::Attack, vFor(0.6, 10000.0));
        const double slowRise = Rig::timeTo(slow.play({{0.0, noteOn(kRoot)}}, 1.2), 0.9, 1.2);
        qInfo("attack 20 ms: 90%% at %.3f s; 600 ms: at %.3f s", fastRise, slowRise);
        QVERIFY(fastRise < 0.04);
        QVERIFY(slowRise > 0.45 && slowRise < 0.65);
    }

    void releaseDownIsTheSoundFontsAndUpSetsIt()
    {
        const auto tail = [this](double release) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            if (release > 0) rig.set(Param::Release, vFor(release, 20000.0));
            const auto out = rig.play({{0.0, noteOn(kRoot)}, {0.3, noteOff(kRoot)}}, 1.0);
            return db(rms(out, 0.5, 0.55) / rms(out, 0.2, 0.3)); // 0.2 s after the key is let go
        };
        const double own = tail(0.0);
        const double longer = tail(1.0);
        qInfo("0.2 s after letting go: the SoundFont's 0.05 s release %.1f dB, 1 s release %.1f dB", own, longer);
        QVERIFY(own < -60.0);
        QVERIFY(longer > -30.0 && longer < -10.0); // FluidSynth (as the EMU) falls 96 dB over the release time
    }

    void sustainAndDecaySetTheLevelHeldAndHowFastItGetsThere()
    {
        const auto level = [this](double sustain, double decay, double at) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            if (sustain > 0) rig.set(Param::Sustain, sustain);
            if (decay > 0) rig.set(Param::Decay, vFor(decay, 20000.0));
            const auto out = rig.play({{0.0, noteOn(kRoot)}}, at + 0.05);
            return db(rms(out, at, at + 0.05));
        };
        const double full = level(0.0, 0.01, 0.6);       // the SoundFont's sustain: full
        const double half = level(0.5, 0.01, 0.6) - full; // -30 dB under it
        const double slowDecay = level(0.5, 4.0, 0.3) - full; // still on its way down
        qInfo("held: Sustain at half %.1f dB under the SoundFont's (full); a 4 s decay at 0.3 s %.1f dB", half, slowDecay);
        QVERIFY(half < -25.0 && half > -35.0);
        QVERIFY(slowDecay > -10.0);
    }

    // ---- LFO: the pitch LFO (FL's LFO 2).
    void lfoAmountWobblesThePitchAndTwelveOClockIsNone()
    {
        const auto wobble = [this](double amount, double delaySeconds, double from, double to) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            if (amount > 0) rig.set(Param::LfoAmount, amount);
            rig.set(Param::LfoSpeed, std::log(5.0 / 0.1) / std::log(200.0)); // 5 Hz
            rig.set(Param::LfoDelay, vFor(delaySeconds, 5000.0));
            const auto out = rig.play({{0.0, noteOn(kRoot)}}, to);
            return spread(pitches(out, from, to));
        };
        const double own = wobble(0.0, 0.002, 0.1, 1.0);    // the SoundFont has none
        const double none = wobble(0.5, 0.002, 0.1, 1.0);   // 12 o'clock: zero
        const double up = wobble(1.0, 0.002, 0.1, 1.0);     // +2 semitones
        const double down = wobble(0.0001, 0.002, 0.1, 1.0); // just off the left: -2 semitones
        qInfo("pitch spread: SoundFont's %.1f Hz, 12 o'clock %.1f Hz, right %.1f Hz, left %.1f Hz", own, none, up, down);
        QVERIFY(own < 3.0);
        QVERIFY(none < 3.0);
        QVERIFY(up > 60.0);   // ±2 semitones round 480 Hz: about 110 Hz peak to peak
        QVERIFY(down > 60.0);
        // The delay: none before it.
        const double before = wobble(1.0, 0.8, 0.1, 0.7);
        qInfo("with a 0.8 s delay, before it: %.1f Hz", before);
        QVERIFY(before < 3.0);
    }

    // ---- Misc: the filter.
    void cutoffClosesTheFilterAndModOpensIt()
    {
        const auto take = [this](double cutoff, double mod) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            if (cutoff > 0) rig.set(Param::Cutoff, cutoff);
            if (mod > 0) rig.set(Param::CutoffMod, mod);
            return rig.play({{0.0, noteOn(kRoot)}}, 0.5);
        };
        const auto open = take(0.0, 0.0);
        const double closedAt = std::log(200.0 / 20.0) / std::log(1000.0); // 200 Hz
        const auto closed = take(closedAt, 0.0);
        const auto modded = take(closedAt, 0.5); // +4 octaves: 3.2 kHz
        const double closedDb = db(rms(closed, 0.2, 0.5) / rms(open, 0.2, 0.5));
        const double moddedDb = db(rms(modded, 0.2, 0.5) / rms(open, 0.2, 0.5));
        qInfo("cut at 200 Hz: %.1f dB; with MOD +4 oct: %.1f dB; overtone share open %.3f, cut %.4f",
              closedDb, moddedDb, brightness(open, 0.2, 0.5), brightness(closed, 0.2, 0.5));
        QVERIFY(closedDb < -10.0);
        QVERIFY(moddedDb > -4.0);
        QVERIFY(brightness(closed, 0.2, 0.5) < brightness(open, 0.2, 0.5) / 20.0);
    }

    // ---- Options.
    void velocityCutoffSwitchesTheStandardVelocityFilter()
    {
        // On: softer notes darker (the standard's 2 octaves at the softest);
        // off: every note as bright. (FluidSynth alone never darkens them.)
        const auto share = [this](bool on, int velocity) {
            Rig rig(m_path);
            rig.dry();
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            rig.set(Param::VelocityCutoff, on ? 1.0 : 0.0);
            return brightness(rig.play({{0.0, noteOn(kRoot, velocity)}}, 0.4), 0.2, 0.4);
        };
        const double softOn = share(true, 30);
        const double softOff = share(false, 30);
        const double hardOn = share(true, 127);
        const double hardOff = share(false, 127);
        qInfo("overtone share at velocity 30: on %.3f, off %.3f; at 127: on %.3f, off %.3f", softOn, softOff, hardOn, hardOff);
        QVERIFY(softOff > softOn * 2.0);
        QVERIFY(std::abs(hardOn - hardOff) < 0.02 * hardOff);
        QVERIFY(std::abs(softOff - hardOff) < 0.02 * hardOff);
    }

    void drumsPlayEachHitToItsEndWhateverTheKey()
    {
        const auto take = [this](Mode mode, int presetIndex) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Attack, vFor(0.002, 10000.0)); // (fully down would be the SoundFont's 0.3 s)
            rig.set(Param::Preset, stepValue(presetIndex, 2));
            rig.choose(mode);
            // Let go after 20 ms; held keys of a loop would sound for ever.
            return rig.play({{0.0, noteOn(kRoot)}, {0.02, noteOff(kRoot)}}, 0.4);
        };
        const auto instrument = take(Mode::Instrument, 0);
        const auto drums = take(Mode::Drums, 0);
        const auto autoTone = take(Mode::Auto, 0); // bank 0: an instrument
        const auto autoKit = take(Mode::Auto, 1);  // bank 128: drums
        qInfo("at 70 ms: instrument %.1f dB, drums %.1f dB; after the sample (0.2 s): drums %.1f dB",
              db(rms(instrument, 0.07, 0.09)), db(rms(drums, 0.07, 0.09)), db(rms(drums, 0.2, 0.4)));
        QVERIFY(rms(instrument, 0.07, 0.09) < 1e-3); // let go: the release (0.05 s) is over
        QVERIFY(rms(drums, 0.07, 0.09) > 0.05);       // the hit plays on
        QVERIFY(rms(drums, 0.2, 0.4) < 1e-4);         // to the sample's end (0.1 s), no loop
        QVERIFY(rms(autoTone, 0.07, 0.09) < 1e-3);
        QVERIFY(rms(autoKit, 0.07, 0.09) > 0.05);
    }

    void qualityChangesHowTransposedNotesAreResampled()
    {
        const auto take = [this](Quality q) {
            Rig rig(m_path);
            rig.set(Param::ReverbOn, 0.0);
            rig.set(Param::ChorusOn, 0.0);
            rig.set(Param::Quality, stepValue(static_cast<int>(q), kQualities));
            return rig.play({{0.0, noteOn(kRoot + 7)}}, 0.4);
        };
        const auto crunchy = take(Quality::Crunchy);
        const auto hq = take(Quality::High);
        double diff = 0.0;
        for (std::size_t i = 0; i < crunchy.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(crunchy.at(i) - hq.at(i))));
        qInfo("Crunchy against HQ, a fifth up: up to %.4f apart", diff);
        QVERIFY(diff > 1e-3);
        // (The overtone, near the top, suffers most from linear resampling.)
        QVERIFY(brightness(crunchy, 0.35, 0.4) != brightness(hq, 0.35, 0.4));
    }

    // ---- Reverb and chorus: the light is the built-in effect, the knob its level.
    void reverbAndChorusAreTheBuiltInEffects()
    {
        const auto take = [this](bool reverb, double level, bool chorus) {
            Rig rig(m_path);
            rig.set(Param::Attack, vFor(0.002, 10000.0));
            rig.set(Param::ReverbOn, reverb ? 1.0 : 0.0);
            rig.set(Param::ReverbLevel, level);
            rig.set(Param::ChorusOn, chorus ? 1.0 : 0.0);
            return rig.play({{0.0, noteOn(kRoot)}, {0.3, noteOff(kRoot)}}, 1.2);
        };
        const double off = rms(take(false, 0.5, false), 0.6, 1.0);
        const double half = rms(take(true, 0.5, false), 0.6, 1.0);
        const double full = rms(take(true, 1.0, false), 0.6, 1.0);
        qInfo("after the note: no reverb %.1f dB, level 0.5 %.1f dB, level 1 %.1f dB", db(off), db(half), db(full));
        QVERIFY(off < 1e-6);
        QVERIFY(half > 1e-4);
        QVERIFY(full > half * 1.2);

        const auto dry = take(false, 0.5, false);
        const auto chorus = take(false, 0.5, true);
        double diff = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.3 * kRate); ++i) diff = std::max(diff, static_cast<double>(std::abs(dry.at(i) - chorus.at(i))));
        qInfo("chorus on against off: up to %.4f apart", diff);
        QVERIFY(diff > 1e-3);
    }

    // ---- Presets, and saving.
    void presetsAreChosenByTheControlAndSavedWithTheFile()
    {
        auto node = SoundFontNode::create(kRate, kBlock);
        QVERIFY(node->setFile(m_path));
        QElapsedTimer clock;
        clock.start();
        while (node->parameterValue(readings::kSoundFontFile) < 1.0 && clock.elapsed() < 10000) {
            node->serviceMainThread();
            QThread::msleep(5);
        }
        QCOMPARE(node->parameterValue(readings::kSoundFontFile), 1.0);
        const auto params = node->parameters();
        QCOMPARE(params.front().choices, (QStringList{u"000:000 Tone"_s, u"128:000 Kit"_s}));
        QVERIFY(node->setParameter(paramId(Param::Preset), 1.0));                // the kit
        QVERIFY(node->setParameter(paramId(Param::Attack), 0.25));
        QVERIFY(node->setParameter(paramId(Param::LfoAmount), 0.8));
        QVERIFY(node->setParameter(paramId(Param::Mode), stepValue(2, kModes))); // drums
        QVERIFY(node->takeEdited());
        const auto saved = node->saveEncodedState();
        QVERIFY(saved);

        auto again = SoundFontNode::create(kRate, kBlock);
        QVERIFY(again->restoreEncodedState(*saved));
        clock.restart();
        while (again->parameterValue(readings::kSoundFontFile) < 1.0 && clock.elapsed() < 10000) {
            again->serviceMainThread();
            QThread::msleep(5);
        }
        QCOMPARE(again->file(), node->file());
        QCOMPARE(again->parameterValue(paramId(Param::Preset)), 1.0);
        QCOMPARE(again->parameterValue(paramId(Param::Attack)), 0.25);
        QCOMPARE(again->parameterValue(paramId(Param::LfoAmount)), 0.8);
        QCOMPARE(again->parameterValue(paramId(Param::Mode)), stepValue(2, kModes));
        QVERIFY(again->takeMessages().isEmpty());

        // Saved, then the file moved away: said, and the settings kept.
        const QString elsewhere = m_dir.path() + u"/moved.sf2"_s;
        QVERIFY(QFile::copy(m_path, elsewhere));
        auto gone = SoundFontNode::create(kRate, kBlock);
        QVERIFY(gone->setFile(elsewhere));
        clock.restart();
        while (gone->parameterValue(readings::kSoundFontFile) < 1.0 && clock.elapsed() < 10000) {
            gone->serviceMainThread();
            QThread::msleep(5);
        }
        const auto goneState = gone->saveEncodedState();
        gone.reset(); // (nobody holds the file in memory any more: it is read again)
        QVERIFY(QFile::remove(elsewhere));
        auto missing = SoundFontNode::create(kRate, kBlock);
        QVERIFY(missing->restoreEncodedState(*goneState));
        clock.restart();
        QStringList said;
        while (said.isEmpty() && clock.elapsed() < 10000) {
            missing->serviceMainThread();
            said = missing->takeMessages();
            QThread::msleep(5);
        }
        QCOMPARE(said.size(), 1);
        QVERIFY2(said.front().contains(u"moved.sf2"_s) && said.front().contains(u"not there"_s), qPrintable(said.front()));
        QCOMPARE(missing->file(), elsewhere); // kept: put the file back and open the song again
        QCOMPARE(missing->parameterValue(readings::kSoundFontFile), 0.25);
    }

    void controlsAtTheirMinimumOverrideNothing()
    {
        std::array<double, kParamCount> values{};
        for (uint32_t id = 0; id < static_cast<uint32_t>(kParamCount); ++id) values.at(id) = paramDefault(id);
        QVERIFY(overrides(values).empty()); // a new player plays the SoundFont exactly as made
        values.at(paramId(Param::Release)) = 0.5;
        QCOMPARE(overrides(values).size(), std::size_t{1});
    }

private:
    QTemporaryDir m_dir;
    QString m_path;
};

QTEST_GUILESS_MAIN(TestSoundFont)
#include "tst_soundfont.moc"
