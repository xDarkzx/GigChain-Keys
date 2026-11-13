// Pads Pro's guard rails: every pad sounds, in tune, never clipping, none
// jumping in level from the next, light enough to play live; every macro
// really changes every pad; songs saved by 0.9 load; the shimmer climbs and
// never runs away. How the pads sound is set by ear against the analog
// collections' (the measurements printed are for that, not targets). With
// GIGCHAIN_PADS_RENDER set to a folder, every pad is rendered there to
// listen to.
#include "PadsPro.h"
#include "PadsProNode.h"
#include "PluginNode.h"
#include "VideoSounds.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

std::size_t sampleAt(double seconds) { return static_cast<std::size_t>(seconds * kRate); }

// The level (dB, both ears) from `from` to `to` seconds.
double rmsDb(const Take& t, double from, double to)
{
    const auto a = sampleAt(from);
    const auto b = std::min(sampleAt(to), t.left.size());
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i) sum += 0.5 * (t.left.at(i) * t.left.at(i) + t.right.at(i) * t.right.at(i));
    return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<std::size_t>(b - a, 1)), 1e-20));
}

// How much of `hz` is in the left channel from `from` to `to` (Goertzel), dB.
double toneDb(const Take& t, double hz, double from, double to)
{
    const auto a = sampleAt(from);
    const auto b = std::min(sampleAt(to), t.left.size());
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

std::vector<float> mono(const Take& t, double from, double to)
{
    const auto a = sampleAt(from);
    const auto b = std::min(sampleAt(to), t.left.size());
    std::vector<float> m;
    m.reserve(b - a);
    for (std::size_t i = a; i < b; ++i) m.push_back(0.5F * (t.left.at(i) + t.right.at(i)));
    return m;
}

double energy(const std::vector<float>& x)
{
    return std::accumulate(x.begin(), x.end(), 0.0, [](double sum, float s) { return sum + static_cast<double>(s) * s; });
}

// `x` through two 12 dB filters in a row (24 dB/oct): high-passed at `lowHz`
// (if above 0), low-passed at `highHz` (if above 0).
std::vector<float> band(const std::vector<float>& x, double lowHz, double highHz)
{
    std::vector<float> y = x;
    for (int pass = 0; pass < 2; ++pass) {
        sawstack::Svf hp;
        sawstack::Svf lp;
        hp.set(std::max(lowHz, 10.0), 0.7071, kRate);
        lp.set(highHz > 0.0 ? highHz : 20000.0, 0.7071, kRate);
        for (float& s : y) {
            if (lowHz > 0.0) s = hp.highPass(s);
            if (highHz > 0.0) s = lp.lowPass(s);
        }
    }
    return y;
}

// The pitch (cents from `hz`) of what is near it, from `from` to `to`
// seconds: the centre of its energy within ±40 cents (Hann window, Goertzel
// every half cent).
double pitchCents(const Take& t, double hz, double from, double to)
{
    const std::vector<float> m = mono(t, from, to);
    const auto n = static_cast<double>(m.size());
    std::vector<double> windowed(m.size());
    for (std::size_t i = 0; i < m.size(); ++i) windowed.at(i) = m.at(i) * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / n));
    double weighted = 0.0;
    double total = 0.0;
    for (int halfCents = -80; halfCents <= 80; ++halfCents) {
        const double cents = 0.5 * halfCents;
        const double w = 2.0 * std::numbers::pi * hz * std::exp2(cents / 1200.0) / kRate;
        const double c = 2.0 * std::cos(w);
        double s1 = 0.0;
        double s2 = 0.0;
        for (const double s : windowed) {
            const double s0 = s + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        weighted += cents * power;
        total += power;
    }
    return total > 0.0 ? weighted / total : 1000.0;
}

std::unique_ptr<Synth> padOf(int type)
{
    auto synth = std::make_unique<Synth>();
    synth->prepare(kRate, kBlock);
    synth->setParam(paramId(Param::Type), typeValue(type));
    return synth;
}

QString nameOf(int type) { return QString::fromLatin1(padType(type).name); }

// C: root, fifth, octave (what Sustain Pads holds).
std::vector<int> drone() { return {48, 55, 60}; }

