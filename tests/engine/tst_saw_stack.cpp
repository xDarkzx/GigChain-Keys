// Saw Stack Pro, the app's own saw-stack synth: it plays in tune, its
// layers keep to their jobs, its types and knobs do what they say, it stays
// clean and in level, notes end, and it saves and loads its settings as a
// plugin does.
#include "PluginNode.h"
#include "SawStack.h"
#include "SawStackNode.h"

#include <QElapsedTimer>
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

} // namespace

class TestSawStack : public QObject
{
    Q_OBJECT

private slots:
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
#ifdef NDEBUG // (a debug build is many times slower)
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
        QCOMPARE(synth.soundingNotes(), 0); // then it is gone
        const Audio after = play(synth, {}, 0.1);
        QCOMPARE(peak(after), 0.0F);
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
        // Players see the type and five knobs, nothing else.
        QStringList names;
        for (const auto& p : node->parameters()) names << p.name;
        QCOMPARE(names, (QStringList{u"Type"_s, u"Cutoff"_s, u"Resonance"_s, u"Attack"_s, u"Release"_s, u"Spread"_s}));
        const uint32_t type = paramId(Macro::Type);
        const uint32_t cutoff = paramId(Macro::Cutoff);
        node->setParameter(type, typeValue(typeNamed("Trance Lead")));
        node->setParameter(cutoff, 0.8);
        QVERIFY(node->takeEdited());
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
        const double dark = brightness(0.1);
        const double asDesigned = brightness(0.5);
        const double bright = brightness(0.9);
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
        QCOMPARE(synth.param(paramId(Macro::Cutoff)), 0.5F);    // and its knobs start where it starts them
    }

    void theGlueLeavesSilenceSilentAndSquashesBothWays()
    {
        Glue glue;
        glue.prepare(kRate);
        glue.setDepth(1.0);
        for (std::size_t i = 0; i < kTenthOfASecond; ++i) {
            float l = 0.0F;
            float r = 0.0F;
            glue.tick(l, r);
            QCOMPARE(l, 0.0F);
        }
        // A quiet steady tone comes up, a loud one comes down.
        const auto through = [](float amplitude) {
            Glue g;
            g.prepare(kRate);
            g.setDepth(1.0);
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
