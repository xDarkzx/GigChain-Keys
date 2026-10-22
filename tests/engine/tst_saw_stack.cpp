// Saw Stack Pro, the app's own saw-stack synth: it plays in tune, its
// layers keep to their jobs, its types and knobs do what they say, it stays
// clean and in level, notes end, and it saves and loads its settings as a
// plugin does.
#include "PluginNode.h"
#include "SawStack.h"
#include "SawStackNode.h"
#include "VideoSounds.h"

#include "gigchain/engine/SidechainCurves.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTest>

#include <cmath>
#include <numeric>
#include <vector>

using namespace gigchain::engine;
using namespace gigchain::engine::sawstack;
using namespace Qt::StringLiterals;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr std::size_t kTenthOfASecond = 4800;

MidiEvent noteOn(int note, int velocity = 100)
{
    return MidiEvent{.status = 0x90, .data1 = static_cast<uint8_t>(note), .data2 = static_cast<uint8_t>(velocity)};
}
MidiEvent noteOff(int note)
{
    return MidiEvent{.status = 0x80, .data1 = static_cast<uint8_t>(note), .data2 = 0};
}
MidiEvent controller(int cc, int value)
{
    return MidiEvent{.status = 0xB0, .data1 = static_cast<uint8_t>(cc), .data2 = static_cast<uint8_t>(value)};
}

std::vector<MidiEvent> sixNotes()
{
    return {noteOn(48), noteOn(55), noteOn(60), noteOn(64), noteOn(67), noteOn(72)};
}

struct Audio
{
    std::vector<float> left, right;
};

// Plays `events` at the start, then renders `seconds`.
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

double rms(const std::vector<float>& s, std::size_t from = 0)
{
    return rms(std::span<const float>(s).subspan(std::min(from, s.size())));
}

float peak(const Audio& a)
{
    const auto loudest = [](const std::vector<float>& s) {
        return std::accumulate(s.begin(), s.end(), 0.0F, [](float p, float x) { return std::max(p, std::abs(x)); });
    };
    return std::max(loudest(a.left), loudest(a.right));
}

bool allFinite(const Audio& a)
{
    const auto finite = [](float x) { return std::isfinite(x); };
    return std::ranges::all_of(a.left, finite) && std::ranges::all_of(a.right, finite);
}

// The pitch, from how often the sound crosses zero going up.
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

int typeNamed(const char* name)
{
    const auto types = stackTypes();
    const auto found = std::ranges::find_if(types, [&](const StackType& t) { return QLatin1StringView(t.name) == QLatin1StringView(name); });
    if (found == types.end()) qFatal("no stack type %s", name);
    return static_cast<int>(std::distance(types.begin(), found));
}

// Only `layer` on, as one plain saw (no detune, no filter, no low cut), in a
// type without the wide doubling (it would mix the sides).
void soloPlain(Synth& synth, int layer, Wave wave = Wave::Saw)
{
    synth.setParam(paramId(Macro::Type), typeValue(typeNamed("Reese Bass")));
    synth.setParam(paramId(Macro::Spread), 0.5);
    for (const Macro effect : {Macro::Reverb, Macro::Delay, Macro::Ott, Macro::Width, Macro::Sidechain}) {
        synth.setParam(paramId(effect), 0.0); // the oscillator as it is
    }
    for (int l = 0; l < kLayers; ++l) synth.setParam(paramId(l, LayerParam::On), l == layer ? 1.0 : 0.0);
    synth.setParam(paramId(layer, LayerParam::Voices), Ranges::fromVoices(1));
    synth.setParam(paramId(layer, LayerParam::Octave), Ranges::fromOctave(0));
    synth.setParam(paramId(layer, LayerParam::Wave), Ranges::fromChoice(static_cast<int>(wave), 3));
    synth.setParam(paramId(layer, LayerParam::Cutoff), 1.0);
    synth.setParam(paramId(layer, LayerParam::FilterEnv), 0.0);
    synth.setParam(paramId(layer, LayerParam::Highpass), 0.0);
    synth.setParam(paramId(layer, LayerParam::LowestOnly), 0.0);
    synth.setParam(paramId(layer, LayerParam::Sustain), 1.0);
}

// One steady 440 Hz sine, ducked by the sidechain full on with `curve` at `rate` (120 bpm).
void steadySineUnderTheSidechain(Synth& synth, int curve, int rate)
{
    soloPlain(synth, 2, Wave::Sine);
    synth.setParam(paramId(Macro::Sidechain), 1.0);
    synth.setParam(paramId(Macro::SidechainSlope), 0.5);
    synth.setParam(paramId(Macro::SidechainCurve), Ranges::fromChoice(curve, static_cast<int>(sidechain::kCurves.size())));
    synth.setParam(paramId(Macro::SidechainRate), Ranges::fromChoice(rate, static_cast<int>(sidechain::kRates.size())));
}

// Its level, one 440 Hz cycle at a time.
std::vector<double> levelPerCycle(const std::vector<float>& s)
{
    constexpr std::size_t kCycle = 109; // 48000 / 440
    std::vector<double> levels;
    const std::span<const float> all(s);
    for (std::size_t at = 0; at + kCycle <= all.size(); at += kCycle) levels.push_back(rms(all.subspan(at, kCycle)));
    return levels;
}

} // namespace