// One pad holding the drone, measured once for the tests that read it.
struct Measured
{
    double level = 0.0;   // dB, held (5-9 s in)
    double struck = 0.0;  // dB, its first seconds (0.3-2 s): how loud a struck pad (bells, keys) sounds
    double low = 0.0;     // under 200 Hz, dB of the whole
    double mid = 0.0;     // 200 Hz - 1 kHz
    double top = 0.0;     // 4 - 10 kHz
    double swing = 0.0;   // the level's range over 100 ms windows, dB
    double correlation = 0.0; // left against right
};

Measured measure(int type)
{
    auto synth = padOf(type);
    const Take t = play(*synth, drone(), 9.0);
    Measured m;
    m.level = rmsDb(t, 5.0, 9.0);
    m.struck = rmsDb(t, 0.3, 2.0);
    const std::vector<float> x = mono(t, 4.0, 9.0);
    const double all = std::max(energy(x), 1e-20);
    const auto share = [all](const std::vector<float>& part) { return 10.0 * std::log10(std::max(energy(part), 1e-20) / all); };
    m.low = share(band(x, 0.0, 200.0));
    m.mid = share(band(x, 200.0, 1000.0));
    m.top = share(band(x, 4000.0, 10000.0));
    double quiet = 1000.0;
    double loud = -1000.0;
    for (int tenth = 40; tenth < 90; ++tenth) {
        const double at = 0.1 * tenth;
        const double db = rmsDb(t, at, at + 0.1);
        quiet = std::min(quiet, db);
        loud = std::max(loud, db);
    }
    m.swing = loud - quiet;
    double lr = 0.0;
    double ll = 0.0;
    double rr = 0.0;
    for (std::size_t i = sampleAt(4.0); i < sampleAt(9.0); ++i) {
        lr += static_cast<double>(t.left.at(i)) * t.right.at(i);
        ll += static_cast<double>(t.left.at(i)) * t.left.at(i);
        rr += static_cast<double>(t.right.at(i)) * t.right.at(i);
    }
    m.correlation = lr / std::max(std::sqrt(ll * rr), 1e-20);
    return m;
}

} // namespace

class TestPadsPro : public QObject
{
    Q_OBJECT

    std::vector<Measured> m_measured;
    QString m_report;

private slots:
    void initTestCase()
    {
        QStringList lines{u"pad: level | <200 Hz, 200 Hz-1 kHz, 4-10 kHz (dB of the whole) | swing | L/R correlation"_s};
        for (int i = 0; i < padTypeCount(); ++i) {
            m_measured.push_back(measure(i));
            const Measured& m = m_measured.back();
            lines << u"%1: %2 dB | %3, %4, %5 | %6 dB | %7"_s.arg(nameOf(i))
                         .arg(m.level, 0, 'f', 1)
                         .arg(m.low, 0, 'f', 1)
                         .arg(m.mid, 0, 'f', 1)
                         .arg(m.top, 0, 'f', 1)
                         .arg(m.swing, 0, 'f', 1)
                         .arg(m.correlation, 0, 'f', 2);
        }
        m_report = lines.join(u"\n"_s);
        qInfo().noquote() << m_report;
    }

    // Twenty-four pads in six families; the first eight keep their places
    // and keys (saved songs and Sustain Pads ask for them so); every saved
    // knob keeps its key.
    void thereAreTwentyFourPadsAndTheOldOnesKeepTheirPlaces()
    {
        QCOMPARE(padTypeCount(), 23);
        const QStringList firstKeys{u"warm"_s, u"lush"_s, u"dark"_s, u"air"_s, u"shimmer"_s, u"choir"_s, u"strings"_s, u"organ"_s};
        for (int i = 0; i < firstKeys.size(); ++i) QCOMPARE(QString::fromLatin1(padType(i).key), firstKeys.at(i));
        std::set<std::string> keys;
        std::set<std::string> names;
        const std::set<std::string> families{"PIANO", "GUITAR", "STRINGS", "ORGAN", "SYNTH", "AIR", "ATMOS"};
        for (int i = 0; i < padTypeCount(); ++i) {
            QVERIFY2(keys.insert(padType(i).key).second, padType(i).key);
            QVERIFY2(names.insert(padType(i).name).second, padType(i).name);
            QVERIFY2(families.contains(padType(i).family), padType(i).name);
            QCOMPARE(typeIndex(typeValue(i)), i);
        }
        // The knobs 0.9 had that kept their meaning keep their ids and keys (a
        // learned knob, a saved song); the face is an analog collection's.
        for (const auto& [p, key] : {std::pair{Param::Type, u"type"_s}, std::pair{Param::Tone, u"tone"_s}, std::pair{Param::Motion, u"motion"_s},
                                     std::pair{Param::Reverb, u"reverb"_s}, std::pair{Param::Level, u"level"_s}, std::pair{Param::Velocity, u"velocity"_s}}) {
            QCOMPARE(paramKey(paramId(p)), key);
        }
        QCOMPARE(paramId(Param::Tone), 1U);
        QCOMPARE(paramId(Param::Motion), 3U);
        QCOMPARE(paramId(Param::Reverb), 7U);
        QCOMPARE(paramId(Param::Level), 8U);
        QCOMPARE(paramId(Param::Velocity), 9U);
        QStringList knobs;
        for (uint32_t id = 0; id < static_cast<uint32_t>(kParamCount); ++id) knobs << paramName(id);
        QCOMPARE(knobs, (QStringList{u"Type"_s, u"Brightness"_s, u"Chorus"_s, u"Movement"_s, u"Phaser"_s, u"Delay"_s, u"Timbre"_s,
                                     u"Reverb"_s, u"Master"_s, u"Touch"_s, u"Time"_s}));
    }

