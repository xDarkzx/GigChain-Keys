// Piano Pro, the app's own stage piano: its recorded pianos play in tune
// between their recordings, harder is louder (and from the louder
// recordings), let-go keys ring out with their release sound, the pedal
// holds, and a held note rings on round its loop, fading, without a click;
// its DX piano and its polysynth play in tune and brighten when struck
// harder; without its recordings it still plays (its FM piano); every control
// on its window changes the sound; its types are named as players name
// them and sit at about the same loudness; it saves and loads.
#include "BuiltinEffects.h"
#include "PianoLibrary.h"
#include "PianoPro.h"
#include "PianoProNode.h"
#include "PluginNode.h"
#include "SawStack.h"
#include "VideoSounds.h"

#include "gigchain/engine/SidechainCurves.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <numbers>
#include <numeric>
#include <ranges>
#include <utility>
#include <vector>

using namespace gigchain::engine;
using namespace gigchain::engine::piano;
using namespace Qt::StringLiterals;
using sawstack::Ranges;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

MidiEvent noteOn(int note, int velocity = 100)
{
    return MidiEvent{.status = 0x90, .data1 = static_cast<uint8_t>(note), .data2 = static_cast<uint8_t>(velocity)};
}
MidiEvent noteOff(int note)
{
    return MidiEvent{.status = 0x80, .data1 = static_cast<uint8_t>(note), .data2 = 0};
}
MidiEvent pedal(bool down)
{
    return MidiEvent{.status = 0xB0, .data1 = 64, .data2 = static_cast<uint8_t>(down ? 127 : 0)};
}

struct Audio
{
    std::vector<float> left, right;
};

Audio play(Synth& synth, const std::vector<MidiEvent>& events, double seconds)
{
    Audio out;
    const auto total = static_cast<int>(seconds * kRate);
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    bool first = true;
    for (int done = 0; done < total; done += kBlock) {
        const int frames = std::min(kBlock, total - done);
        synth.process(first ? std::span<const MidiEvent>(events) : std::span<const MidiEvent>(),
                      AudioBlock{.left = l.data(), .right = r.data(), .frames = frames});
        first = false;
        out.left.insert(out.left.end(), l.begin(), std::next(l.begin(), frames));
        out.right.insert(out.right.end(), r.begin(), std::next(r.begin(), frames));
    }
    return out;
}

double rms(std::span<const float> s)
{
    if (s.empty()) return 0.0;
    const double sum = std::accumulate(s.begin(), s.end(), 0.0, [](double total, float x) { return total + (double{x} * x); });
    return std::sqrt(sum / static_cast<double>(s.size()));
}

double rms(const std::vector<float>& s, std::size_t from = 0, std::size_t count = SIZE_MAX)
{
    const std::span<const float> all(s);
    from = std::min(from, all.size());
    return rms(all.subspan(from, std::min(count, all.size() - from)));
}

bool allFinite(const Audio& a)
{
    const auto finite = [](float x) { return std::isfinite(x); };
    return std::ranges::all_of(a.left, finite) && std::ranges::all_of(a.right, finite);
}

float peak(const Audio& a)
{
    const auto loudest = [](const std::vector<float>& s) {
        return std::accumulate(s.begin(), s.end(), 0.0F, [](float p, float x) { return std::max(p, std::abs(x)); });
    };
    return std::max(loudest(a.left), loudest(a.right));
}

double pitchOf(const std::vector<float>& s, std::size_t from)
{
    int crossings = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    for (std::size_t i = from + 1; i < s.size(); ++i) {
        if (s.at(i - 1) <= 0.0F && s.at(i) > 0.0F) {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * kRate / static_cast<double>(last - first) : 0.0;
}

// The pitch by autocorrelation over 8192 samples from `from` (a sound rich in
// harmonics crosses zero more than once a period); 40 Hz to 2 kHz.
double periodPitchOf(const std::vector<float>& s, std::size_t from)
{
    constexpr std::size_t kLength = 8192;
    if (from + kLength + 1200 > s.size()) return 0.0;
    const auto at = [&](std::size_t lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < kLength; ++i) sum += double{s.at(from + i)} * s.at(from + i + lag);
        return sum;
    };
    const auto lo = static_cast<std::size_t>(kRate / 2000.0);
    const auto hi = static_cast<std::size_t>(kRate / 40.0);
    const double zero = at(0);
    // The first lag that comes back nearly as alike as none (the period, not a multiple of it).
    std::vector<double> c(hi + 2);
    for (std::size_t lag = lo; lag <= hi + 1; ++lag) c.at(lag) = at(lag);
    for (std::size_t lag = lo + 1; lag <= hi; ++lag) {
        if (c.at(lag) > 0.8 * zero && c.at(lag) >= c.at(lag - 1) && c.at(lag) >= c.at(lag + 1)) {
            // Between samples: a parabola through the peak.
            const double a = c.at(lag - 1), b = c.at(lag), d = c.at(lag + 1);
            const double shift = 0.5 * (a - d) / (a - 2.0 * b + d);
            return kRate / (static_cast<double>(lag) + shift);
        }
    }
    return 0.0;
}

// How bright: the level of the sound's changes against its level.
double brightness(const std::vector<float>& s, std::size_t from, std::size_t count)
{
    std::vector<float> change;
    for (std::size_t i = from + 1; i < std::min(s.size(), from + count); ++i) change.push_back(s.at(i) - s.at(i - 1));
    return rms(change) / std::max(rms(s, from, count), 1e-12);
}

// A level the made-up piano's recordings come in at (about -24 dB).
constexpr float kQuietAsVcsl = 0.063F;

// A recorded piano made here: sines at each recording's key, two loudness
// layers (soft 0.2, hard 0.6), a release sound (a 2 kHz blip) per note.
SampleSet madeUpPiano(bool withReleases = true)
{
    SampleSet set;
    set.name = u"Made Up"_s;
    set.sampleRate = kRate;
    set.velocityTrack = 0.0F; // the layers alone make it louder
    for (const int key : {36, 48, 60, 72, 84}) {
        for (int layer = 0; layer < 2; ++layer) {
            SampleZone z;
            z.loKey = key - 6;
            z.hiKey = key + 5;
            z.keyCenter = key;
            z.loVel = layer == 0 ? 0 : 80;
            z.hiVel = layer == 0 ? 79 : 127;
            z.attack = 0.002;
            z.release = 0.3;
            z.gain = kQuietAsVcsl;
            const double hz = 440.0 * std::exp2((key - 69) / 12.0);
            const auto frames = static_cast<std::size_t>(3.0 * kRate);
            for (std::size_t i = 0; i < frames; ++i) {
                const double fade = std::exp(-static_cast<double>(i) / (2.0 * kRate));
                const auto v = static_cast<int16_t>(
                    std::lround((layer == 0 ? 0.2 : 0.6) * fade * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate) * 32767.0));
                z.frames.push_back(v);
                z.frames.push_back(v);
            }
            set.attack.push_back(std::move(z));
        }
        if (!withReleases) continue;
        SampleZone r;
        r.loKey = key - 6;
        r.hiKey = key + 5;
        r.keyCenter = key;
        r.release = 0.05;
        r.gain = kQuietAsVcsl;
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.3 * kRate); ++i) {
            const auto v = static_cast<int16_t>(std::lround(0.6 * std::sin(2.0 * std::numbers::pi * 2000.0 * static_cast<double>(i) / kRate) * 32767.0));
            r.frames.push_back(v);
            r.frames.push_back(v);
        }
        set.release.push_back(std::move(r));
    }
    return set;
}