class TestSawStack : public QObject
{
    Q_OBJECT

private slots:
    // The proof that no control on the panel is dead: each one, turned, makes
    // the sound measurably different (the difference at least -20 dB of the
    // sound itself). Same notes, same start, only that control changed.
    void everyControlOnThePanelChangesTheSound_data()
    {
        QTest::addColumn<QString>("control");
        QTest::addColumn<uint32_t>("id");
        QTest::addColumn<double>("from");
        QTest::addColumn<double>("to");
        QTest::addColumn<bool>("tail"); // measured after the notes are let go
        const auto row = [](const char* name, uint32_t id, double from, double to, bool tail = false) {
            QTest::newRow(name) << QString::fromLatin1(name) << id << from << to << tail;
        };
        row("Cutoff", paramId(Macro::Cutoff), 1.0, 0.45);
        row("Resonance", paramId(Macro::Resonance), 0.0, 0.9);
        row("Spread", paramId(Macro::Spread), 0.5, 1.0);
        row("Attack", paramId(Macro::Attack), Ranges::fromSeconds(0.003), Ranges::fromSeconds(0.5));
        row("Release", paramId(Macro::Release), Ranges::fromSeconds(0.1), Ranges::fromSeconds(2.0), true);
        row("Reverb", paramId(Macro::Reverb), 0.0, 0.8, true);
        row("Delay", paramId(Macro::Delay), 0.0, 0.7, true);
        row("OTT", paramId(Macro::Ott), 0.0, 1.0);
        row("Width", paramId(Macro::Width), 0.0, 1.0);
        row("Sidechain", paramId(Macro::Sidechain), 0.0, 1.0);
        row("Sidechain Rate", paramId(Macro::SidechainRate), Ranges::fromChoice(5, 9), 0.0);
        row("Sidechain Slope", paramId(Macro::SidechainSlope), 0.5, 1.0);
        row("Sidechain Curve", paramId(Macro::SidechainCurve), 0.0, 1.0);
        row("Sidechain Trigger", paramId(Macro::SidechainTrigger), 0.0, 1.0);
        row("Sidechain On", paramId(Macro::SidechainOn), 1.0, 0.0);
        row("Master", paramId(GlobalParam::Level), Ranges::fromLevelDb(-14.0), Ranges::fromLevelDb(-24.0));
        for (int l = 0; l < kLayers; ++l) {
            row(qPrintable(u"Layer %1 on"_s.arg(layerName(l))), paramId(l, LayerParam::On), 1.0, 0.0);
        }
    }

    void everyControlOnThePanelChangesTheSound()
    {
        QFETCH(QString, control);
        QFETCH(uint32_t, id);
        QFETCH(double, from);
        QFETCH(double, to);
        QFETCH(bool, tail);
        const auto render = [&](double value) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Type), typeValue(kDefaultType));
            // Every layer on (each layer's light is tested on its own), and
            // the sidechain's settings heard with it full on, the chord
            // played off the beat (so following the notes differs from the beat).
            for (int l = 0; l < kLayers; ++l) synth.setParam(paramId(l, LayerParam::On), 1.0);
            const bool sidechain = id == paramId(Macro::SidechainRate) || id == paramId(Macro::SidechainSlope)
                                   || id == paramId(Macro::SidechainCurve) || id == paramId(Macro::SidechainTrigger)
                                   || id == paramId(Macro::SidechainOn);
            if (sidechain) synth.setParam(paramId(Macro::Sidechain), 1.0);
            if (id == paramId(Macro::Resonance)) synth.setParam(paramId(Macro::Cutoff), 0.6); // heard at the cutoff
            synth.setParam(id, value);
            if (sidechain) (void)play(synth, {}, 0.13);
            const Audio held = play(synth, {noteOn(48), noteOn(60), noteOn(64), noteOn(67)}, 1.0);
            if (!tail) return held.left;
            return play(synth, {noteOff(48), noteOff(60), noteOff(64), noteOff(67)}, 1.5).left;
        };
        const std::vector<float> a = render(from);
        const std::vector<float> same = render(from);
        std::vector<float> nothing(a.size());
        std::ranges::transform(a, same, nothing.begin(), std::minus<>());
        QCOMPARE(rms(nothing), 0.0); // the same settings play exactly the same (so a difference is the control's)
        const std::vector<float> b = render(to);
        std::vector<float> difference(a.size());
        std::ranges::transform(a, b, difference.begin(), std::minus<>());
        // Where it changes most, a tenth of a second at a time (a pluck is
        // a hit at the start of the chord, not the whole second); measured
        // against the sound there, never against near-silence.
        const std::span<const float> sa(a);
        const std::span<const float> sb(b);
        const std::span<const float> sd(difference);
        const double overall = std::max(rms(a), rms(b));
        double ratio = 0.0;
        for (std::size_t at = 0; at + kTenthOfASecond <= sd.size(); at += kTenthOfASecond) {
            const double level = std::max({rms(sa.subspan(at, kTenthOfASecond)), rms(sb.subspan(at, kTenthOfASecond)), 0.25 * overall});
            ratio = std::max(ratio, rms(sd.subspan(at, kTenthOfASecond)) / level);
        }
        qInfo().noquote() << u"%1: the sound changes by %2 dB"_s.arg(control).arg(20.0 * std::log10(std::max(ratio, 1e-9)), 0, 'f', 1);
        QVERIFY2(ratio > 0.1, qPrintable(u"%1 barely changes the sound (%2)"_s.arg(control).arg(ratio)));
    }

    void theDetuneFollowsTheMeasuredCurve()
    {
        // Barely any spread over the first half of the knob, all of it at the top.
        QVERIFY(detuneCurve(0.0) < 0.01);
        QVERIFY(detuneCurve(0.5) < 0.2 * detuneCurve(1.0));
        QVERIFY(std::abs(detuneCurve(1.0) - 1.0) < 0.01);
        for (int step = 1; step <= 20; ++step) QVERIFY(detuneCurve(step / 20.0) >= detuneCurve((step - 1) / 20.0) - 1e-9);
    }

    void aLayerIsAsLoudWithOneSawAsWithSixteen()
    {
        UnisonSpread one;
        UnisonSpread sixteen;
        one.set(1, 0.5, 0.5, 1.0);
        sixteen.set(16, 0.5, 0.5, 1.0);
        const auto power = [](const UnisonSpread& s) {
            const auto voices = static_cast<std::ptrdiff_t>(s.voices);
            const auto square = [](double total, float x) { return total + (double{x} * x); };
            return std::accumulate(s.left.begin(), std::next(s.left.begin(), voices), 0.0, square)
                   + std::accumulate(s.right.begin(), std::next(s.right.begin(), voices), 0.0, square);
        };
        QVERIFY(std::abs(power(one) - power(sixteen)) < 0.05 * power(one));
    }

    void silentUntilANoteIsPlayed()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        const Audio a = play(synth, {}, 0.2);
        QCOMPARE(peak(a), 0.0F);
    }

    void aNotePlaysInTune()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        soloPlain(synth, 2);
        const Audio a = play(synth, {noteOn(69)}, 0.5);
        const double hz = pitchOf(a.left, kTenthOfASecond);
        QVERIFY2(std::abs(hz - 440.0) < 2.0, qPrintable(QString::number(hz)));
    }

    void theOctaveMovesTheLayer()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        soloPlain(synth, 4);
        synth.setParam(paramId(4, LayerParam::Octave), Ranges::fromOctave(1));
        const Audio a = play(synth, {noteOn(57)}, 0.5); // A3, one octave up: 440
        QVERIFY(std::abs(pitchOf(a.left, kTenthOfASecond) - 440.0) < 2.0);
    }

    void theSubPlaysOnlyTheLowestNote()
    {
        // A chord: the sub plays its root, two octaves down, and nothing else.
        Synth synth;
        synth.prepare(kRate, kBlock);
        soloPlain(synth, 0, Wave::Sine);
        synth.setParam(paramId(0, LayerParam::LowestOnly), 1.0);
        synth.setParam(paramId(0, LayerParam::Octave), Ranges::fromOctave(-2));
        const Audio a = play(synth, {noteOn(69), noteOn(73), noteOn(76)}, 0.6); // A4 C#5 E5
        const double hz = pitchOf(a.left, 2 * kTenthOfASecond);
        QVERIFY2(std::abs(hz - 110.0) < 1.0, qPrintable(QString::number(hz)));
    }

    void widthZeroIsMonoAndFullWidthIsStereo()
    {
        const auto sides = [](double width) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            soloPlain(synth, 2);
            synth.setParam(paramId(2, LayerParam::Voices), Ranges::fromVoices(9));
            synth.setParam(paramId(2, LayerParam::Detune), 0.5);
            synth.setParam(paramId(2, LayerParam::Width), width);
            const Audio a = play(synth, {noteOn(60)}, 0.3);
            std::vector<float> side(a.left.size());
            std::ranges::transform(a.left, a.right, side.begin(), std::minus<>());
            // (After the switched-off layers' fade, a few milliseconds.)
            return rms(side, kTenthOfASecond) / rms(a.left, kTenthOfASecond);
        };
        QVERIFY(sides(0.0) < 1e-4);
        QVERIFY(sides(1.0) > 0.2);
    }

    void aBigChordStaysCleanAndInLevel()
    {
        // The default stack, a six-note chord with the sustain pedal down:
        // full, never louder than full scale, never garbage.
        Synth synth;
        synth.prepare(kRate, kBlock);
        std::vector<MidiEvent> events{controller(64, 127)};
        std::ranges::copy(sixNotes(), std::back_inserter(events));
        const Audio a = play(synth, events, 1.0);
        QVERIFY(allFinite(a));
        QVERIFY2(peak(a) < 1.0F, qPrintable(QString::number(peak(a))));
        QVERIFY2(rms(a.left, 2 * kTenthOfASecond) > 0.05, qPrintable(QString::number(rms(a.left, 2 * kTenthOfASecond))));
    }

    void aBigChordCostsLittleOfTheComputer()
    {
        // Every layer on, sixteen saws in each, six notes: well inside real time.
        Synth synth;
        synth.prepare(kRate, kBlock);
        for (int l = 0; l < kNoiseLayer; ++l) synth.setParam(paramId(l, LayerParam::Voices), 1.0);
        QElapsedTimer timer;
        timer.start();
        play(synth, sixNotes(), 4.0);
        const double share = static_cast<double>(timer.nsecsElapsed()) / 4.0e9;
        qInfo().noquote() << u"Six notes, every layer at sixteen saws: %1% of one core"_s.arg(share * 100.0, 0, 'f', 1);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) // (debug and AddressSanitizer builds are many times slower)
        QVERIFY2(share < 0.5, qPrintable(QString::number(share)));
