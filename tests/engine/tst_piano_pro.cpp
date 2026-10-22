// Piano Pro, the app's own EDM piano: its recorded pianos play in tune
// between their recordings, harder is louder (and from the louder
// recordings), let-go keys ring out with their release sound, the pedal
// holds; its FM electric pianos play in tune and brighten when struck
// harder; without its recordings it still plays (its FM piano); every control
// on its window changes the sound; its types are named as producers name
// them and sit at about the same loudness; it saves and loads.
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

#include <cmath>
#include <numbers>
#include <numeric>
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

// How bright: the level of the sound's changes against its level.
double brightness(const std::vector<float>& s, std::size_t from, std::size_t count)
{
    std::vector<float> change;
    for (std::size_t i = from + 1; i < std::min(s.size(), from + count); ++i) change.push_back(s.at(i) - s.at(i - 1));
    return rms(change) / std::max(rms(s, from, count), 1e-12);
}

// The level VCSL's recordings come in at (about -24 dB, which Piano Pro makes up).
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
            plainPiano(synth, "Grand Piano");
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
            plainPiano(synth, "Grand Piano");
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
            plainPiano(synth, "Grand Piano");
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
        plainPiano(synth, "Grand Piano");
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

    void theElectricPianosPlayInTuneAndBrightenWhenStruckHarder()
    {
        for (const char* type : {"Electric Piano", "DX Piano"}) {
            const auto render = [&](int velocity) {
                Synth synth;
                synth.prepare(kRate, kBlock);
                plainPiano(synth, type);
                return play(synth, {noteOn(69, velocity)}, 0.5).left;
            };
            const std::vector<float> hard = render(120);
            const double got = pitchOf(hard, 14400); // after the hammer's bright moment

            QVERIFY2(std::abs(got / 440.0 - 1.0) < 0.01, qPrintable(u"%1: %2 Hz"_s.arg(QLatin1StringView(type)).arg(got)));
            const std::vector<float> soft = render(30);
            QVERIFY2(brightness(hard, 0, 4800) > 1.2 * brightness(soft, 0, 4800), type);
        }
    }

    void withoutItsRecordingsAPianoStillPlays()
    {
        Synth synth; // no recordings given: the FM piano plays
        synth.prepare(kRate, kBlock);
        plainPiano(synth, "Grand Piano");
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
        row("OTT", paramId(Macro::Ott), 0.0, 1.0);
        row("Tape", paramId(Macro::Tape), 0.0, 1.0);
        row("Sidechain", paramId(Macro::Sidechain), 0.0, 1.0);
        row("Sidechain Rate", paramId(Macro::SidechainRate), Ranges::fromChoice(5, 9), 0.0);
        row("Sidechain Slope", paramId(Macro::SidechainSlope), 0.5, 1.0);
        row("Sidechain Curve", paramId(Macro::SidechainCurve), 0.0, 1.0);
        row("Sidechain Trigger", paramId(Macro::SidechainTrigger), 0.0, 1.0);
        row("Sidechain On", paramId(Macro::SidechainOn), 1.0, 0.0);
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
            synth.setParam(paramId(Macro::Sidechain), 1.0);
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
        QCOMPARE(names, (QStringList{u"House Piano"_s, u"Big House Piano"_s, u"Prog House Piano"_s, u"Emotional Piano"_s, u"Felt Piano"_s,
                                     u"Grand Piano"_s, u"Upright Piano"_s, u"Electric Piano"_s, u"DX Piano"_s, u"Lo-fi Keys"_s}));
    }

    // With the real recordings (when fetched) and the FM pianos: every type
    // plays clean, and none is much louder than another.
    void everyTypePlaysCleanAndAboutAsLoudAsTheOthers()
    {
        auto& library = PianoLibrary::instance();
        const auto* grand = library.request(Source::Grand, kRate);
        const auto* upright = library.request(Source::Upright, kRate);
        QTRY_VERIFY_WITH_TIMEOUT(library.state(Source::Grand, kRate) != PianoLibrary::State::Loading
                                     && library.state(Source::Upright, kRate) != PianoLibrary::State::Loading,
                                 60000);
        const bool recordings = grand->load() != nullptr && upright->load() != nullptr;
        std::vector<std::pair<QString, double>> levels;
        const auto types = static_cast<int>(pianoTypes().size());
        for (int t = 0; t < types; ++t) {
            const PianoType& type = pianoType(t);
            if (recorded(type.source) && !recordings) continue;
            Synth synth;
            synth.useRecordings(Source::Grand, grand);
            synth.useRecordings(Source::Upright, upright);
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
        QCOMPARE(names, (QStringList{u"Type"_s, u"Tone"_s, u"Resonance"_s, u"Attack"_s, u"Release"_s, u"Width"_s, u"Reverb"_s,
                                     u"Delay"_s, u"OTT"_s, u"Tape"_s, u"Sidechain"_s, u"Sidechain Rate"_s, u"Sidechain Slope"_s,
                                     u"Sidechain Curve"_s, u"Sidechain Trigger"_s, u"Sidechain On"_s, u"Piano On"_s, u"Piano Level"_s,
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
        // The electric types need no recordings.
        QCOMPARE(node->parameterValue(kSourceParameter), 1.0 / 3.0); // Felt: the upright
        QVERIFY(node->setParameter(type, typeValue(typeNamed("Electric Piano"))).has_value());
        QCOMPARE(node->parameterValue(kRecordingsParameter), 1.0);
        QCOMPARE(node->parameterValue(kSourceParameter), 2.0 / 3.0);
    }

    void aPianoIsReadOnceAndSharedAndAMissingOneSaysWhy()
    {
        QTemporaryDir empty;
        qputenv("GIGCHAIN_PIANO_SAMPLES", empty.path().toUtf8());
        auto& library = PianoLibrary::instance();
        constexpr double kOddRate = 22050.0; // a rate no other test asked for
        const auto* first = library.request(Source::Upright, kOddRate);
        QCOMPARE(library.request(Source::Upright, kOddRate), first); // the same piano, not read twice
        QVERIFY(library.request(Source::Electric, kOddRate) == nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(library.state(Source::Upright, kOddRate), PianoLibrary::State::Failed, 10000);
        QVERIFY2(library.problem(Source::Upright, kOddRate).contains(u"Upright Piano, Y.sfz"_s), qPrintable(library.problem(Source::Upright, kOddRate)));
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
        node->queueParameter(paramId(Macro::Type), typeValue(typeNamed("Grand Piano")), 0);
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
        std::vector<std::pair<int, Take>> scenes;
        { // 01: Big House Piano, the riff
            Take t{36};
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, 8);
            scenes.emplace_back(1, t);
        }
        { // 02: the grand, then the upright
            Take t{36};
            t.add({0, typeId, type("Grand Piano")});
            phrase(t, 0, 85);
            t.add({16, typeId, type("Upright Piano")});
            phrase(t, 16, 85);
            scenes.emplace_back(2, t);
        }
        { // 03: soft, hard, pedalled
            Take t{36};
            t.add({0, typeId, type("Grand Piano")});
            phrase(t, 0, 40);
            chord(t, 9, {36, 48, 55, 64, 67, 72}, 3, 125);
            t.add({14, true});
            heldChords(t, 14, 4, 4.0, 80);
            t.add({30, false});
            scenes.emplace_back(3, t);
        }
        { // 04: House Piano
            Take t{20};
            t.add({0, typeId, type("House Piano")});
            houseRiff(t, 0, 4);
            scenes.emplace_back(4, t);
        }
        { // 05: Big House Piano
            Take t{20};
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, 4);
            scenes.emplace_back(5, t);
        }
        { // 06: Prog House Piano
            Take t{20};
            t.add({0, typeId, type("Prog House Piano")});
            houseRiff(t, 0, 4);
            scenes.emplace_back(6, t);
        }
        { // 07: Emotional Piano's slow held chords, then Felt Piano's quiet melody
            Take t{40};
            t.add({0, typeId, type("Emotional Piano")});
            t.add({0, true});
            heldChords(t, 0, 2, 8.0, 75);
            t.add({16, false});
            t.add({20, typeId, type("Felt Piano")});
            phrase(t, 20, 60);
            phrase(t, 28, 55);
            scenes.emplace_back(7, t);
        }
        { // 08: Electric Piano, soft then hard
            Take t{36};
            t.add({0, typeId, type("Electric Piano")});
            heldChords(t, 0, 4, 4.0, 45);
            heldChords(t, 16, 4, 4.0, 120);
            scenes.emplace_back(8, t);
        }
        { // 09: DX Piano, then Lo-fi Keys
            Take t{36};
            t.add({0, typeId, type("DX Piano")});
            heldChords(t, 0, 4, 4.0, 100);
            t.add({16, typeId, type("Lo-fi Keys")});
            heldChords(t, 16, 4, 4.0, 100);
            scenes.emplace_back(9, t);
        }
        { // 10: House Piano plain, then octave, strings and sub come in
            Take t{36};
            t.add({0, typeId, type("House Piano")});
            for (const Layer l : {Layer::Octave, Layer::Strings, Layer::Sub}) t.add({0, paramId(l, LayerParam::On), 0.0});
            houseRiff(t, 0, 8);
            t.add({8, paramId(Layer::Octave, LayerParam::On), 1.0});
            t.add({16, paramId(Layer::Strings, LayerParam::On), 1.0});
            t.add({24, paramId(Layer::Sub, LayerParam::On), 1.0});
            scenes.emplace_back(10, t);
        }
        { // 11: Felt Piano, tape off then on
            Take t{36};
            t.add({0, typeId, type("Felt Piano")});
            t.add({0, paramId(Macro::Tape), 0.0});
            phrase(t, 0, 60);
            t.add({16, paramId(Macro::Tape), 0.85});
            phrase(t, 16, 60);
            scenes.emplace_back(11, t);
        }
        { // 12: House Piano pumping at 1/4, then 1/8D
            Take t{36};
            t.add({0, typeId, type("House Piano")});
            t.add({0, paramId(Macro::Sidechain), 0.75});
            const auto rate = [](int index) { return Ranges::fromChoice(index, static_cast<int>(sidechain::kRates.size())); };
            t.add({0, paramId(Macro::SidechainRate), rate(5)});
            heldChords(t, 0, 8, 4.0, 105);
            t.add({16, paramId(Macro::SidechainRate), rate(4)});
            scenes.emplace_back(12, t);
        }
        { // 13: the riff again (the video fades it out)
            Take t{36};
            t.add({0, typeId, type("Big House Piano")});
            houseRiff(t, 0, 8);
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

    void itIsInTheInstrumentList()
    {
        const auto builtins = PluginNode::builtins();
        QVERIFY(std::ranges::any_of(builtins, [](const PluginInfo& p) { return p.id == QLatin1StringView(PianoProNode::kPluginId); }));
    }
};

QTEST_GUILESS_MAIN(TestPianoPro)
#include "tst_piano_pro.moc"