int typeNamed(const char* name)
{
    const auto types = pianoTypes();
    const auto found = std::ranges::find_if(types, [&](const PianoType& t) { return QLatin1StringView(t.name) == QLatin1StringView(name); });
    if (found == types.end()) qFatal("no piano type %s", name);
    return static_cast<int>(std::distance(types.begin(), found));
}

// Only the piano layer, plain: no effects, no sidechain, Tone open.
void plainPiano(Synth& synth, const char* type)
{
    synth.setParam(paramId(Macro::Type), typeValue(typeNamed(type)));
    for (const Macro m : {Macro::Reverb, Macro::Delay, Macro::Ott, Macro::Width, Macro::Tape, Macro::Sidechain}) synth.setParam(paramId(m), 0.0);
    synth.setParam(paramId(Macro::Tone), 1.0);
    for (const Layer l : {Layer::Octave, Layer::Strings, Layer::Sub}) synth.setParam(paramId(l, LayerParam::On), 0.0);
    synth.setParam(paramId(Layer::Piano, LayerParam::On), 1.0);
}

} // namespace

class TestPianoPro : public QObject
{
    Q_OBJECT

    SampleSet m_piano; // made in initTestCase
    std::atomic<const SampleSet*> m_slot{&m_piano};

    void withPiano(Synth& synth)
    {
        synth.useRecordings(Source::Grand, &m_slot);
        synth.useRecordings(Source::Upright, &m_slot);
        synth.prepare(kRate, kBlock);
    }

private slots:
    void initTestCase() { m_piano = madeUpPiano(); }

    void aRecordedNotePlaysInTuneBetweenItsRecordings()
    {
        for (const int note : {60, 64, 57, 79}) {
            Synth synth;
            withPiano(synth);
            plainPiano(synth, "Concert Grand");
            const Audio a = play(synth, {noteOn(note)}, 0.6);
            const double expected = 440.0 * std::exp2((note - 69) / 12.0);
            const double got = pitchOf(a.left, 2400);
            QVERIFY2(std::abs(got / expected - 1.0) < 0.005, qPrintable(u"note %1: %2 Hz, not %3"_s.arg(note).arg(got).arg(expected)));
        }
    }

    void harderIsLouderFromTheLouderRecordings()
    {
        const auto level = [this](int velocity) {
            Synth synth;
            withPiano(synth);
            plainPiano(synth, "Concert Grand");
            return rms(play(synth, {noteOn(60, velocity)}, 0.4).left, 2400);
        };
        QVERIFY2(level(120) > 2.0 * level(40), qPrintable(u"%1 vs %2"_s.arg(level(120)).arg(level(40))));
    }

    void aLetGoKeyRingsOutWithItsReleaseSound()
    {
        const auto after = [](SampleSet& set) {
            std::atomic<const SampleSet*> slot{&set};
            Synth synth;
            synth.useRecordings(Source::Grand, &slot);
            synth.prepare(kRate, kBlock);
            plainPiano(synth, "Concert Grand");
            (void)play(synth, {noteOn(60)}, 0.5);
            const Audio a = play(synth, {noteOff(60)}, 0.3);
            return std::pair{rms(a.left, 0, 2400), rms(a.left, static_cast<std::size_t>(0.25 * kRate))};
        };
        SampleSet with = madeUpPiano(true);
        SampleSet without = madeUpPiano(false);
        const auto [withStart, withEnd] = after(with);
        const auto [withoutStart, withoutEnd] = after(without);
        QVERIFY2(withStart > 1.2 * withoutStart, qPrintable(u"%1 vs %2"_s.arg(withStart).arg(withoutStart))); // the release sound
        QVERIFY(withoutEnd < 0.1 * withoutStart); // and it stops
    }

    void theSustainPedalHoldsNotesUntilItIsLifted()
    {
        Synth synth;
        withPiano(synth);
        plainPiano(synth, "Concert Grand");
        (void)play(synth, {pedal(true), noteOn(60)}, 0.2);
        const Audio held = play(synth, {noteOff(60)}, 0.5);
        QVERIFY(rms(held.left, static_cast<std::size_t>(0.4 * kRate)) > 0.01);
        QVERIFY(synth.holdsNotes());
        (void)play(synth, {pedal(false)}, 1.0);
        QVERIFY(!synth.holdsNotes());
        QCOMPARE(synth.soundingNotes(), 0);
    }

    void manyPedalledNotesKeepEveryLayerOfTheNewest()
    {
        // Big House Piano (piano, octave, strings) with the pedal down and
        // more keys than it plays at once: the newest note keeps all its
        // layers; the oldest notes make room (faded, not cut).
        Synth synth;
        withPiano(synth);
        synth.setParam(paramId(Macro::Type), typeValue(typeNamed("Big House Piano")));
        std::vector<MidiEvent> keys{pedal(true)};
        for (int note = 36; note < 36 + 40; ++note) keys.push_back(noteOn(note));
        const Audio a = play(synth, keys, 0.3);
        QVERIFY(allFinite(a));
        QCOMPARE(synth.layersPlaying(36 + 39), 3);
        QVERIFY2(synth.soundingNotes() <= kMaxNotes, qPrintable(QString::number(synth.soundingNotes())));
    }

    void theSynthesisedPianosPlayInTuneAndBrightenWhenStruckHarder()
    {
        for (const char* type : {"DX Piano", "Poly Keys", "Poly Brass"}) {
            const auto render = [&](int velocity) {
                Synth synth;
                synth.prepare(kRate, kBlock);
                plainPiano(synth, type);
                return play(synth, {noteOn(69, velocity)}, 0.5).left;
            };
            const std::vector<float> hard = render(120);
            const double got = periodPitchOf(hard, 14400); // after the hammer's bright moment
            // A (or, with the polysynth's sub under it, its octave down: the period that repeats).
            const double octaves = std::log2(std::max(got, 1.0) / 440.0);
            QVERIFY2(got > 100.0 && std::abs(octaves - std::round(octaves)) < 0.015,
                     qPrintable(u"%1: %2 Hz"_s.arg(QLatin1StringView(type)).arg(got)));
            const std::vector<float> soft = render(30);
            QVERIFY2(brightness(hard, 0, 4800) > 1.2 * brightness(soft, 0, 4800), type);
        }
    }

    void withoutItsRecordingsAPianoStillPlays()
    {
        Synth synth; // no recordings given: the FM piano plays
        synth.prepare(kRate, kBlock);
        plainPiano(synth, "Concert Grand");
        const Audio a = play(synth, {noteOn(60)}, 0.5);
        QVERIFY(rms(a.left) > 0.01);
        QVERIFY(allFinite(a));
    }