    // Every pad sounds, and switching pads never jumps in level (within 2
    // dB of their average). (A guard against a fault; how each pad sounds
    // is set by ear.)
    void theTypesAreEquallyLoud()
    {
        // (A struck pad is as loud as it sounds when struck: its first
        // seconds; it then rings on quieter, as a piano or bells do.)
        std::vector<double> levels;
        for (std::size_t i = 0; i < m_measured.size(); ++i) {
            const bool struck = padType(static_cast<int>(i)).attack < 0.1;
            levels.push_back(struck ? m_measured.at(i).struck : m_measured.at(i).level);
        }
        const double average = std::accumulate(levels.begin(), levels.end(), 0.0) / static_cast<double>(levels.size());
        QStringList off; // (every one out of line named at once)
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const auto type = static_cast<int>(i);
            QVERIFY2(levels.at(i) > -40.0, qPrintable(u"%1 is silent: %2 dB"_s.arg(nameOf(type)).arg(levels.at(i))));
            if (std::abs(levels.at(i) - average) > 2.0) off << u"%1 %2 dB"_s.arg(nameOf(type)).arg(levels.at(i) - average, 0, 'f', 1);
        }
        QVERIFY2(off.isEmpty(), qPrintable(u"from the average: %1\n%2"_s.arg(off.join(u", "_s), m_report)));
    }

    // In tune: every pad's note, over four seconds, within 3 cents of true
    // (its movement is in the tone; any vibrato comes and goes around the
    // note). A place (wind, the sea) is noise sung on the note: its pitch
    // reads only roughly, within 12 cents.
    void everyPadIsInTune()
    {
        QStringList report;
        QStringList off;
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            synth->setParam(paramId(Param::Reverb), 0.0); // (the space's own movement aside)
            const Take t = play(*synth, {57}, 8.0);       // A3, 220 Hz
            const double cents = pitchCents(t, 220.0, 4.0, 8.0);
            report << u"%1 %2"_s.arg(nameOf(i)).arg(cents, 0, 'f', 2);
            const double limit = std::string_view(padType(i).family) == "ATMOS" ? 12.0 : 3.0;
            if (std::abs(cents) > limit) off << u"%1 %2"_s.arg(nameOf(i)).arg(cents, 0, 'f', 2);
        }
        qInfo().noquote() << u"cents from true:"_s << report.join(u", "_s);
        QVERIFY2(off.isEmpty(), qPrintable(u"out of tune (cents): %1"_s.arg(off.join(u", "_s))));
    }

    // Another pad chosen while a chord is held (a player changing sound
    // live): the held notes ring out as the pad they were, the new pad
    // plays the next chord, and nothing breaks, whichever pads they are
    // (a recorded pad's notes never read a synth's leftovers).
    void anotherPadWhileAChordIsHeldRingsOutAndPlaysOn()
    {
        // As the app plays it (the instrument loaded, its recordings read),
        // chords overlapping as a player's do: each pressed before the last
        // is let go, the pad changed between them.
        // C, G, Am, F a bar each at 124 BPM, the bass an octave down, each
        // chord held a little past the next (seen breaking: Juno Warmth to
        // Orchestra Strings on the third bar).
        {
            auto loaded = PluginNode::load(QString::fromLatin1(PadsProNode::kPluginId), kRate, kBlock);
            QVERIFY(loaded.has_value());
            PluginNode& node = **loaded;
            QVERIFY(node.setParameter(paramId(Param::Type), typeValue(0)).has_value());
            const std::array<std::array<int, 4>, 4> bars{{{36, 60, 64, 67}, {31, 59, 62, 67}, {33, 60, 64, 69}, {29, 60, 65, 69}}};
            const double samplesPerBeat = kRate * 60.0 / 124.0;
            struct Ev
            {
                Ev(int64_t when, MidiEvent event) : at(when), midi(event) {}
                int64_t at = 0;
                MidiEvent midi{};
            };
            std::vector<Ev> evs;
            for (int bar = 0; bar < 8; ++bar) {
                for (const int k : bars.at(static_cast<std::size_t>(bar % 4))) {
                    evs.emplace_back(static_cast<int64_t>(bar * 4.0 * samplesPerBeat), noteOn(k, 88));
                    evs.emplace_back(static_cast<int64_t>(((bar * 4.0) + 4.05) * samplesPerBeat), noteOff(k));
                }
            }
            std::ranges::stable_sort(evs, {}, &Ev::at);
            std::vector<float> l(kBlock), r(kBlock);
            std::size_t next = 0;
            bool finite = true;
            const auto changeAt = static_cast<int64_t>(12.0 * samplesPerBeat);
            for (int64_t at = 0; at < static_cast<int64_t>(32.0 * samplesPerBeat); at += kBlock) {
                if (changeAt >= at && changeAt < at + kBlock) QVERIFY(node.setParameter(paramId(Param::Type), typeValue(6)).has_value());
                std::vector<MidiEvent> block;
                for (; next < evs.size() && evs.at(next).at < at + kBlock; ++next) {
                    MidiEvent e = evs.at(next).midi;
                    e.sampleOffset = static_cast<int32_t>(std::max<int64_t>(evs.at(next).at - at, 0));
                    block.push_back(e);
                }
                const TimeInfo time{.tempo = 124.0, .sampleRate = kRate, .samplePosition = at, .ppqPosition = static_cast<double>(at) / samplesPerBeat};
                node.process(block, AudioBlock{.left = l.data(), .right = r.data(), .frames = kBlock}, time);
                finite = finite && std::ranges::all_of(l, [](float x) { return std::isfinite(x); });
            }
            QVERIFY(finite);
        }
        const std::vector<std::vector<int>> chords{{48, 60, 64, 67}, {45, 60, 64, 69}};
        for (int i = 0; i < padTypeCount(); ++i) {
            for (const auto& [from, to] : {std::pair{0, i}, std::pair{i, 0}, std::pair{i, (i + 7) % padTypeCount()}}) {
                if (from == to) continue;
                auto loaded = PluginNode::load(QString::fromLatin1(PadsProNode::kPluginId), kRate, kBlock);
                QVERIFY(loaded.has_value());
                PluginNode& node = **loaded;
                QVERIFY(node.setParameter(paramId(Param::Type), typeValue(from)).has_value());
                std::vector<float> l(kBlock), r(kBlock);
                bool finite = true;
                double lastLevel = 0.0;
                for (int block = 0; block < static_cast<int>(4.0 * kRate) / kBlock; ++block) {
                    std::vector<MidiEvent> events;
                    const int bar = block / (static_cast<int>(kRate) / kBlock); // a chord a second
                    if (block % (static_cast<int>(kRate) / kBlock) == 0) {
                        if (bar == 2) QVERIFY(node.setParameter(paramId(Param::Type), typeValue(to)).has_value());
                        std::ranges::transform(chords.at(static_cast<std::size_t>(bar % 2)), std::back_inserter(events), [](int k) { return noteOn(k); });
                        if (bar > 0) {
                            for (const int k : chords.at(static_cast<std::size_t>((bar + 1) % 2))) {
                                MidiEvent off = noteOff(k);
                                off.sampleOffset = 100; // (let go just after the next is pressed)
                                events.push_back(off);
                            }
                        }
                    }
                    node.process(events, AudioBlock{.left = l.data(), .right = r.data(), .frames = kBlock}, TimeInfo{});
                    finite = finite && std::ranges::all_of(l, [](float x) { return std::isfinite(x); })
                             && std::ranges::all_of(r, [](float x) { return std::isfinite(x); });
                    lastLevel = std::accumulate(l.begin(), l.end(), 0.0, [](double sum, float x) { return sum + (static_cast<double>(x) * x); });
                }
                QVERIFY2(finite, qPrintable(u"%1 to %2"_s.arg(nameOf(from), nameOf(to))));
                QVERIFY2(lastLevel > 1e-12, qPrintable(u"%1 to %2: the new pad is silent"_s.arg(nameOf(from), nameOf(to))));
            }
        }
    }

    // Every note its own voice (as an analog polysynth's), yet each one in
    // tune: the same key six times, never more than 3 cents apart.
    void everyNoteIsInTuneWithTheLast()
    {
        auto synth = padOf(0); // Juno Warmth: an analog polysynth's voices
        synth->setParam(paramId(Param::Reverb), 0.0);
        synth->setParam(paramId(Param::Delay), 0.0);
        synth->setParam(paramId(Param::Time), 0.0); // (quick: each take its own voice)
        std::vector<double> pitch;
        for (int take = 0; take < 6; ++take) {
            const Take t = play(*synth, {69}, 7.0, 3.0); // A4 for 3 s, then its tail
            pitch.push_back(pitchCents(t, 440.0, 1.0, 3.0));
        }
        const double range = *std::ranges::max_element(pitch) - *std::ranges::min_element(pitch);
        qInfo() << "pitch over six notes ranges" << range << "cents";
        QVERIFY2(range < 3.0, qPrintable(u"pitch differs by %1 cents"_s.arg(range)));
    }

    // Light enough to play live: sixteen notes of each pad take under a
    // quarter of one core (an optimised build; a debug build only reports).
    void sixteenNotesAreLiveSafe()
    {
        std::vector<int> keys(16);
        std::ranges::generate(keys, [k = 43]() mutable { return std::exchange(k, k + 2); });
        double worst = 0.0;
        QString worstName;
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            QElapsedTimer timer;
            timer.start();
            (void)play(*synth, keys, 4.0);
            const double share = static_cast<double>(timer.nsecsElapsed()) / 4.0e9;
            if (share > worst) {
                worst = share;
                worstName = nameOf(i);
            }
        }
        qInfo().noquote() << u"heaviest: %1, %2 % of a core for 16 notes"_s.arg(worstName).arg(worst * 100.0, 0, 'f', 1);