#endif
    }

    void theAirDoesNotPileUpWithMoreNotes()
    {
        // The noise is one for the whole chord: six notes, no more hiss than one.
        const auto noiseLevel = [](const std::vector<MidiEvent>& notes) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            for (int l = 0; l < kLayers; ++l) synth.setParam(paramId(l, LayerParam::On), l == kNoiseLayer ? 1.0 : 0.0);
            synth.setParam(paramId(kNoiseLayer, LayerParam::Sustain), 1.0);
            return rms(play(synth, notes, 0.5).left, 2 * kTenthOfASecond);
        };
        const double one = noiseLevel({noteOn(60)});
        const double six = noiseLevel(sixNotes());
        QVERIFY(one > 0.0);
        QVERIFY2(six < one * 1.2, qPrintable(u"%1 vs %2"_s.arg(six).arg(one)));
    }

    void aNoteLetGoRingsOutThenStops()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        play(synth, {noteOn(60)}, 0.3);
        QVERIFY(synth.holdsNotes());
        const Audio tail = play(synth, {noteOff(60)}, 3.0);
        QVERIFY(!synth.holdsNotes());
        QVERIFY(rms(tail.left) > 0.0);      // it rings out
        QCOMPARE(synth.soundingNotes(), 0); // then the note is gone
        // ...and the reverb's tail fades out below hearing (-80 dB).
        play(synth, {}, 6.0);
        const Audio after = play(synth, {}, 0.1);
        QVERIFY2(peak(after) < 1e-4F, qPrintable(QString::number(peak(after))));
    }

    void theSustainPedalHoldsNotesUntilItIsLifted()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        play(synth, {controller(64, 127), noteOn(60), noteOff(60)}, 0.2);
        QVERIFY(synth.holdsNotes());
        play(synth, {controller(64, 0)}, 3.0);
        QVERIFY(!synth.holdsNotes());
        QCOMPARE(synth.soundingNotes(), 0);
    }

    void moreNotesThanItPlaysStealTheOldest()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        std::vector<MidiEvent> many(24);
        std::ranges::generate(many, [n = 40]() mutable { return noteOn(n++); });
        play(synth, many, 0.1);
        QVERIFY(synth.soundingNotes() <= kMaxNotes);
    }

    void itSavesAndLoadsItsSettings()
    {
        auto loaded = PluginNode::load(QString::fromLatin1(SawStackNode::kPluginId), kRate, kBlock);
        QVERIFY(loaded.has_value());
        auto* node = dynamic_cast<SawStackNode*>(loaded->get());
        QVERIFY(node != nullptr);
        QVERIFY(node->isInstrument());
        // Players see the type, five sound knobs and five effect knobs, nothing else.
        QStringList names;
        for (const auto& p : node->parameters()) names << p.name;
        QCOMPARE(names, (QStringList{u"Type"_s, u"Cutoff"_s, u"Resonance"_s, u"Attack"_s, u"Release"_s, u"Spread"_s,
                                     u"Reverb"_s, u"Delay"_s, u"OTT"_s, u"Width"_s, u"Sidechain"_s,
                                     u"Sidechain Rate"_s, u"Sidechain Slope"_s, u"Sidechain Curve"_s,
                                     u"Sidechain Trigger"_s, u"Sidechain On"_s}));
        const uint32_t type = paramId(Macro::Type);
        const uint32_t cutoff = paramId(Macro::Cutoff);
        QVERIFY(node->setParameter(type, typeValue(typeNamed("Trance Lead"))).has_value());
        QVERIFY(node->setParameter(cutoff, 0.8).has_value());
        QVERIFY(node->takeEdited());
        QCOMPARE(node->takeTouchedParameter(), std::optional<uint32_t>(cutoff)); // for MIDI Learn
        QCOMPARE(node->takeTouchedParameter(), std::nullopt);
        // The type knob names its settings.
        QCOMPARE(node->parameters().front().choices.size(), static_cast<qsizetype>(stackTypes().size()));
        QVERIFY(!node->setParameter(9999, 0.5).has_value());
        const auto saved = node->saveEncodedState();
        QVERIFY(saved.has_value());

        auto other = PluginNode::load(QString::fromLatin1(SawStackNode::kPluginId), kRate, kBlock);
        QVERIFY(other.has_value());
        QVERIFY((*other)->restoreEncodedState(*saved).has_value());
        QCOMPARE(typeIndex((*other)->parameterValue(type)), typeNamed("Trance Lead"));
        QVERIFY(std::abs((*other)->parameterValue(cutoff) - 0.8) < 1e-6);
        QVERIFY(!(*other)->takeEdited()); // loading is not an edit
        // Someone else's settings are refused with the reason, not taken.
        const auto wrong = (*other)->restoreEncodedState(QByteArray("GCS1garbage"));
        QVERIFY(!wrong.has_value());
        QVERIFY(wrong.error().message.contains(u"damaged"_s));
    }

    // The sidechain as Nicky Romero's Kickstart has it (a bank of curves, the
    // length, the slope, sync or triggered by the notes), with the dotted and
    // triplet lengths Kickstart lacks.
    void theSidechainOffersCurvesLengthsAndTriggers()
    {
        auto loaded = PluginNode::load(QString::fromLatin1(SawStackNode::kPluginId), kRate, kBlock);
        QVERIFY(loaded.has_value());
        const auto params = (*loaded)->parameters();
        const auto choicesOf = [&](Macro m) {
            const auto found = std::ranges::find_if(params, [&](const PluginNode::Parameter& p) { return p.id == paramId(m); });
            return found != params.end() ? found->choices : QStringList{u"(missing)"_s};
        };
        QCOMPARE(choicesOf(Macro::SidechainRate),
                 (QStringList{u"1/16"_s, u"1/8T"_s, u"1/8"_s, u"1/4T"_s, u"1/8D"_s, u"1/4"_s, u"1/4D"_s, u"1/2"_s, u"1/1"_s}));
        QCOMPARE(choicesOf(Macro::SidechainTrigger), (QStringList{u"Sync"_s, u"Note"_s}));
        QStringList curves;
        for (const sidechain::Curve& c : sidechain::kCurves) curves << QString::fromLatin1(c.name);
        QCOMPARE(choicesOf(Macro::SidechainCurve), curves);
        QCOMPARE(curves.size(), 12);
        // A new stack ducks every quarter note, as Kickstart starts.
        Synth synth;
        QCOMPARE(sidechain::kRates.at(static_cast<std::size_t>(Ranges::choice(synth.param(paramId(Macro::SidechainRate)), 9))).beats,
                 1.0);
    }

    void eachLengthDucksThatOften_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<double>("seconds"); // between ducks at 120 bpm
        for (std::size_t r = 0; r < sidechain::kRates.size(); ++r) {
            QTest::newRow(sidechain::kRates.at(r).name) << static_cast<int>(r) << sidechain::kRates.at(r).beats * 0.5;
        }
    }

    void eachLengthDucksThatOften()
    {
        QFETCH(int, rate);
        QFETCH(double, seconds);
        Synth synth;
        synth.prepare(kRate, kBlock);
        steadySineUnderTheSidechain(synth, 0, rate);
        const std::vector<double> levels = levelPerCycle(play(synth, {noteOn(69)}, 6.2).left); // three 1/1 ducks after the start
        const double loudest = *std::ranges::max_element(levels);
        // Where it goes down (past a fifth of the level), after the first half second.
        std::vector<double> downs;
        for (std::size_t i = 1; i < levels.size(); ++i) {
            const double at = static_cast<double>(i) * 109.0 / kRate;
            if (at > 0.5 && levels.at(i) < 0.2 * loudest && levels.at(i - 1) >= 0.2 * loudest) downs.push_back(at);
        }
        QVERIFY2(downs.size() >= 3, qPrintable(u"only %1 ducks"_s.arg(downs.size())));
        const double apart = (downs.back() - downs.front()) / static_cast<double>(downs.size() - 1);
        QVERIFY2(std::abs(apart - seconds) < 0.004, qPrintable(u"ducks %1 s apart, not %2"_s.arg(apart).arg(seconds)));
    }

    void noteTriggerRestartsTheDuckOnEachNote()
    {
        // The note comes 0.2 s after the beat: following the beat, the duck
        // is already over; following the note, it ducks as the note starts.
        const auto start = [](double trigger) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            steadySineUnderTheSidechain(synth, 0, 5);
            synth.setParam(paramId(Macro::SidechainTrigger), trigger);
            (void)play(synth, {}, 0.2);
            const std::vector<float> s = play(synth, {noteOn(69)}, 0.3).left;
            return std::pair{rms(std::span<const float>(s).subspan(240, 960)), rms(std::span<const float>(s).subspan(9600, 2400))};
        };
        const auto [syncStart, syncLater] = start(0.0);
        const auto [noteStart, noteLater] = start(1.0);
        QVERIFY2(noteStart < 0.3 * syncStart, qPrintable(u"note %1, sync %2"_s.arg(noteStart).arg(syncStart)));
        QVERIFY(noteLater > 3.0 * noteStart); // and comes back up after it
    }

    void followingTheNotesItKeepsDuckingWhenTheSongJumpsBack()
    {
        // A chord at beat 100, then the song's clock goes back to 0 (a jump to
        // an earlier section, a clock Start): the duck goes on as it was,
        // never stuck at its lowest.
        Sidechain duck;
        duck.set(1.0, 0, 1.0, 0.5, true);
        duck.noteAt(100.0);
        for (int step = 0; step < 500; ++step) (void)duck.gain(100.0 + (step * 0.001));
        float loudest = 0.0F;
        for (int step = 0; step < 1000; ++step) loudest = std::max(loudest, duck.gain(step * 0.001));
        QVERIFY2(loudest > 0.9F, qPrintable(u"stuck at %1"_s.arg(loudest)));
    }

    void theSidechainSwitchesOffAndBackOnWithItsSettingsKept()
    {
        // Off: the sound is steady, as with no sidechain at all; on again:
        // it ducks as it was set, Mix and all.
        const auto ducking = [](double on) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            steadySineUnderTheSidechain(synth, 0, 5);
            synth.setParam(paramId(Macro::SidechainOn), on);
            const std::vector<double> levels = levelPerCycle(play(synth, {noteOn(69)}, 1.2).left);
            const auto [quietest, loudest] = std::ranges::minmax(std::span<const double>(levels).subspan(50));
            return quietest / loudest;
        };
        QVERIFY2(ducking(0.0) > 0.95, "still ducks when off");
        QVERIFY2(ducking(1.0) < 0.2, "does not duck when on");
        const Synth fresh;
        QVERIFY(fresh.param(paramId(Macro::SidechainOn)) >= 0.5); // a new stack has it on
        QVERIFY(fresh.param(paramId(Macro::Sidechain)) > 0.0);   // its Mix kept whether on or off
    }

    void everySidechainCurveIsItsOwnShape()
    {
        constexpr int kPoints = 64;
        const auto shape = [](std::size_t curve, double slope) {
            std::vector<double> points;
            points.reserve(kPoints);
            for (int i = 0; i < kPoints; ++i) points.push_back(sidechain::curveGain(static_cast<int>(curve), i / static_cast<double>(kPoints), slope));
            return points;
        };
        for (std::size_t a = 0; a < sidechain::kCurves.size(); ++a) {
            const std::vector<double> pa = shape(a, 0.5);
            QVERIFY(std::ranges::all_of(pa, [](double g) { return g >= 0.0 && g <= 1.0; }));
            for (std::size_t b = a + 1; b < sidechain::kCurves.size(); ++b) {
                const std::vector<double> pb = shape(b, 0.5);
                double most = 0.0;
                for (int i = 0; i < kPoints; ++i) most = std::max(most, std::abs(pa.at(i) - pb.at(i)));
                QVERIFY2(most > 0.1, qPrintable(u"%1 looks like %2"_s.arg(sidechain::kCurves.at(a).name, sidechain::kCurves.at(b).name)));
            }
            const sidechain::Curve& c = sidechain::kCurves.at(a);
            const double begin = sidechain::curveGain(static_cast<int>(a), 0.0, 0.5);
            const double middle = sidechain::curveGain(static_cast<int>(a), 0.5, 0.5);
            const double end = sidechain::curveGain(static_cast<int>(a), 0.97, 0.5);
            switch (c.group) {
            case sidechain::Group::Duck:
            case sidechain::Group::Sub: // down on the beat, back up before the next
                QVERIFY2(begin < 0.1 && end > 0.9, c.name);
                break;
            case sidechain::Group::Reverse: // swells up into the next beat
                QVERIFY2(middle < 0.5 && end > 0.8, c.name);
                break;
            case sidechain::Group::Rhythm: // more than one dip in a cycle
            {
                int dips = 0;
                for (int i = 1; i < kPoints; ++i) dips += pa.at(i) < 0.3 && pa.at(i - 1) >= 0.3 ? 1 : 0;
                QVERIFY2(dips + (pa.front() < 0.3 ? 1 : 0) >= 2, c.name);
                break;
            }
            }
        }
        // Slope stretches the dip: the classic curve is back to half later.
        const auto half = [](double slope) {
            int i = 0;
            while (i < 1000 && sidechain::curveGain(0, i / 1000.0, slope) < 0.5) ++i;
            return i;
        };
        QVERIFY(half(0.0) < half(0.5));
        QVERIFY(half(0.5) < half(1.0));
    }

    void theTypesHaveTheirCommonNames()
    {
        QStringList names;
        for (const StackType& t : stackTypes()) names << QString::fromLatin1(t.name);
        QCOMPARE(names, (QStringList{u"Supersaw Lead"_s, u"Supersaw Chords"_s, u"Future Bass Chords"_s,
                                     u"Melodic Dubstep Lead"_s, u"Trance Lead"_s, u"Anthem Lead"_s, u"Big Room Lead"_s,
                                     u"Supersaw Pad"_s, u"Supersaw Pluck"_s, u"Reese Bass"_s}));
    }

    void everyTypePlaysCleanAndAboutAsLoudAsTheOthers()
    {
        // A chord (a bass note for the Reese) in every type: no garbage,
        // never past full scale, and no type much louder or quieter than
        // the rest (switching types live must not jump in level).
        std::vector<double> levels;
        QStringList report;
        const int count = static_cast<int>(stackTypes().size());
        for (int t = 0; t < count; ++t) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Type), typeValue(t));
            const bool bass = t == typeNamed("Reese Bass");
            const std::vector<MidiEvent> notes = bass ? std::vector<MidiEvent>{noteOn(36)}
                                                      : std::vector<MidiEvent>{noteOn(60), noteOn(64), noteOn(67), noteOn(72)};
            const Audio a = play(synth, notes, 1.5);
            const QString name = QString::fromLatin1(stackType(t).name);
            QVERIFY2(allFinite(a), qPrintable(name));
            QVERIFY2(peak(a) < 1.0F, qPrintable(u"%1: peak %2"_s.arg(name).arg(peak(a))));
            // How loud it gets (a pad takes a while to swell in, a pluck is loudest at the start).
            double loudest = 0.0;
            const std::span<const float> left(a.left);
            for (std::size_t at = 0; at + kTenthOfASecond <= left.size(); at += kTenthOfASecond / 2) {
                loudest = std::max(loudest, rms(left.subspan(at, kTenthOfASecond)));
            }
            const double db = 20.0 * std::log10(std::max(loudest, 1e-9));
            levels.push_back(db);
            report << u"%1 %2 dB"_s.arg(name).arg(db, 0, 'f', 1);
        }
        qInfo().noquote() << report.join(u", "_s);
        const auto [low, high] = std::ranges::minmax(levels);
        QVERIFY2(high - low < 4.0, qPrintable(report.join(u", "_s)));
    }

    void theCutoffKnobMakesItDarkerAndBrighter()
    {
        const auto brightness = [](double knob) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Cutoff), knob);
            const Audio a = play(synth, {noteOn(60), noteOn(64), noteOn(67)}, 0.6);
            // How much of it is high (the sample-to-sample change) against how loud it is.
            std::vector<float> change(a.left.size());
            std::adjacent_difference(a.left.begin(), a.left.end(), change.begin());
            return rms(change, kTenthOfASecond) / std::max(rms(a.left, kTenthOfASecond), 1e-9);
        };
        // The knob is the main filter's frequency: about 480 Hz, 2.5 kHz, open.
        const double dark = brightness(0.45);
        const double asDesigned = brightness(0.7);
        const double bright = brightness(1.0);
        QVERIFY2(dark < asDesigned && asDesigned < bright, qPrintable(u"%1 %2 %3"_s.arg(dark).arg(asDesigned).arg(bright)));
    }

    void theAttackKnobSlowsTheStart()
    {
        const auto start = [](double seconds) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Attack), Ranges::fromSeconds(seconds));
            return rms(play(synth, {noteOn(60), noteOn(64), noteOn(67)}, 0.1).left);
        };
        QVERIFY(start(1.0) < 0.5 * start(0.003));
    }

    void theReleaseKnobLengthensTheTail()
    {
        const auto tail = [](double seconds) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Release), Ranges::fromSeconds(seconds));
            play(synth, {noteOn(60)}, 0.3);
            const Audio after = play(synth, {noteOff(60)}, 1.0);
            return rms(after.left, 5 * kTenthOfASecond); // half a second after letting go
        };
        QVERIFY(tail(2.0) > 4.0 * tail(0.1));
    }

    void anotherTypeReplacesTheLayersAndTheSameTypeKeepsTheKnobs()
    {
        Synth synth;
        synth.prepare(kRate, kBlock);
        QCOMPARE(typeIndex(synth.param(paramId(Macro::Type))), kDefaultType);
        QCOMPARE(synth.param(paramId(0, LayerParam::On)), 1.0); // the chords have a sub
        synth.setParam(paramId(Macro::Cutoff), 0.2);
        synth.setParam(paramId(Macro::Type), typeValue(kDefaultType)); // the same type again
        QCOMPARE(synth.param(paramId(Macro::Cutoff)), 0.2F);
        synth.setParam(paramId(Macro::Type), typeValue(typeNamed("Trance Lead")));
        QCOMPARE(synth.param(paramId(0, LayerParam::On)), 0.0); // a lead has none
        QCOMPARE(synth.param(paramId(Macro::Cutoff)), 1.0F);    // and its knobs start where it starts them (the filter open)
    }

    void theOttLeavesSilenceSilentAndSquashesBothWays()
    {
        Ott ott;
        ott.prepare(kRate);
        ott.setMix(1.0);
        for (std::size_t i = 0; i < kTenthOfASecond; ++i) {
            float l = 0.0F;
            float r = 0.0F;
            ott.tick(l, r);
            QCOMPARE(l, 0.0F);
        }
        // A quiet steady tone comes up, a loud one comes down.
        const auto through = [](float amplitude) {
            Ott g;
            g.prepare(kRate);
            g.setMix(1.0);
            double in = 0.0;
            double out = 0.0;
            for (int i = 0; i < 48000; ++i) {
                float l = amplitude * std::sin(2.0F * std::numbers::pi_v<float> * 1000.0F * static_cast<float>(i) / static_cast<float>(kRate));
                float r = l;
                if (i > 24000) in += double{l} * l;
                g.tick(l, r);
                if (i > 24000) out += double{l} * l;
            }
            return std::sqrt(out / in);
        };
        QVERIFY(through(0.005F) > 1.5); // -46 dB: pulled up
        QVERIFY(through(0.8F) < 0.7);   // -2 dB: pulled down
    }

    // For listening and measuring: with GIGCHAIN_SAWSTACK_RENDER set to a
    // folder, every type plays a chord progression (Am F C G, a bar each at
    // 128 BPM) into <key>.f32 (stereo, 48 kHz, 32-bit float). Off otherwise.
    void renderEveryTypeForListening()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_SAWSTACK_RENDER");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_SAWSTACK_RENDER to a folder to render every type");
        const std::vector<std::vector<int>> chords{{57, 60, 64, 69}, {53, 57, 60, 65}, {48, 55, 60, 64}, {55, 59, 62, 67}};
        const double bar = 4 * 60.0 / 128.0;
        const int count = static_cast<int>(stackTypes().size());
        for (int t = 0; t < count; ++t) {
            Synth synth;
            synth.prepare(kRate, kBlock);
            synth.setParam(paramId(Macro::Type), typeValue(t));
            const bool bass = t == typeNamed("Reese Bass");
            Audio all;
            for (const std::vector<int>& chord : chords) {
                std::vector<MidiEvent> on;
                for (int note : chord) {
                    on.push_back(noteOn(bass ? note - 24 : note));
                    if (bass) break;
                }
                Audio held = play(synth, on, bar * 0.9);
                std::vector<MidiEvent> off;
                std::ranges::transform(on, std::back_inserter(off), [](const MidiEvent& e) { return noteOff(e.data1); });
                const Audio gap = play(synth, off, bar * 0.1);
                for (const Audio* part : std::array<const Audio*, 2>{&held, &gap}) {
                    all.left.insert(all.left.end(), part->left.begin(), part->left.end());
                    all.right.insert(all.right.end(), part->right.begin(), part->right.end());
                }
            }
            const Audio tail = play(synth, {}, 2.0);
            all.left.insert(all.left.end(), tail.left.begin(), tail.left.end());
            all.right.insert(all.right.end(), tail.right.begin(), tail.right.end());
            QFile file(folder + u'/' + QString::fromLatin1(stackType(t).key) + u".f32"_s);
            QVERIFY(file.open(QIODevice::WriteOnly));
            std::vector<float> interleaved;
            interleaved.reserve(all.left.size() * 2);
            auto right = all.right.begin();
            for (const float l : all.left) {
                interleaved.push_back(l);
                interleaved.push_back(*right++);
            }
            const auto bytes = static_cast<qint64>(interleaved.size() * sizeof(float));
            QCOMPARE(file.write(reinterpret_cast<const char*>(interleaved.data()), bytes), bytes); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): raw samples to a file
        }
    }

    // Feature video 6's sound, scene by scene (its script.md): made only
    // when GIGCHAIN_VIDEO_SOUNDS names the folder to write sound_NN.wav into.
    void renderVideoSounds()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_VIDEO_SOUNDS");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_VIDEO_SOUNDS to a folder to make the video's sounds");
        using video::Take;
        const auto type = [](const char* name) { return typeValue(typeNamed(name)); };
        const uint32_t typeId = paramId(Macro::Type);
        // A minor: Am, F, C, G, a bar each.
        const std::vector<std::vector<int>> chords{{45, 57, 60, 64, 69}, {41, 57, 60, 65, 69}, {48, 55, 60, 64, 67}, {43, 55, 59, 62, 67}};
        const auto progression = [&](Take& t, double from, int bars, double hold = 4.0, int velocity = 105) {
            for (int bar = 0; bar < bars; ++bar) {
                for (const int key : chords.at(static_cast<std::size_t>(bar) % chords.size())) {
                    t.notes.push_back({.beat = from + (bar * 4.0), .key = key, .velocity = velocity, .length = hold - 0.1});
                }
            }
        };
        const auto lead = [](Take& t, double from) {
            const std::vector<std::array<double, 3>> riff{{0, 76, .5}, {.5, 74, .5}, {1, 72, .5}, {1.5, 74, .5}, {2, 76, 1},
                                                          {3, 79, .5}, {3.5, 76, .5}, {4, 74, 1.5}, {5.5, 72, .5}, {6, 71, 1}, {7, 69, .9}};
            for (const auto& [beat, key, length] : riff) {
                t.notes.push_back({.beat = from + beat, .key = static_cast<int>(key), .velocity = 110, .length = length - 0.05});
            }
        };
        const auto pluck = [](Take& t, double from) {
            const std::vector<int> arp{57, 60, 64, 69, 64, 60, 57, 60, 57, 60, 65, 69, 65, 60, 57, 60};
            for (std::size_t i = 0; i < arp.size(); ++i) {
                t.notes.push_back({.beat = from + (static_cast<double>(i) * 0.5), .key = arp.at(i), .velocity = 105, .length = 0.4});
            }
        };
        const auto on = [](int layer) { return paramId(layer, LayerParam::On); };
        std::vector<std::pair<int, Take>> scenes;
        { // 01: the drop
            Take t{36};
            t.add({0, typeId, type("Future Bass Chords")});
            progression(t, 0, 8);
            scenes.emplace_back(1, t);
        }
        { // 02: one held chord
            Take t{12};
            t.add({0, typeId, type("Supersaw Chords")});
            chord(t, 0, {45, 57, 60, 64, 69}, 8);
            scenes.emplace_back(2, t);
        }
        { // 03: Supersaw Chords, then Future Bass Chords
            Take t{34};
            t.add({0, typeId, type("Supersaw Chords")});
            progression(t, 0, 4);
            t.add({16, typeId, type("Future Bass Chords")});
            progression(t, 16, 4);
            scenes.emplace_back(3, t);
        }
        { // 04: the leads, two bars each
            Take t{34};
            const std::array<const char*, 4> leads{"Supersaw Lead", "Trance Lead", "Anthem Lead", "Big Room Lead"};
            for (std::size_t i = 0; i < leads.size(); ++i) {
                t.add({static_cast<double>(i) * 8.0, typeId, type(leads.at(i))});
                lead(t, static_cast<double>(i) * 8.0);
            }
            scenes.emplace_back(4, t);
        }
        { // 05: the pad, the pluck, the reese
            Take t{36};
            t.add({0, typeId, type("Supersaw Pad")});
            chord(t, 0, {45, 57, 60, 64, 69}, 11.5, 90);
            t.add({16, typeId, type("Supersaw Pluck")});
            pluck(t, 16);
            t.add({25, typeId, type("Reese Bass")});
            chord(t, 25, {33}, 8, 110);
            scenes.emplace_back(5, t);
        }
        { // 06: air on, off, on; pluck on, off, on
            Take t{50};
            t.add({0, typeId, type("Supersaw Chords")});
            progression(t, 0, 12, 4.0);
            t.add({8, on(kNoiseLayer), 0.0});
            t.add({16, on(kNoiseLayer), 1.0});
            t.add({32, on(6), 0.0});
            t.add({40, on(6), 1.0});
            scenes.emplace_back(6, t);
        }
        { // 07: the cutoff swept down and up, then resonance
            Take t{34};
            t.add({0, typeId, type("Supersaw Chords")});
            for (int bar = 0; bar < 8; ++bar) chord(t, bar * 4.0, {45, 57, 60, 64, 69}, 3.9);
            t.add({0, 8, paramId(Macro::Cutoff), 1.0, 0.35});
            t.add({8, 16, paramId(Macro::Cutoff), 0.35, 1.0});
            t.add({16, paramId(Macro::Cutoff), 0.62});
            t.add({16, 24, paramId(Macro::Resonance), 0.0, 0.75});
            scenes.emplace_back(7, t);
        }
        { // 08: spread, attack, release
            Take t{34};
            t.add({0, typeId, type("Supersaw Chords")});
            chord(t, 0, {45, 57, 60, 64, 69}, 6);
            t.add({8, paramId(Macro::Spread), 1.0});
            chord(t, 8, {45, 57, 60, 64, 69}, 6);
            t.add({16, paramId(Macro::Attack), Ranges::fromSeconds(0.8)});
            chord(t, 16, {45, 57, 60, 64, 69}, 6);
            t.add({24, paramId(Macro::Release), Ranges::fromSeconds(2.5)});
            chord(t, 24, {45, 57, 60, 64, 69}, 3);
            scenes.emplace_back(8, t);
        }
        { // 09: dry, then OTT, width, delay, reverb come in one by one
            Take t{36};
            t.add({0, typeId, type("Supersaw Chords")});
            for (const Macro m : {Macro::Reverb, Macro::Delay, Macro::Ott, Macro::Width}) t.add({0, paramId(m), 0.0});
            progression(t, 0, 8, 2.0);
            t.add({4, paramId(Macro::Ott), 0.8});
            t.add({12, paramId(Macro::Width), 0.8});
            t.add({20, paramId(Macro::Delay), 0.35});
            t.add({28, paramId(Macro::Reverb), 0.5});
            scenes.emplace_back(9, t);
        }
        { // 10: the curves: classic, smooth, suction, gate
            Take t{34};
            t.add({0, typeId, type("Supersaw Chords")});
            t.add({0, paramId(Macro::Sidechain), 0.85});
            progression(t, 0, 8);
            const auto curve = [](int index) { return Ranges::fromChoice(index, static_cast<int>(sidechain::kCurves.size())); };
            t.add({0, paramId(Macro::SidechainCurve), curve(0)});
            t.add({8, paramId(Macro::SidechainCurve), curve(1)});
            t.add({16, paramId(Macro::SidechainCurve), curve(8)});
            t.add({24, paramId(Macro::SidechainCurve), curve(10)});
            scenes.emplace_back(10, t);
        }
        { // 11: 1/4, then 1/8D, then off, then on
            Take t{34};
            t.add({0, typeId, type("Supersaw Chords")});
            t.add({0, paramId(Macro::Sidechain), 0.85});
            progression(t, 0, 8);
            const auto rate = [](int index) { return Ranges::fromChoice(index, static_cast<int>(sidechain::kRates.size())); };
            t.add({0, paramId(Macro::SidechainRate), rate(5)});
            t.add({8, paramId(Macro::SidechainRate), rate(4)});
            t.add({16, paramId(Macro::SidechainOn), 0.0});
            t.add({24, paramId(Macro::SidechainOn), 1.0});
            scenes.emplace_back(11, t);
        }
        { // 12: a build, the filter opening into the drop
            Take t{36};
            t.add({0, typeId, type("Future Bass Chords")});
            progression(t, 0, 8);
            t.add({0, 16, paramId(Macro::Cutoff), 0.3, 1.0});
            scenes.emplace_back(12, t);
        }
        { // 13: the drop again (the video fades it out)
            Take t{36};
            t.add({0, typeId, type("Future Bass Chords")});
            progression(t, 0, 8);
            scenes.emplace_back(13, t);
        }
        for (const auto& [number, take] : scenes) {
            Synth synth;
            synth.prepare(video::kRate, video::kBlock);
            const std::vector<float> sound = video::render(synth, take);
            const QString path = u"%1/sound_%2.wav"_s.arg(folder).arg(number, 2, 10, QLatin1Char('0'));
            QVERIFY2(video::writeWav(path, sound), qPrintable(path));
            qInfo().noquote() << path << u"%1 s"_s.arg(static_cast<double>(sound.size()) / 2.0 / video::kRate, 0, 'f', 1);
        }
    }

    void itIsInTheInstrumentList()
    {
        const auto list = PluginNode::builtins();
        QVERIFY(std::ranges::any_of(list, [](const PluginInfo& p) {
            return p.id == QLatin1StringView(SawStackNode::kPluginId) && p.name == u"Saw Stack Pro"_s
                   && p.kind == PluginKind::Instrument && p.format == PluginFormat::Builtin;
        }));
        QVERIFY(!PluginNode::isVst2(QString::fromLatin1(SawStackNode::kPluginId)));
        QVERIFY(!PluginNode::load(u"builtin:no-such-thing"_s, kRate, kBlock).has_value());
    }
};

QTEST_GUILESS_MAIN(TestSawStack)
#include "tst_saw_stack.moc"