    void everyControlOnTheWindowChangesTheSound_data()
    {
        QTest::addColumn<uint32_t>("id");
        QTest::addColumn<double>("from");
        QTest::addColumn<double>("to");
        QTest::addColumn<bool>("tail");
        const auto row = [](const char* name, uint32_t id, double from, double to, bool tail = false) {
            QTest::newRow(name) << id << from << to << tail;
        };
        row("Tone", paramId(Macro::Tone), 1.0, 0.45);
        row("Resonance", paramId(Macro::Resonance), 0.0, 0.9);
        row("Attack", paramId(Macro::Attack), 0.0, Ranges::fromSeconds(0.3));
        row("Release", paramId(Macro::Release), Ranges::fromSeconds(0.1), Ranges::fromSeconds(2.0), true);
        row("Width", paramId(Macro::Width), 0.0, 1.0);
        row("Reverb", paramId(Macro::Reverb), 0.0, 0.8, true);
        row("Delay", paramId(Macro::Delay), 0.0, 0.7, true);
        row("Tape", paramId(Macro::Tape), 0.0, 1.0);
        row("Master", paramId(GlobalParam::Level), Ranges::fromLevelDb(-14.0), Ranges::fromLevelDb(-24.0));
        for (const Layer l : {Layer::Piano, Layer::Octave, Layer::Strings, Layer::Sub}) {
            row(qPrintable(u"%1 on"_s.arg(layerName(l))), paramId(l, LayerParam::On), 1.0, 0.0);
            row(qPrintable(u"%1 level"_s.arg(layerName(l))), paramId(l, LayerParam::Level), Ranges::fromLevelDb(-10.0), Ranges::fromLevelDb(-24.0));
        }
    }

    void everyControlOnTheWindowChangesTheSound()
    {
        QFETCH(uint32_t, id);
        QFETCH(double, from);
        QFETCH(double, to);
        QFETCH(bool, tail);
        const auto render = [&](double value) {
            Synth synth;
            withPiano(synth);
            synth.setParam(paramId(Macro::Type), typeValue(typeNamed("Big House Piano")));
            for (const Layer l : {Layer::Piano, Layer::Octave, Layer::Strings, Layer::Sub}) synth.setParam(paramId(l, LayerParam::On), 1.0);
            if (id == paramId(Macro::Resonance)) synth.setParam(paramId(Macro::Tone), 0.6); // heard at the cutoff
            synth.setParam(id, value);
            (void)play(synth, {}, 0.13); // the chord off the beat (following the notes differs from the beat)
            const std::vector<MidiEvent> chord{noteOn(48), noteOn(60), noteOn(64), noteOn(67)};
            const Audio held = play(synth, chord, 1.0);
            if (!tail) return held.left;
            return play(synth, {noteOff(48), noteOff(60), noteOff(64), noteOff(67)}, 1.5).left;
        };
        const std::vector<float> a = render(from);
        const std::vector<float> same = render(from);
        std::vector<float> nothing(a.size());
        std::ranges::transform(a, same, nothing.begin(), std::minus<>());
        QCOMPARE(rms(nothing), 0.0); // the same settings play exactly the same
        const std::vector<float> b = render(to);
        std::vector<float> difference(a.size());
        std::ranges::transform(a, b, difference.begin(), std::minus<>());
        constexpr std::size_t kTenth = 4800;
        const double overall = std::max(rms(a), rms(b));
        double ratio = 0.0;
        for (std::size_t at = 0; at + kTenth <= difference.size(); at += kTenth) {
            const double level = std::max({rms(a, at, kTenth), rms(b, at, kTenth), 0.25 * overall});
            ratio = std::max(ratio, rms(difference, at, kTenth) / level);
        }
        QVERIFY2(ratio > 0.1, qPrintable(u"%1 barely changes the sound (%2)"_s.arg(QString::fromLatin1(QTest::currentDataTag())).arg(ratio)));
    }

    void theTypesHaveTheirCommonNames()
    {
        QStringList names;
        for (const PianoType& t : pianoTypes()) names << QString::fromLatin1(t.name);
        QCOMPARE(names, (QStringList{u"Concert Grand"_s, u"Bright Grand"_s, u"Ballad Grand"_s, u"Upright Piano"_s, u"Felt Piano"_s,
                                     u"Rhodes"_s, u"Suitcase Rhodes"_s, u"Wurlitzer"_s, u"Electric Grand"_s, u"Pianet"_s, u"DX Piano"_s,
                                     u"Lo-fi Keys"_s, u"Poly Keys"_s, u"Poly Strings"_s, u"Poly Brass"_s, u"House Piano"_s,
                                     u"Big House Piano"_s, u"Prog House Piano"_s, u"Emotional Piano"_s}));
        // Every type saved before keeps its key (old songs find their piano).
        QStringList keys;
        for (const PianoType& t : pianoTypes()) keys << QString::fromLatin1(t.key);
        for (const char* old : {"house-piano", "big-house-piano", "prog-house-piano", "emotional-piano", "felt-piano", "grand-piano",
                                "upright-piano", "electric-piano", "dx-piano", "lofi-keys"}) {
            QVERIFY2(keys.contains(QLatin1StringView(old)), old);
        }
        QCOMPARE(QSet<QString>(keys.begin(), keys.end()).size(), keys.size());
    }

    // The real concert grand (when fetched): a note held with the pedal rings
    // on round its loop well past the recording, fading as a piano does, and
    // never clicks where the loop turns round.
    void aHeldNoteRingsOnRoundItsLoopWithoutAClick()
    {
        auto& library = PianoLibrary::instance();
        const auto* grand = library.request(Source::Grand, kRate);
        QTRY_VERIFY_WITH_TIMEOUT(library.state(Source::Grand, kRate) != PianoLibrary::State::Loading, 60000);
        if (grand->load() == nullptr) QSKIP("The piano pack is not fetched here");
        for (const int note : {33, 48, 60, 72, 84}) {
            Synth synth;
            synth.useRecordings(Source::Grand, grand);
            synth.prepare(kRate, kBlock);
            plainPiano(synth, "Concert Grand");
            const Audio a = play(synth, {pedal(true), noteOn(note, 90), noteOff(note)}, 12.0);
            QVERIFY(allFinite(a));
            const auto second = [&](double s) { return rms(a.left, static_cast<std::size_t>(s * kRate), static_cast<std::size_t>(kRate)); };
            // The low and middle strings: still sounding long after the
            // recording (5 s at most) ended (within 60 dB of where they
            // started), and quieter than they were. (High ones die sooner, as a piano's do.)
            if (note <= 60) {
                QVERIFY2(second(10.0) > 1e-3 * second(1.0), qPrintable(u"note %1 stopped: %2 against %3"_s.arg(note).arg(second(10.0)).arg(second(1.0))));
                QVERIFY2(second(10.0) < second(1.0), qPrintable(u"note %1 did not fade"_s.arg(note)));
            }
            // No click: no sample-to-sample jump much bigger than the note's own steepest.
            double steepest = 0.0;
            double jump = 0.0;
            const auto start = static_cast<std::size_t>(0.5 * kRate);
            for (std::size_t i = start + 1; i < a.left.size(); ++i) {
                const double d = std::abs(double{a.left.at(i)} - double{a.left.at(i - 1)});
                if (i < start + static_cast<std::size_t>(kRate)) steepest = std::max(steepest, d);
                else jump = std::max(jump, d);
            }
            QVERIFY2(jump <= steepest * 1.05, qPrintable(u"note %1: a jump of %2 (its steepest %3)"_s.arg(note).arg(jump).arg(steepest)));
        }
    }