#ifdef NDEBUG
        QVERIFY2(worst < 0.25, qPrintable(u"%1 takes %2 % of a core"_s.arg(worstName).arg(worst * 100.0, 0, 'f', 1)));
#endif
    }

    // A pad blooms in (quiet in its first tenth of a second, full by its
    // attack; a struck one, bells or keys, starts as it is struck), and
    // never clips however many notes.
    void padsBloomInAndNeverClip()
    {
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            const Take t = play(*synth, {48, 52, 55, 60, 64, 67, 72, 76}, 7.0);
            const double first = rmsDb(t, 0.0, 0.1);
            const double full = rmsDb(t, 5.0, 7.0);
            const bool struck = padType(i).attack < 0.1;
            QVERIFY2(struck || full - first > 10.0, qPrintable(u"%1: first 0.1 s %2 dB, full %3 dB"_s.arg(nameOf(i)).arg(first).arg(full)));
            float peak = 0.0F;
            for (std::size_t s = 0; s < t.left.size(); ++s) peak = std::max({peak, std::abs(t.left.at(s)), std::abs(t.right.at(s))});
            QVERIFY2(peak < 1.0F, qPrintable(u"%1 peaks at %2"_s.arg(nameOf(i)).arg(peak)));
        }
    }

    // Pads sit steady under the piano: playing softly or hard is the
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

    // Let go: it rings on (the release, then the space), then falls silent.
    void letGoItRingsOnThenStops()
    {
        auto synth = padOf(0); // Juno Warmth: a 3.5 s tail
        const Take t = play(*synth, drone(), 26.0, 4.0);
        const double held = rmsDb(t, 3.0, 4.0);
        QVERIFY2(rmsDb(t, 4.1, 4.4) > held - 12.0, "it should ring on just after the keys are let go");
        QVERIFY2(rmsDb(t, 24.0, 26.0) < held - 50.0, qPrintable(u"still %1 dB 20 s later"_s.arg(rmsDb(t, 24.0, 26.0))));
        QVERIFY(!synth->holdsNotes());
    }

    // The shimmer (as Sustain Pads adds it): energy two octaves above the
    // note, growing with it; none without it. (On the organ: its drawbars
    // make nothing two octaves up of their own, where a saw's harmonics are
    // everywhere.)
    void theShimmerClimbsAnOctave()
    {
        const auto octaveUp = [](double shimmer) {
            auto synth = padOf(7); // Organ
            synth->setExtraShimmer(shimmer);
            synth->setParam(paramId(Param::Reverb), 0.6);
            const Take t = play(*synth, {57}, 10.0, 3.0); // A3 (220 Hz) for 3 s, then the tail
            return toneDb(t, 880.0, 4.0, 10.0) - toneDb(t, 220.0, 4.0, 10.0); // two octaves up, relative to the note
        };
        const double none = octaveUp(0.0);
        const double full = octaveUp(1.0);
        qInfo() << "two octaves up, in the tail: no shimmer" << none << "dB, full" << full << "dB (relative to the note)";
        QVERIFY2(full - none > 6.0, qPrintable(u"shimmer adds only %1 dB two octaves up"_s.arg(full - none)));
    }

    // At its most (shimmer and space full, a chord held a minute) it stays
    // bounded: the tail does not keep growing.
    void theShimmerNeverRunsAway()
    {
        auto synth = padOf(4);
        synth->setExtraShimmer(1.0);
        synth->setParam(paramId(Param::Motion), 1.0); // (its Movement brings more shimmer in)
        synth->setParam(paramId(Param::Reverb), 1.0);
        const Take t = play(*synth, drone(), 60.0);
        const double middle = rmsDb(t, 20.0, 30.0);
        const double late = rmsDb(t, 50.0, 60.0);
        QVERIFY2(late < middle + 2.0, qPrintable(u"the tail grew from %1 to %2 dB"_s.arg(middle).arg(late)));
        const float peak = std::accumulate(t.left.begin(), t.left.end(), 0.0F, [](float most, float s) { return std::max(most, std::abs(s)); });
        QVERIFY(peak < 1.0F);
    }

    // Movement: the slow movement (the filter breathing, its own random
    // wander, the pulse widths) is there, and Movement at 0 stills it.
    // Measured as brightness: the share of the sound above 3 kHz, two
    // seconds at a time (the copies' quicker beating evens out; a breath
    // ten seconds long does not), both ears (the chorus moves each ear's
    // comb on its own; the two together stay put).
    void movementMovesThePad()
    {
        const auto movement = [](double motion) {
            auto synth = padOf(1); // Supersaw Haze
            synth->setParam(paramId(Param::Motion), motion);
            synth->setParam(paramId(Param::Reverb), 0.0);
            const Take t = play(*synth, drone(), 24.0);
            sawstack::Svf highL;
            sawstack::Svf highR;
            highL.set(3000.0, 0.7071, kRate);
            highR.set(3000.0, 0.7071, kRate);
            std::vector<float> topL(t.left.size());
            std::vector<float> topR(t.right.size());
            for (std::size_t i = 0; i < t.left.size(); ++i) {
                topL.at(i) = highL.highPass(t.left.at(i));
                topR.at(i) = highR.highPass(t.right.at(i));
            }
            double low = 1000.0;
            double most = -1000.0;
            for (int second = 4; second < 24; second += 2) {
                const auto a = sampleAt(second);
                const auto b = sampleAt(second + 2);
                double all = 0.0;
                double bright = 0.0;
                for (std::size_t i = a; i < b; ++i) {
                    all += t.left.at(i) * t.left.at(i) + t.right.at(i) * t.right.at(i);
                    bright += topL.at(i) * topL.at(i) + topR.at(i) * topR.at(i);
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
        QVERIFY2(moving > still + 1.5, qPrintable(u"Movement swings the brightness %1 dB, still %2 dB"_s.arg(moving).arg(still)));
    }

    // Each macro, on every pad, really changes it: turned fully down and
    // fully up, the pad differs by at least 1 dB in its level, its top, its
    // bottom, its first half second or how much its top moves. (What each
    // does is the pad's design, set by ear.)
    void everyMacroChangesEveryPad()
    {
        struct Features
        {
            std::array<double, 5> values{};
        };
        const auto features = [](int type, Param macro, double value) {
            auto synth = padOf(type);
            synth->setParam(paramId(Param::Reverb), 0.0);
            synth->setParam(paramId(macro), value);
            const Take t = play(*synth, drone(), 5.0);
            const std::vector<float> x = mono(t, 2.5, 5.0);
            const double all = std::max(energy(x), 1e-20);
            const auto share = [all](const std::vector<float>& part) { return 10.0 * std::log10(std::max(energy(part), 1e-20) / all); };
            double low = 1000.0;
            double high = -1000.0;
            for (int quarter = 6; quarter < 20; ++quarter) {
                const double at = 0.25 * quarter;
                const std::vector<float> part = mono(t, at, at + 0.25);
                const double s = 10.0 * std::log10(std::max(energy(band(part, 3000.0, 0.0)), 1e-20) / std::max(energy(part), 1e-20));
                low = std::min(low, s);
                high = std::max(high, s);
            }
            return Features{{rmsDb(t, 2.5, 5.0), share(band(x, 3000.0, 0.0)), share(band(x, 0.0, 300.0)), rmsDb(t, 0.2, 0.7), high - low}};
        };
        QStringList report;
        QStringList weak;
        for (int i = 0; i < padTypeCount(); ++i) {
            for (const auto& [macro, name] : {std::pair{Param::Tone, "Brightness"}, std::pair{Param::Timbre, "Timbre"},
                                              std::pair{Param::Time, "Time"}, std::pair{Param::Motion, "Movement"}}) {
                const Features down = features(i, macro, 0.0);
                const Features up = features(i, macro, 1.0);
                double most = 0.0;
                for (std::size_t f = 0; f < down.values.size(); ++f) most = std::max(most, std::abs(up.values.at(f) - down.values.at(f)));
                const QString line = u"%1 %2 %3"_s.arg(nameOf(i), QString::fromLatin1(name)).arg(most, 0, 'f', 1);
                report << line;
                if (most < 1.0) weak << line;
            }
        }
        qInfo().noquote() << u"each macro's change (dB):"_s << report.join(u", "_s);
        QVERIFY2(weak.isEmpty(), qPrintable(u"macros that hardly change their pad (dB): %1"_s.arg(weak.join(u", "_s))));
    }

    // A new pad brings its own knobs, as a preset does (the macros in the
    // middle, the effects where it has them); Master is the player's.
    void aNewPadBringsItsKnobs()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        synth.setParam(paramId(Param::Level), 0.9);
        synth.setParam(paramId(Param::Tone), 0.1);
        synth.setParam(paramId(Param::Type), typeValue(12)); // Celeste
        QVERIFY(std::abs(synth.param(paramId(Param::Tone)) - 0.5) < 1e-6);
        QVERIFY(std::abs(synth.param(paramId(Param::Chorus)) - padType(12).chorus) < 1e-6); // (kept as a float)
        QVERIFY(std::abs(synth.param(paramId(Param::Delay)) - padType(12).echo) < 1e-6);
        QVERIFY(std::abs(synth.param(paramId(Param::Reverb)) - padType(12).reverb) < 1e-6);
        QVERIFY(std::abs(synth.param(paramId(Param::Level)) - 0.9) < 1e-6);
    }

    // A song saved by 0.9 (its knobs then: Tone, Shimmer, Motion, Attack,
    // Release, Width, Reverb, Level, Velocity) loads: its pad, and the knobs
    // that kept their meaning; the rest are the pad's own.
    void aSongSavedBy09Loads()
    {
        const QJsonObject params{{u"type"_s, u"lush"_s}, {u"tone"_s, 0.3},   {u"shimmer"_s, 0.8}, {u"motion"_s, 0.7},
                                 {u"attack"_s, 0.2},     {u"release"_s, 0.6}, {u"width"_s, 0.5},  {u"reverb"_s, 0.25},
                                 {u"level"_s, 0.7},      {u"velocity"_s, 0.1}};
        const QJsonObject state{{u"version"_s, 1}, {u"params"_s, params}};
        const QByteArray bytes = QByteArray("GCP1") + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
        auto node = PadsProNode::create(kRate, kBlock);
        const auto loaded = node->restoreEncodedState(bytes);
        QVERIFY2(loaded.has_value(), qPrintable(loaded.has_value() ? QString() : loaded.error().message));
        const auto value = [&node](Param p) { return node->parameterValue(paramId(p)); };
        QCOMPARE(typeIndex(value(Param::Type)), 1);
        QVERIFY(std::abs(value(Param::Tone) - 0.3) < 1e-6);
        QVERIFY(std::abs(value(Param::Motion) - 0.7) < 1e-6);
        QVERIFY(std::abs(value(Param::Reverb) - 0.25) < 1e-6);
        QVERIFY(std::abs(value(Param::Level) - 0.7) < 1e-6);
        QVERIFY(std::abs(value(Param::Velocity) - 0.1) < 1e-6);
        QVERIFY(std::abs(value(Param::Chorus) - padType(1).chorus) < 1e-6);
        QVERIFY(std::abs(value(Param::Timbre) - 0.5) < 1e-6);
    }

    void diagnoseNotes()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_PADS_DIAG");
        if (folder.isEmpty()) QSKIP("diagnostics");
        QVERIFY(QDir().mkpath(folder));
        struct Case
        {
            int type = 0;
            const char* tag = "";
            std::vector<std::pair<Param, double>> knobs;
        };
        const std::vector<Case> cases{{.type = 0, .tag = "warm", .knobs = {}},
                                      {.type = 0, .tag = "warm_still", .knobs = {{Param::Motion, 0.0}}},
                                      {.type = 1, .tag = "lush_drive0", .knobs = {{Param::Timbre, 0.0}, {Param::Motion, 0.0}}},
                                      {.type = 1, .tag = "lush_drive1", .knobs = {{Param::Timbre, 1.0}, {Param::Motion, 0.0}}},
                                      {.type = 2, .tag = "dark_drive0", .knobs = {{Param::Timbre, 0.0}, {Param::Motion, 0.0}}},
                                      {.type = 2, .tag = "dark_drive1", .knobs = {{Param::Timbre, 1.0}, {Param::Motion, 0.0}}},
                                      {.type = 9, .tag = "space_still", .knobs = {{Param::Motion, 0.0}}}};
        for (const Case& c : cases) {
            for (const int note : {72, 84, 96}) {
                auto synth = padOf(c.type);
                for (const Param p : {Param::Reverb, Param::Delay, Param::Chorus, Param::Phaser}) synth->setParam(paramId(p), 0.0);
                for (const auto& [p, v] : c.knobs) synth->setParam(paramId(p), v);
                const Take t = play(*synth, {note}, 6.0);
                std::vector<float> both;
                for (std::size_t s = 0; s < t.left.size(); ++s) {
                    both.push_back(t.left.at(s));
                    both.push_back(t.right.at(s));
                }
                QVERIFY(video::writeWav(u"%1/%2-%3.wav"_s.arg(folder, QString::fromLatin1(c.tag)).arg(note), both));
            }
        }
    }

    // A note played again while its tail still rings sounds again at once
    // (the bug: an engine's envelopes stayed let go, the note silent until
    // its old tail had died, about ten seconds).
    void aNoteReplayedInItsTailSoundsAgain()
    {
        QStringList quiet;
        for (int i = 0; i < padTypeCount(); ++i) {
            auto synth = padOf(i);
            const Take first = play(*synth, {60}, 4.0, 3.0);
            const Take again = play(*synth, {60}, 3.0);
            const double before = rmsDb(first, 2.0, 3.0);
            const double after = rmsDb(again, 2.0, 3.0);
            if (after < before - 6.0) quiet << u"%1 (%2 dB, was %3)"_s.arg(nameOf(i)).arg(after, 0, 'f', 1).arg(before, 0, 'f', 1);
        }
        QVERIFY2(quiet.isEmpty(), qPrintable(quiet.join(u", "_s)));
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
            const QString path = u"%1/pads-pro-%2-%3.wav"_s.arg(folder).arg(i + 1, 2, 10, QChar(u'0')).arg(QString::fromLatin1(padType(i).key));
            QVERIFY2(video::writeWav(path, both), qPrintable(path));
            qInfo().noquote() << path;
        }
    }
};

QTEST_GUILESS_MAIN(TestPadsPro)
#include "tst_pads_pro.moc"