    // With the real recordings (when fetched) and the FM pianos: every type
    // plays clean, and none is much louder than another.
    void everyTypePlaysCleanAndAboutAsLoudAsTheOthers()
    {
        auto& library = PianoLibrary::instance();
        std::array<const std::atomic<const SampleSet*>*, kSources> pianos{};
        for (int s = 0; s < kSources; ++s) pianos.at(static_cast<std::size_t>(s)) = library.request(static_cast<Source>(s), kRate);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::none_of(std::views::iota(0, kSources),
                                                      [&](int s) { return library.state(static_cast<Source>(s), kRate) == PianoLibrary::State::Loading; }),
                                 120000);
        bool recordings = true;
        for (int s = 0; s < kSources; ++s) {
            if (recorded(static_cast<Source>(s)) && pianos.at(static_cast<std::size_t>(s))->load() == nullptr) recordings = false;
        }
        std::vector<std::pair<QString, double>> levels;
        const auto types = static_cast<int>(pianoTypes().size());
        for (int t = 0; t < types; ++t) {
            const PianoType& type = pianoType(t);
            if (recorded(type.source) && !recordings) continue;
            Synth synth;
            for (int s = 0; s < kSources; ++s) {
                if (pianos.at(static_cast<std::size_t>(s)) != nullptr) synth.useRecordings(static_cast<Source>(s), pianos.at(static_cast<std::size_t>(s)));
            }
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Type), typeValue(t));
            const Audio a = play(synth, {noteOn(48, 100), noteOn(60, 100), noteOn(64, 100), noteOn(67, 100)}, 2.0);
            QVERIFY2(allFinite(a), type.name);
            QVERIFY2(peak(a) < 1.0F, type.name);
            const double db = 20.0 * std::log10(std::max(rms(a.left, static_cast<std::size_t>(0.2 * kRate)), 1e-9));
            qInfo().noquote() << u"%1: %2 dB"_s.arg(QString::fromLatin1(type.name), -20).arg(db, 0, 'f', 1);
            levels.emplace_back(QString::fromLatin1(type.name), db);
        }
        if (!recordings) qInfo("(The recorded pianos are not fetched here: their types were not measured.)");
        QVERIFY(!levels.empty());
        for (const auto& [name, db] : levels) QVERIFY2(std::abs(db - (-16.0)) < 3.0, qPrintable(u"%1: %2 dB"_s.arg(name).arg(db)));
    }

    void itIsAnInstrumentThatSavesAndLoadsItsSettings()
    {
        auto loaded = PluginNode::load(QString::fromLatin1(PianoProNode::kPluginId), kRate, kBlock);
        QVERIFY(loaded.has_value());
        auto* node = dynamic_cast<PianoProNode*>(loaded->get());
        QVERIFY(node != nullptr);
        QVERIFY(node->isInstrument());
        QStringList names;
        for (const auto& p : node->parameters()) names << p.name;
        // (OTT, Enhance and the sidechain are effects of their own: not here.)
        QCOMPARE(names, (QStringList{u"Type"_s, u"Tone"_s, u"Resonance"_s, u"Attack"_s, u"Release"_s, u"Width"_s, u"Reverb"_s,
                                     u"Delay"_s, u"Tape"_s, u"Piano On"_s, u"Piano Level"_s,
                                     u"Octave On"_s, u"Octave Level"_s, u"Strings On"_s, u"Strings Level"_s, u"Sub On"_s,
                                     u"Sub Level"_s, u"Level"_s}));
        QCOMPARE(node->parameters().front().choices.size(), static_cast<qsizetype>(pianoTypes().size()));
        const uint32_t type = paramId(Macro::Type);
        const uint32_t tone = paramId(Macro::Tone);
        QVERIFY(node->setParameter(type, typeValue(typeNamed("Felt Piano"))).has_value());
        QVERIFY(node->setParameter(tone, 0.6).has_value());
        QVERIFY(node->takeEdited());
        QCOMPARE(node->takeTouchedParameter(), std::optional<uint32_t>(tone));
        QVERIFY(!node->setParameter(9999, 0.5).has_value());
        const auto saved = node->saveEncodedState();
        QVERIFY(saved.has_value());
        auto other = PluginNode::load(QString::fromLatin1(PianoProNode::kPluginId), kRate, kBlock);
        QVERIFY(other.has_value());
        QVERIFY((*other)->restoreEncodedState(*saved).has_value());
        QCOMPARE(typeIndex((*other)->parameterValue(type)), typeNamed("Felt Piano"));
        QVERIFY(std::abs((*other)->parameterValue(tone) - 0.6) < 1e-6);
        QVERIFY(!(*other)->takeEdited());
        QVERIFY(!(*other)->restoreEncodedState(QByteArray("GCS1garbage")).has_value());
        // The window's readings.
        const double recordingsState = node->parameterValue(kRecordingsParameter);
        QVERIFY(recordingsState == 0.0 || recordingsState == 0.5 || recordingsState == 1.0);
        QVERIFY(node->parameterValue(sidechain::kPositionParameter) >= 0.0);
        // The synthesised types need no recordings.
        QCOMPARE(node->parameterValue(kSourceParameter), 1.0 / (kSources - 1)); // Felt: the upright
        QVERIFY(node->setParameter(type, typeValue(typeNamed("DX Piano"))).has_value());
        QCOMPARE(node->parameterValue(kRecordingsParameter), 1.0);
        QCOMPARE(node->parameterValue(kSourceParameter), 6.0 / (kSources - 1));
    }

    void aPianoIsReadOnceAndSharedAndAMissingOneSaysWhy()
    {
        QTemporaryDir empty;
        qputenv("GIGCHAIN_PIANO_SAMPLES", empty.path().toUtf8());
        auto& library = PianoLibrary::instance();
        constexpr double kOddRate = 22050.0; // a rate no other test asked for
        const auto* first = library.request(Source::Upright, kOddRate);
        QCOMPARE(library.request(Source::Upright, kOddRate), first); // the same piano, not read twice
        QVERIFY(library.request(Source::Dx, kOddRate) == nullptr);
        QVERIFY(library.request(Source::Poly, kOddRate) == nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(library.state(Source::Upright, kOddRate), PianoLibrary::State::Failed, 10000);
        QVERIFY2(library.problem(Source::Upright, kOddRate).contains(u"Upright.sfz"_s), qPrintable(library.problem(Source::Upright, kOddRate)));
        QVERIFY(first->load() == nullptr);
        qunsetenv("GIGCHAIN_PIANO_SAMPLES");
    }

    void onlyThePianoTheTypeNeedsIsReadAndAKnobChangeReadsItsPiano()
    {
        QTemporaryDir empty;
        qputenv("GIGCHAIN_PIANO_SAMPLES", empty.path().toUtf8()); // (fails fast: nothing there)
        const auto& library = PianoLibrary::instance();
        constexpr double kOddRate = 32000.0; // a rate no other test asks for
        auto loaded = PluginNode::load(QString::fromLatin1(PianoProNode::kPluginId), kOddRate, kBlock);
        QVERIFY(loaded.has_value());
        auto* node = dynamic_cast<PianoProNode*>(loaded->get());
        QVERIFY(node != nullptr);
        // Made, then its saved type (Felt: the upright) restored: the grand is never read.
        auto felt = PluginNode::load(QString::fromLatin1(PianoProNode::kPluginId), 48000.0, kBlock);
        QVERIFY((*felt)->setParameter(paramId(Macro::Type), typeValue(typeNamed("Felt Piano"))).has_value());
        const auto saved = (*felt)->saveEncodedState();
        QVERIFY(saved.has_value());
        QVERIFY(node->restoreEncodedState(*saved).has_value());
        node->serviceMainThread();
        QCOMPARE(library.state(Source::Grand, kOddRate), PianoLibrary::State::NotAsked);
        QVERIFY(library.state(Source::Upright, kOddRate) != PianoLibrary::State::NotAsked);
        // A type changed from a keyboard knob (the audio thread): its piano is asked for next.
        node->queueParameter(paramId(Macro::Type), typeValue(typeNamed("Concert Grand")), 0);
        QCOMPARE(library.state(Source::Grand, kOddRate), PianoLibrary::State::NotAsked);
        QCOMPARE(node->parameterValue(kRecordingsParameter), 0.5); // shown as on its way
        node->serviceMainThread();
        QVERIFY(library.state(Source::Grand, kOddRate) != PianoLibrary::State::NotAsked);
        QTRY_COMPARE_WITH_TIMEOUT(library.state(Source::Grand, kOddRate), PianoLibrary::State::Failed, 10000);
        qunsetenv("GIGCHAIN_PIANO_SAMPLES");
    }

    // Feature video 7's sound, scene by scene (its script.md), with the real
    // recorded pianos: made only when GIGCHAIN_VIDEO_SOUNDS names the folder
    // to write sound_NN.wav into.
    void renderVideoSounds()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_VIDEO_SOUNDS");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_VIDEO_SOUNDS to a folder to make the video's sounds");
        auto& library = PianoLibrary::instance();
        const auto* grand = library.request(Source::Grand, video::kRate);
        const auto* upright = library.request(Source::Upright, video::kRate);
        QTRY_VERIFY_WITH_TIMEOUT(library.state(Source::Grand, video::kRate) != PianoLibrary::State::Loading
                                     && library.state(Source::Upright, video::kRate) != PianoLibrary::State::Loading,
                                 120000);
        QVERIFY2(grand->load() != nullptr && upright->load() != nullptr, "the recorded pianos are needed (fetch the samples)");
        using video::Take;
        const auto type = [](const char* name) { return typeValue(typeNamed(name)); };
        const uint32_t typeId = paramId(Macro::Type);
        // A minor: Am, F, C, G: a left-hand root and right-hand chord per bar.
        const std::vector<int> roots{45, 41, 48, 43};
        const std::vector<std::vector<int>> hands{{60, 64, 69}, {60, 65, 69}, {60, 64, 67}, {59, 62, 67}};
        const auto houseRiff = [&](Take& t, double from, int bars) {
            for (int bar = 0; bar < bars; ++bar) {
                const auto c = static_cast<std::size_t>(bar) % roots.size();
                const double start = from + (bar * 4.0);
                t.notes.push_back({.beat = start, .key = roots.at(c), .velocity = 105, .length = 3.6});
                for (const double hit : {0.0, 0.75, 1.5, 2.5, 3.25}) {
                    for (const int key : hands.at(c)) t.notes.push_back({.beat = start + hit, .key = key, .velocity = 112, .length = 0.35});
                }
            }
        };
        const auto heldChords = [&](Take& t, double from, int bars, double beatsEach, int velocity) {
            for (int bar = 0; bar < bars; ++bar) {
                const auto c = static_cast<std::size_t>(bar) % roots.size();
                const double start = from + (bar * beatsEach);
                t.notes.push_back({.beat = start, .key = roots.at(c), .velocity = velocity, .length = beatsEach - 0.1});
                for (const int key : hands.at(c)) t.notes.push_back({.beat = start, .key = key, .velocity = velocity, .length = beatsEach - 0.1});
            }
        };
        const auto phrase = [](Take& t, double from, int velocity) {
            const std::vector<std::array<double, 3>> melody{{0, 72, .5}, {.5, 74, .5}, {1, 76, 1}, {2, 79, .5}, {2.5, 77, .5},
                                                            {3, 76, 1}, {4, 74, 1}, {5, 72, 2.8}};
            for (const auto& [beat, key, length] : melody) {
                t.notes.push_back({.beat = from + beat, .key = static_cast<int>(key), .velocity = velocity, .length = length - 0.05});
            }
            t.notes.push_back({.beat = from, .key = 48, .velocity = velocity - 10, .length = 7.8});
            t.notes.push_back({.beat = from + 4, .key = 43, .velocity = velocity - 10, .length = 3.8});
        };
        // What changes in a scene's sound changes on the word that names it
        // (the narration's timing, subs.json); each sound runs past its scene's end.
        const video::Cues cues(folder);
        QVERIFY2(cues.loaded(), "the video's subs.json (make_subs.py) is needed: its words time the sound");
        const auto at = [&](const char* scene, const char* word, double fallback, int nth = 1) {
            return cues.beatOf(QString::fromLatin1(scene), QString::fromLatin1(word), fallback, nth);
        };
        const auto takeFor = [&](const char* scene) { return Take{cues.beatsOf(QString::fromLatin1(scene), 36)}; };
        const auto bars = [](const Take& t) { return static_cast<int>(std::ceil(t.beats / 4.0)); };
        // A melody every two bars, from `from` until `until`.
        const auto phrases = [&](Take& t, double from, double until, int velocity) {
            for (int n = 0; from + (n * 8.0) + 6 <= until; ++n) phrase(t, from + (n * 8.0), velocity);
        };
        std::vector<std::pair<int, Take>> scenes;
        { // 01: Big House Piano, the riff
            Take t = takeFor("01");
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, bars(t));
            scenes.emplace_back(1, t);
        }
        { // 02: the grand, then "a real upright"
            Take t = takeFor("02");
            const double toUpright = at("02", "upright", 16);
            t.add({0, typeId, type("Concert Grand")});
            phrases(t, 0, toUpright, 85);
            t.add({toUpright, typeId, type("Upright Piano")});
            phrases(t, toUpright, t.beats, 85);
            scenes.emplace_back(2, t);
        }
        { // 03: "play softly", "hit it hard", "the sustain pedal"
            Take t = takeFor("03");
            const double soft = at("03", "softly", 6);
            const double hard = at("03", "hard", 12);
            const double pedal = at("03", "pedal", 24);
            t.add({0, typeId, type("Concert Grand")});
            chord(t, 0, {48, 60, 64, 67}, 4, 75);
            chord(t, soft, {48, 60, 64, 67}, hard - soft - 1, 35);
            chord(t, hard, {36, 48, 55, 64, 67, 72}, 3, 125);
            t.add({pedal, true});
            heldChords(t, pedal, std::max(1, static_cast<int>((t.beats - pedal - 3) / 2)), 2.0, 80);
            scenes.emplace_back(3, t);
        }
        { // 04: House Piano
            Take t = takeFor("04");
            t.add({0, typeId, type("House Piano")});
            houseRiff(t, 0, bars(t));
            scenes.emplace_back(4, t);
        }
        { // 05: Big House Piano
            Take t = takeFor("05");
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, bars(t));
            scenes.emplace_back(5, t);
        }
        { // 06: Prog House Piano
            Take t = takeFor("06");
            t.add({0, typeId, type("Prog House Piano")});
            houseRiff(t, 0, bars(t));
            scenes.emplace_back(6, t);
        }
        { // 07: Emotional Piano's slow held chords, then "felt piano"'s quiet melody
            Take t = takeFor("07");
            const double felt = at("07", "felt", 20);
            t.add({0, typeId, type("Emotional Piano")});
            t.add({0, true});
            heldChords(t, 0, std::max(1, static_cast<int>((felt - 1) / 8)), 8.0, 75);
            t.add({felt - 1, false});
            t.add({felt, typeId, type("Felt Piano")});
            phrases(t, felt, t.beats, 60);
            scenes.emplace_back(7, t);
        }
        { // 08: Electric Piano, soft, then "the harder you play"
            Take t = takeFor("08");
            const double harder = at("08", "harder", 16);
            t.add({0, typeId, type("Rhodes")});
            const int softBars = static_cast<int>(harder / 4);
            const int hardBars = static_cast<int>((t.beats - harder) / 4);
            heldChords(t, 0, softBars, 4.0, 45);
            heldChords(t, harder, hardBars, 4.0, 120);
            scenes.emplace_back(8, t);
        }
        { // 09: DX Piano, then "Lo-fi Keys"
            Take t = takeFor("09");
            const double lofi = at("09", "lo", 10);
            t.add({0, typeId, type("DX Piano")});
            heldChords(t, 0, bars(t), 4.0, 100);
            t.add({lofi, typeId, type("Lo-fi Keys")});
            scenes.emplace_back(9, t);
        }
        { // 10: House Piano plain, then the octave, the strings and the sub, each on its name
            Take t = takeFor("10");
            t.add({0, typeId, type("House Piano")});
            for (const Layer l : {Layer::Octave, Layer::Strings, Layer::Sub}) t.add({0, paramId(l, LayerParam::On), 0.0});
            houseRiff(t, 0, bars(t));
            t.add({at("10", "octave", 8), paramId(Layer::Octave, LayerParam::On), 1.0});
            t.add({at("10", "strings", 12), paramId(Layer::Strings, LayerParam::On), 1.0});
            t.add({at("10", "sub", 16), paramId(Layer::Sub, LayerParam::On), 1.0});
            scenes.emplace_back(10, t);
        }
        { // 11: Felt Piano, tape off, then on at "tape"
            Take t = takeFor("11");
            t.add({0, typeId, type("Felt Piano")});
            t.add({0, paramId(Macro::Tape), 0.0});
            phrases(t, 0, t.beats, 60);
            t.add({at("11", "tape", 20), paramId(Macro::Tape), 0.85});
            scenes.emplace_back(11, t);
        }
        { // 12: House Piano pumping at 1/4, then 1/8D at "timings"
            Take t = takeFor("12");
            t.add({0, typeId, type("House Piano")});
            t.add({0, paramId(Macro::Sidechain), 0.75});
            const auto rate = [](int index) { return Ranges::fromChoice(index, static_cast<int>(sidechain::kRates.size())); };
            t.add({0, paramId(Macro::SidechainRate), rate(5)});
            heldChords(t, 0, bars(t), 4.0, 105);
            t.add({at("12", "timings", 12), paramId(Macro::SidechainRate), rate(4)});
            scenes.emplace_back(12, t);
        }
        { // 13: the riff again (the video fades it out)
            Take t = takeFor("13");
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, bars(t));
            scenes.emplace_back(13, t);
        }
        for (const auto& [number, take] : scenes) {
            Synth synth;
            synth.useRecordings(Source::Grand, grand);
            synth.useRecordings(Source::Upright, upright);
            synth.prepare(video::kRate, video::kBlock);
            const std::vector<float> sound = video::render(synth, take);
            const QString path = u"%1/sound_%2.wav"_s.arg(folder).arg(number, 2, 10, QLatin1Char('0'));
            QVERIFY2(video::writeWav(path, sound), qPrintable(path));
            qInfo().noquote() << path << u"%1 s"_s.arg(static_cast<double>(sound.size()) / 2.0 / video::kRate, 0, 'f', 1);
        }
    }

    // Feature video 12's sound (its plan.json, from make_plan.py): each
    // scene's instrument (Piano Pro, Saw Stack Pro or Pads Pro) through its
    // real engine, then the app's own effects, playing C G Am F (a bar each)
    // in the part its preset plays, presets, knobs and effects changing on
    // the plan's beats. Made only when GIGCHAIN_VIDEO12 names the folder.
    void renderSoundUpdateVideo()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_VIDEO12");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_VIDEO12 to video 12's folder to make its sounds");
        QFile planFile(folder + u"/plan.json"_s);
        QVERIFY2(planFile.open(QIODevice::ReadOnly), qPrintable(planFile.errorString()));
        const QJsonObject plan = QJsonDocument::fromJson(planFile.readAll()).object();
        const double tempo = plan.value(u"tempo"_s).toDouble(124.0);
        const QJsonObject scenes = plan.value(u"scenes"_s).toObject();
        QVERIFY(!scenes.isEmpty());
        const QStringList only = qEnvironmentVariable("GIGCHAIN_DEMO_SCENES").split(u',', Qt::SkipEmptyParts);
        // Every recorded piano read first (a preset changed mid-scene plays its recordings at once).
        auto& library = PianoLibrary::instance();
        for (int s = 0; s < kSources; ++s) (void)library.request(static_cast<Source>(s), video::kRate);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::none_of(std::views::iota(0, kSources),
                                                      [&](int s) { return library.state(static_cast<Source>(s), video::kRate) == PianoLibrary::State::Loading; }),
                                 180000);

        // C, G, Am, F: the bass note and the chord, voiced close.
        struct Chord
        {
            constexpr Chord(int bassKey, std::array<int, 3> keys) : bass(bassKey), triad(keys) {}
            int bass = 0;
            std::array<int, 3> triad{};
        };
        constexpr std::array<Chord, 4> kProgression{
            {Chord(48, {60, 64, 67}), Chord(43, {59, 62, 67}), Chord(45, {60, 64, 69}), Chord(41, {60, 65, 69})}};
        struct Hit
        {
            Hit(double atBeat, int note, int strength, double beats) : beat(atBeat), key(note), velocity(strength), length(beats) {}
            double beat = 0.0;
            int key = 60;
            int velocity = 100;
            double length = 1.0;
        };
        // What a part plays in one bar of `c`, from the bar's start.
        const auto pattern = [](const QString& part, const Chord& c) {
            std::vector<Hit> hits;
            const auto chordAt = [&](double beat, double length, int velocity, bool bass) {
                std::ranges::transform(c.triad, std::back_inserter(hits), [&](int k) { return Hit(beat, k, velocity, length); });
                if (bass) hits.emplace_back(beat, c.bass - 12, velocity, length);
            };
            const std::array<int, 3>& t = c.triad;
            if (part == u"keys") { // a ballad: the bass and the chord, the chord again on three
                hits.emplace_back(0.0, c.bass - 12, 84, 3.9);
                hits.emplace_back(0.0, c.bass, 76, 3.9);
                chordAt(0.0, 1.9, 80, false);
                chordAt(2.0, 1.9, 68, false);
                hits.emplace_back(3.0, t.at(2) + 12, 60, 0.9);
            } else if (part == u"house") { // stabs off the beat
                for (const double b : {0.0, 1.5, 3.0}) chordAt(b, 0.55, 100, false);
                hits.emplace_back(0.0, c.bass - 12, 96, 1.4);
            } else if (part == u"stabs") {
                for (const double b : {0.0, 1.5, 3.0}) chordAt(b, 0.9, 92, false);
                hits.emplace_back(0.0, c.bass - 12, 88, 3.8);
            } else if (part == u"pad") {
                chordAt(0.0, 4.05, 88, true);
            } else if (part == u"chords") { // the EDM chord rhythm
                chordAt(0.0, 1.3, 104, true);
                chordAt(1.5, 0.4, 96, false);
                chordAt(2.5, 1.3, 100, true);
            } else if (part == u"lead") {
                const std::array<std::pair<double, int>, 6> line{{{0.0, t.at(2)}, {1.0, t.at(1)}, {1.5, t.at(0)}, {2.0, t.at(1)}, {3.0, t.at(2)}, {3.5, t.at(1)}}};
                for (const auto& [b, k] : line) hits.emplace_back(b, k + 12, 104, b == 0.0 || b == 2.0 ? 0.95 : 0.45);
            } else if (part == u"pluck") {
                const std::array<int, 8> arp{t.at(0), t.at(1), t.at(2), t.at(0) + 12, t.at(2), t.at(1), t.at(0) + 12, t.at(2)};
                for (std::size_t i = 0; i < arp.size(); ++i) hits.emplace_back(0.5 * static_cast<double>(i), arp.at(i) + 12, 96, 0.4);
            } else { // bass
                hits.emplace_back(0.0, c.bass - 12, 110, 3.8);
            }
            return hits;
        };

        for (const QString& key : scenes.keys()) {
            if (!only.isEmpty() && !only.contains(key)) continue;
            const QJsonObject scene = scenes.value(key).toObject();
            const QString instrument = scene.value(u"instrument"_s).toString();
            const QString pluginId = instrument == u"piano" ? QString::fromLatin1(PianoProNode::kPluginId)
                                     : instrument == u"saw" ? u"builtin:saw-stack-pro"_s
                                                             : u"builtin:pads-pro"_s;
            auto loaded = PluginNode::load(pluginId, video::kRate, video::kBlock);
            QVERIFY2(loaded.has_value(), qPrintable(pluginId));
            const std::shared_ptr<PluginNode> node = *loaded;
            const auto params = node->parameters();
            const auto idOf = [&](const QString& name) -> uint32_t {
                const auto found = std::ranges::find_if(params, [&](const PluginNode::Parameter& p) { return p.name == name; });
                if (found == params.end()) qFatal("%s has no %s", qPrintable(pluginId), qPrintable(name));
                return found->id;
            };
            const QStringList types = params.front().choices;
            // The scene's effects, in order, from where it starts them.
            std::vector<std::pair<QString, std::shared_ptr<BuiltinEffectNode>>> chain;
            const auto effectSetting = [](const QString& effect, const QString& name) -> uint32_t {
                if (effect == u"squash") return name == u"Depth" ? OttEffectNode::Depth : OttEffectNode::Time;
                if (effect == u"enhance") return name == u"Amount" ? EnhanceEffectNode::Amount : EnhanceEffectNode::Mode;
                if (name == u"Depth") return SidechainEffectNode::Depth;
                if (name == u"Rate") return SidechainEffectNode::Rate;
                if (name == u"Curve") return SidechainEffectNode::Curve;
                if (name == u"Slope") return SidechainEffectNode::Slope;
                return SidechainEffectNode::Trigger;
            };
            for (const auto& v : scene.value(u"fx"_s).toArray()) {
                const QJsonObject e = v.toObject();
                const QString name = e.value(u"fx"_s).toString();
                std::shared_ptr<BuiltinEffectNode> fx;
                if (name == u"squash") fx = OttEffectNode::create(video::kRate, video::kBlock);
                if (name == u"enhance") fx = EnhanceEffectNode::create(video::kRate, video::kBlock);
                if (name == u"sidechain") fx = SidechainEffectNode::create(video::kRate, video::kBlock);
                QVERIFY(fx != nullptr);
                for (auto it = e.begin(); it != e.end(); ++it) {
                    if (it.key() != u"fx") QVERIFY(fx->setParameter(effectSetting(name, it.key()), it.value().toDouble()).has_value());
                }
                chain.emplace_back(name, fx);
            }
            const auto effectNamed = [&](const QString& name) -> BuiltinEffectNode* {
                const auto found = std::ranges::find_if(chain, [&](const auto& f) { return f.first == name; });
                return found != chain.end() ? found->second.get() : nullptr;
            };

            // The plan's changes, and what part plays from when.
            const QJsonArray events = scene.value(u"events"_s).toArray();
            const double beats = scene.value(u"beats"_s).toDouble();
            std::vector<std::pair<double, QString>> parts;
            for (const auto& v : events) {
                const QJsonObject e = v.toObject();
                if (e.contains(u"part"_s)) parts.emplace_back(e.value(u"beat"_s).toDouble(), e.value(u"part"_s).toString());
            }
            const auto partAt = [&](double beat) {
                QString part = parts.front().second;
                for (const auto& [from, p] : parts) {
                    if (from <= beat + 1e-9) part = p;
                }
                return part;
            };
            // Every note: each bar's pattern for the part playing then (a part
            // changed mid-bar plays its own pattern from there).
            std::vector<Hit> notes;
            // (Playing on past the scene's end: the crossfade into the next scene carries music, not a dying tail.)
            for (int bar = 0; bar * 4 < static_cast<int>(beats) + 2; ++bar) {
                const double start = bar * 4.0;
                const Chord& chord = kProgression.at(static_cast<std::size_t>(bar % 4));
                std::vector<double> cuts{start};
                for (const auto& [from, p] : parts) {
                    if (from > start && from < start + 4.0) cuts.push_back(from);
                }
                cuts.push_back(start + 4.0);
                for (std::size_t s = 0; s + 1 < cuts.size(); ++s) {
                    for (const Hit& h : pattern(partAt(cuts.at(s)), chord)) {
                        const double at = start + h.beat;
                        if (at >= cuts.at(s) - 1e-9 && at < cuts.at(s + 1) - 1e-9 && at < beats + 2.0) notes.emplace_back(at, h.key, h.velocity, h.length);
                    }
                }
            }
            // As MIDI, with the pedal for the keys (down each bar, up just before the next).
            struct Event
            {
                Event(int64_t when, MidiEvent event) : at(when), midi(event) {}
                int64_t at = 0;
                MidiEvent midi{};
            };
            const double samplesPerBeat = video::kRate * 60.0 / tempo;
            const auto when = [&](double beat) { return static_cast<int64_t>(beat * samplesPerBeat); };
            std::vector<Event> midi;
            for (const Hit& h : notes) {
                midi.push_back({when(h.beat), {.status = 0x90, .data1 = static_cast<uint8_t>(h.key), .data2 = static_cast<uint8_t>(h.velocity)}});
                midi.push_back({when(h.beat + h.length), {.status = 0x80, .data1 = static_cast<uint8_t>(h.key), .data2 = 0}});
            }
            for (int bar = 0; bar * 4 < static_cast<int>(beats); ++bar) {
                if (partAt(bar * 4.0) != u"keys") continue;
                midi.push_back({when(bar * 4.0 + 0.05), {.status = 0xB0, .data1 = 64, .data2 = 127}});
                midi.push_back({when(bar * 4.0 + 3.92), {.status = 0xB0, .data1 = 64, .data2 = 0}});
            }
            std::ranges::stable_sort(midi, {}, &Event::at);

            // Played: the plan's changes on their beats, a sweep step by step.
            struct Sweep
            {
                Sweep(double fromBeat, double toBeat, std::function<void(double)> setter, double first, double last)
                    : from(fromBeat), to(toBeat), set(std::move(setter)), start(first), end(last)
                {
                }
                double from = 0.0, to = 0.0;
                std::function<void(double)> set;
                double start = 0.0, end = 0.0;
            };
            std::vector<Sweep> sweeps;
            std::size_t nextEvent = 0;
            const auto apply = [&](const QJsonObject& e) {
                const double beat = e.value(u"beat"_s).toDouble();
                if (e.contains(u"type"_s)) {
                    const auto index = types.indexOf(e.value(u"type"_s).toString());
                    QVERIFY2(index >= 0, qPrintable(e.value(u"type"_s).toString()));
                    QVERIFY(node->setParameter(idOf(u"Type"_s), static_cast<double>(index) / static_cast<double>(types.size() - 1)).has_value());
                    node->serviceMainThread();
                    // (A piano's pedalled chords stack up: 8 dB of room under the top, the mix's levels set afterwards.)
                    if (instrument == u"piano") QVERIFY(node->setParameter(idOf(u"Level"_s), Ranges::fromLevelDb(-22.0)).has_value());
                }
                if (e.contains(u"knob"_s)) QVERIFY(node->setParameter(idOf(e.value(u"knob"_s).toString()), e.value(u"value"_s).toDouble()).has_value());
                if (e.contains(u"sweep"_s)) {
                    const uint32_t id = idOf(e.value(u"sweep"_s).toString());
                    sweeps.emplace_back(beat, beat + e.value(u"beats"_s).toDouble(), [&node, id](double v) { (void)node->setParameter(id, v); },
                                        e.value(u"from"_s).toDouble(), e.value(u"to"_s).toDouble());
                }
                if (e.contains(u"layer"_s)) {
                    const auto id = sawstack::paramId(e.value(u"layer"_s).toInt(), sawstack::LayerParam::On);
                    QVERIFY(node->setParameter(id, e.value(u"on"_s).toDouble()).has_value());
                }
                if (e.contains(u"fx"_s)) {
                    BuiltinEffectNode* fx = effectNamed(e.value(u"fx"_s).toString());
                    QVERIFY2(fx != nullptr, qPrintable(e.value(u"fx"_s).toString()));
                    const uint32_t id = effectSetting(e.value(u"fx"_s).toString(), e.value(u"param"_s).toString());
                    if (e.contains(u"sweepTo"_s)) {
                        sweeps.emplace_back(beat, beat + e.value(u"beats"_s).toDouble(), [fx, id](double v) { (void)fx->setParameter(id, v); },
                                            fx->parameterValue(id), e.value(u"sweepTo"_s).toDouble());
                    } else {
                        QVERIFY(fx->setParameter(id, e.value(u"value"_s).toDouble()).has_value());
                    }
                }
            };
            // (Four beats past the scene: its tail ringing on under the next scene's start, for the crossfade.)
            const auto total = static_cast<int64_t>((beats + 4.0) * samplesPerBeat);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total) * 2);
            std::vector<float> left(video::kBlock);
            std::vector<float> right(video::kBlock);
            std::vector<MidiEvent> block;
            std::size_t nextMidi = 0;
            for (int64_t at = 0; at < total; at += video::kBlock) {
                const int frames = static_cast<int>(std::min<int64_t>(video::kBlock, total - at));
                const double beat = static_cast<double>(at) / samplesPerBeat;
                for (; std::cmp_less(nextEvent, events.size()) && when(events.at(static_cast<qsizetype>(nextEvent)).toObject().value(u"beat"_s).toDouble()) < at + frames;
                     ++nextEvent) {
                    apply(events.at(static_cast<qsizetype>(nextEvent)).toObject());
                    if (QTest::currentTestFailed()) return;
                }
                for (const Sweep& s : sweeps) {
                    if (beat >= s.from && beat <= s.to) s.set(s.start + ((s.end - s.start) * (beat - s.from) / std::max(s.to - s.from, 1e-9)));
                }
                block.clear();
                for (; nextMidi < midi.size() && midi.at(nextMidi).at < at + frames; ++nextMidi) {
                    MidiEvent e = midi.at(nextMidi).midi;
                    e.sampleOffset = static_cast<int32_t>(std::max<int64_t>(midi.at(nextMidi).at - at, 0));
                    block.push_back(e);
                }
                const TimeInfo time{.tempo = tempo, .sampleRate = video::kRate, .samplePosition = at, .ppqPosition = beat};
                const AudioBlock io{.left = left.data(), .right = right.data(), .frames = frames};
                node->process(block, io, time);
                for (const auto& [name, fx] : chain) fx->process(block, io, time);
                for (int i = 0; i < frames; ++i) {
                    out.push_back(left.at(static_cast<std::size_t>(i)));
                    out.push_back(right.at(static_cast<std::size_t>(i)));
                }
            }
            // Under the top (a squashed stack comes out above it; the mix sets its level anyway).
            const float peak = std::accumulate(out.begin(), out.end(), 0.0F, [](float most, float s) { return std::max(most, std::abs(s)); });
            if (peak > 0.9F) {
                const float scale = 0.9F / peak;
                std::ranges::transform(out, out.begin(), [scale](float s) { return s * scale; });
            }
            const QString path = folder + u"/sound_"_s + key + u".wav"_s;
            QVERIFY2(video::writeWav(path, out), qPrintable(path));
            qInfo().noquote() << path << u"%1 s, peak %2"_s.arg(static_cast<double>(out.size()) / 2.0 / video::kRate, 0, 'f', 1).arg(peak, 0, 'f', 2);
        }
    }

    void itIsInTheInstrumentList()
    {
        const auto builtins = PluginNode::builtins();
        QVERIFY(std::ranges::any_of(builtins, [](const PluginInfo& p) { return p.id == QLatin1StringView(PianoProNode::kPluginId); }));
    }
};

QTEST_GUILESS_MAIN(TestPianoPro)
#include "tst_piano_pro.moc"
