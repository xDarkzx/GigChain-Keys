#include "MidiRouter.h"
#include "RenderGraph.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <utility>
#include <cstdlib>
#include <new>
#include <span>
#include <vector>

// Counts heap allocations made while `t_countAllocations` is set on this thread,
// so tests can prove render() never allocates. Replacing the global operator
// new takes mutable globals and malloc/free: that is what it is.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-no-malloc)
namespace {
thread_local bool t_countAllocations = false;
std::atomic<int> g_allocations{0};
} // namespace

void* operator new(std::size_t size)
{
    if (t_countAllocations) g_allocations.fetch_add(1);
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-no-malloc)

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

constexpr int kFrames = 64;

// Instrument that outputs a constant value while any note is held, and
// records what reaches it: events, the time, and parameter changes.
class HeldNoteNode final : public INode
{
public:
    explicit HeldNoteNode(float value) : m_value(value) {}
    core::Result<void> prepare(double, int) override { return {}; }
    void process(std::span<const MidiEvent> events, AudioBlock out, const TimeInfo& time) override
    {
        lastTime = time;
        for (const MidiEvent& e : events) {
            received.push_back(e);
            // Keys are held one by one: a note-off ends only its own note.
            const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
            if (on && !m_keys.at(e.data1)) ++m_held;
            if (!on && ((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90) && m_keys.at(e.data1)) --m_held;
            if ((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90) m_keys.at(e.data1) = on;
        }
        const float v = m_held > 0 ? m_value : 0.0F;
        std::fill_n(out.left, out.frames, v);
        std::fill_n(out.right, out.frames, v);
    }
    void queueParameter(uint32_t id, double value, int32_t) noexcept override
    {
        if (parameterCount < parameters.size()) parameters.at(parameterCount++) = {id, value};
    }
    [[nodiscard]] bool holdsNotes() const noexcept override { return m_held > 0; }
    std::vector<MidiEvent> received; // test-only; reserved before render
    std::array<std::pair<uint32_t, double>, 16> parameters{}; // the first 16 parameter changes
    std::size_t parameterCount = 0;
    TimeInfo lastTime;
private:
    float m_value;
    int m_held = 0;
    std::array<bool, 128> m_keys{};
};

// Effect that computes out = (in + add) * mul, so ordering is observable.
class MathEffect final : public INode
{
public:
    MathEffect(float add, float mul) : m_add(add), m_mul(mul) {}
    core::Result<void> prepare(double, int) override { return {}; }
    void process(std::span<const MidiEvent>, AudioBlock io, const TimeInfo&) override
    {
        const auto frames = static_cast<std::size_t>(io.frames);
        const auto shape = [this](float sample) { return (sample + m_add) * m_mul; };
        std::ranges::transform(std::span(io.left, frames), io.left, shape);
        std::ranges::transform(std::span(io.right, frames), io.right, shape);
    }
private:
    float m_add;
    float m_mul;
};

MidiEvent noteOn(uint8_t note, uint8_t channel = 0) { return MidiEvent{static_cast<uint8_t>(0x90 | channel), note, 100, 0}; }

// Any other message: a controller, a note-off.
MidiEvent cc(uint8_t status, uint8_t data1, uint8_t data2)
{
    return MidiEvent{.status = status, .data1 = data1, .data2 = data2, .sampleOffset = 0};
}

struct Output
{
    std::vector<float> left = std::vector<float>(kFrames, -1.0F);
    std::vector<float> right = std::vector<float>(kFrames, -1.0F);
    AudioBlock block() { return AudioBlock{left.data(), right.data(), kFrames}; }
};

StripSpec strip(std::shared_ptr<INode> instrument, const RouteSettings& route = {}, double volumeDb = 0.0)
{
    StripSpec spec;
    spec.id = core::ChannelId::generate();
    spec.route = route;
    spec.instrument = std::move(instrument);
    spec.volumeDb = volumeDb;
    return spec;
}

} // namespace

class TestRenderGraph : public QObject
{
    Q_OBJECT

private slots:
    void routerFiltersAndTransposes()
    {
        const RouteSettings split{48, 59, 12, 0};
        QVERIFY(!routeEvent(noteOn(47), split).has_value());
        QVERIFY(!routeEvent(noteOn(60), split).has_value());
        const auto routed = routeEvent(noteOn(48), split);
        if (!routed) QFAIL("a note inside the split was not routed");
        QCOMPARE(int(routed->data1), 60);

        const RouteSettings channel2{0, 127, 0, 2};
        QVERIFY(!routeEvent(noteOn(60, 0), channel2).has_value());
        QVERIFY(routeEvent(noteOn(60, 1), channel2).has_value());

        const RouteSettings up{0, 127, 48, 0};
        QVERIFY(!routeEvent(noteOn(100), up).has_value()); // pushed past 127

        const MidiEvent sustain{0xB0, 64, 127, 0};
        const auto cc = routeEvent(sustain, split);
        if (!cc) QFAIL("a controller did not reach the layer"); // controllers reach every layer
        QCOMPARE(int(cc->data1), 64);

        const MidiEvent clock{0xF8, 0, 0, 0};
        QVERIFY(!routeEvent(clock, RouteSettings{}).has_value());
    }

    void emptyGraphRendersSilence()
    {
        RenderGraph graph({}, 48000.0, kFrames);
        Output out;
        graph.render({}, out.block(), 1.0F);
        QVERIFY(std::all_of(out.left.begin(), out.left.end(), [](float v) { return v == 0.0F; }));
    }

    void instrumentPlaysAtUnityAndVolumeScales()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.5F), {}, -6.0));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent events[] = {noteOn(60)};
        graph.render(events, out.block(), 1.0F);
        QVERIFY(std::abs(out.left[0] - 0.5F * 0.50119F) < 1e-4F);
        QVERIFY(std::abs(out.right[kFrames - 1] - out.left[0]) < 1e-6F);

        graph.strip(0)->setVolumeDb(0.0);
        graph.render({}, out.block(), 1.0F);
        QVERIFY(std::abs(out.left[0] - 0.5F) < 1e-6F);

        graph.render({}, out.block(), 0.5F); // master gain
        QVERIFY(std::abs(out.left[0] - 0.25F) < 1e-6F);
    }

    void muteAndSoloSilence()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.25F)));
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.5F)));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent events[] = {noteOn(60)};
        graph.render(events, out.block(), 1.0F);
        QVERIFY(std::abs(out.left[0] - 0.75F) < 1e-6F);

        graph.strip(1)->setMute(true);
        graph.render({}, out.block(), 1.0F);
        QVERIFY(std::abs(out.left[0] - 0.25F) < 1e-6F);

        graph.strip(1)->setMute(false);
        graph.strip(1)->setSolo(true);
        graph.render({}, out.block(), 1.0F);
        QVERIFY(std::abs(out.left[0] - 0.5F) < 1e-6F);
    }

    void panMovesSoundAcrossTheStereoField()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(1.0F)));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent on[] = {noteOn(60)};
        graph.render(on, out.block(), 1.0F); // centre: unity on both sides
        QVERIFY(std::abs(out.left[0] - 1.0F) < 1e-5F && std::abs(out.right[0] - 1.0F) < 1e-5F);

        graph.strip(0)->setPan(-1.0);
        graph.render({}, out.block(), 1.0F);
        QVERIFY(out.left[0] > 1.3F && std::abs(out.right[0]) < 1e-5F); // hard left, +3 dB law

        graph.strip(0)->setPan(0.5);
        graph.render({}, out.block(), 1.0F);
        QVERIFY(out.right[0] > out.left[0] && out.left[0] > 0.0F);
    }

    void effectsRunInOrder()
    {
        StripSpec spec = strip(std::make_shared<HeldNoteNode>(1.0F));
        spec.effects.push_back(std::make_shared<MathEffect>(1.0F, 1.0F)); // +1
        spec.effects.push_back(std::make_shared<MathEffect>(0.0F, 2.0F)); // *2
        std::vector<StripSpec> specs;
        specs.push_back(std::move(spec));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent events[] = {noteOn(60)};
        graph.render(events, out.block(), 1.0F);
        QCOMPARE(out.left[0], 4.0F); // (1 + 1) * 2, not 1 * 2 + 1
    }

    void splitsReceiveOnlyTheirNotes()
    {
        auto low = std::make_shared<HeldNoteNode>(0.1F);
        auto high = std::make_shared<HeldNoteNode>(0.2F);
        low->received.reserve(16);
        high->received.reserve(16);
        std::vector<StripSpec> specs;
        specs.push_back(strip(low, RouteSettings{0, 59, -12, 0}));
        specs.push_back(strip(high, RouteSettings{60, 127, 0, 0}));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent events[] = {noteOn(40), noteOn(72)};
        graph.render(events, out.block(), 1.0F);
        QCOMPARE(low->received.size(), std::size_t{1});
        QCOMPARE(int(low->received[0].data1), 28);
        QCOMPARE(high->received.size(), std::size_t{1});
        QCOMPARE(int(high->received[0].data1), 72);
    }

    void metersHoldPeakUntilRead()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.8F)));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent on[] = {noteOn(60)};
        const MidiEvent off[] = {MidiEvent{0x80, 60, 0, 0}};
        graph.render(on, out.block(), 1.0F);
        graph.render(off, out.block(), 1.0F);
        const LevelReading level = graph.strip(0)->takeLevel();
        QVERIFY(std::abs(level.peak - 0.8F) < 1e-6F); // peak survived the silent block
        QCOMPARE(graph.strip(0)->takeLevel().peak, 0.0F);
    }

    void oversizedBlockIsSilentAndCounted()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.5F)));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        std::vector<float> left(static_cast<std::size_t>(kFrames) * 2, 1.0F);
        std::vector<float> right(static_cast<std::size_t>(kFrames) * 2, 1.0F);
        const MidiEvent events[] = {noteOn(60)};
        graph.render(events, AudioBlock{left.data(), right.data(), kFrames * 2}, 1.0F);
        QVERIFY(std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0F; }));
        QCOMPARE(graph.takeOversizedBlocks(), uint64_t{1});
        QCOMPARE(graph.takeOversizedBlocks(), uint64_t{0});
    }

    void findsStripsById()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.1F)));
        const core::ChannelId id = specs[0].id;
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        QVERIFY(graph.findStrip(id) == graph.strip(0));
        QVERIFY(graph.findStrip(core::ChannelId::generate()) == nullptr);
    }

    void theTimeReachesEveryNode()
    {
        auto piano = std::make_shared<HeldNoteNode>(0.1F);
        std::vector<StripSpec> specs;
        specs.push_back(strip(piano));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const TimeInfo time{.tempo = 97.5, .sampleRate = 48000.0, .samplePosition = 4800, .ppqPosition = 3.25,
                            .barStartPpq = 0.0, .timeSigNumerator = 4, .timeSigDenominator = 4};
        graph.render({}, out.block(), 1.0F, time);
        QCOMPARE(piano->lastTime.tempo, 97.5);
        QCOMPARE(piano->lastTime.ppqPosition, 3.25);
        QCOMPARE(piano->lastTime.samplePosition, int64_t{4800});
    }

    void velocityLayersSplitByTouch()
    {
        RouteSettings soft;
        soft.velocityHigh = 63;
        RouteSettings hard;
        hard.velocityLow = 64;
        QVERIFY(routeEvent(MidiEvent{0x90, 60, 40, 0}, soft).has_value());
        QVERIFY(!routeEvent(MidiEvent{0x90, 60, 40, 0}, hard).has_value());
        QVERIFY(!routeEvent(MidiEvent{0x90, 60, 100, 0}, soft).has_value());
        QVERIFY(routeEvent(MidiEvent{0x90, 60, 100, 0}, hard).has_value());
        // Note-offs reach every layer (a key let go must never hang).
        QVERIFY(routeEvent(MidiEvent{0x80, 60, 0, 0}, soft).has_value());
        QVERIFY(routeEvent(MidiEvent{0x90, 60, 0, 0}, hard).has_value());
    }

    void aMappedKnobMovesItsParameterAndNothingElseHearsIt()
    {
        auto synth = std::make_shared<HeldNoteNode>(0.1F);
        auto effect = std::make_shared<HeldNoteNode>(0.0F);
        synth->received.reserve(16);
        StripSpec spec = strip(synth);
        spec.effects.push_back(effect);
        spec.mappings.push_back(ParameterMapping{.midiChannel = 0, .controller = 74, .target = -1, .parameter = 42,
                                                 .minimum = 0.2, .maximum = 0.6});
        spec.mappings.push_back(ParameterMapping{.midiChannel = 2, .controller = 71, .target = 0, .parameter = 7,
                                                 .minimum = 1.0, .maximum = 0.0}); // reversed
        std::vector<StripSpec> specs;
        specs.push_back(std::move(spec));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const std::array events{cc(0xB0, 74, 127), cc(0xB1, 71, 0), cc(0xB0, 71, 127), cc(0xB0, 1, 64)};
        graph.render(events, out.block(), 1.0F);
        QCOMPARE(synth->parameterCount, std::size_t{1});
        QCOMPARE(synth->parameters.at(0).first, uint32_t{42});
        QVERIFY(std::abs(synth->parameters.at(0).second - 0.6) < 1e-9);
        QCOMPARE(effect->parameterCount, std::size_t{1}); // CC 71 on channel 1 is not its knob
        QVERIFY(std::abs(effect->parameters.at(0).second - 1.0) < 1e-9);
        // Only the unmapped mod wheel reached the instrument as MIDI.
        QCOMPARE(synth->received.size(), std::size_t{2});
        QCOMPARE(int(synth->received.at(1).data1), 1);
    }

    void anInputChannelPlaysTheAudioInput()
    {
        StripSpec spec;
        spec.id = core::ChannelId::generate();
        spec.inputLeft = 1; // the second input, mono
        std::vector<StripSpec> specs;
        specs.push_back(std::move(spec));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        std::vector<float> in1(kFrames, 0.1F);
        std::vector<float> in2(kFrames, 0.3F);
        const std::array<const float*, 2> channels{in1.data(), in2.data()};
        Output out;
        graph.render({}, out.block(), 1.0F, {}, AudioInputs{.channels = channels, .frames = kFrames});
        QVERIFY(std::abs(out.left.front() - 0.3F) < 1e-6F && std::abs(out.right.back() - 0.3F) < 1e-6F);
        // No inputs open (none chosen, or unplugged): silence, not garbage.
        graph.render({}, out.block(), 1.0F);
        QCOMPARE(out.left.front(), 0.0F);
    }

    void aHeldNoteRingsOnAfterAPatchChange()
    {
        auto pad = std::make_shared<HeldNoteNode>(0.4F);
        auto piano = std::make_shared<HeldNoteNode>(0.1F);
        pad->received.reserve(16);
        piano->received.reserve(16);
        std::vector<StripSpec> first;
        first.push_back(strip(pad));
        RenderGraph before(std::move(first), 48000.0, kFrames);
        Output out;
        const std::array hold{noteOn(60)};
        before.render(hold, out.block(), 1.0F);

        // The next patch plays the piano; the pad carries on as a tail.
        std::vector<StripSpec> second;
        second.push_back(strip(piano));
        RenderGraph after(std::move(second), 48000.0, kFrames, {}, before.strips());
        const std::array newNote{noteOn(64)};
        after.render(newNote, out.block(), 1.0F);
        QVERIFY2(std::abs(out.left.front() - 0.5F) < 1e-6F, "the held pad and the new piano note sound together");
        QCOMPARE(pad->received.size(), std::size_t{1}); // the new note went to the piano only

        // Letting the key go ends the pad; after a quiet second the tail is done.
        const std::array release{cc(0x80, 60, 0)};
        after.render(release, out.block(), 1.0F);
        QVERIFY(std::abs(out.left.front() - 0.1F) < 1e-6F);
        QVERIFY(!after.tails().front()->tailDone());
        for (int i = 0; i < 48000 / kFrames + 1; ++i) after.render({}, out.block(), 1.0F);
        QVERIFY(after.tails().front()->tailDone());
    }

    // Sections: a strip takes new notes only in its sections, exactly from
    // the switch point; everything else still reaches it.
    void aSectionGateSendsNewNotesToItsStrips()
    {
        auto verse = std::make_shared<HeldNoteNode>(0.25F);
        auto chorus = std::make_shared<HeldNoteNode>(0.5F);
        verse->received.reserve(16);
        chorus->received.reserve(16);
        std::vector<StripSpec> specs;
        specs.push_back(strip(verse));
        specs.push_back(strip(chorus));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        graph.strip(0)->setSections(0b01); // section 0
        graph.strip(1)->setSections(0b10); // section 1
        QCOMPARE(graph.strip(0)->sections(), uint64_t{0b01});

        MidiEvent before = noteOn(60);
        before.sampleOffset = 31;
        MidiEvent after = noteOn(64);
        after.sampleOffset = 32;
        MidiEvent release = cc(0x80, 60, 0); // the verse's key let go after the switch
        release.sampleOffset = 40;
        const std::array events{before, after, release};
        Output out;
        graph.render(events, out.block(), 1.0F, {}, {}, SectionGate{.before = 0, .after = 1, .switchAt = 32});

        QCOMPARE(verse->received.size(), std::size_t{2}); // its note, and its note-off
        QCOMPARE(verse->received.at(0).data1, uint8_t{60});
        QCOMPARE(verse->received.at(1).status, uint8_t{0x80});
        QCOMPARE(chorus->received.size(), std::size_t{2}); // its note (note-offs reach every strip: harmless)
        QCOMPARE(chorus->received.at(0).data1, uint8_t{64});
        QCOMPARE(chorus->received.at(1).status, uint8_t{0x80});
        QCOMPARE(out.left.at(kFrames - 1), 0.5F); // the verse's key was let go; the chorus sounds
    }

    // Chord follow entering the chorus: the chord's keys pressed a moment
    // before (they reached the verse) move to the chorus at the switch.
    void aHandoverMovesTheHeldChord()
    {
        auto verse = std::make_shared<HeldNoteNode>(0.25F);
        auto chorus = std::make_shared<HeldNoteNode>(0.5F);
        auto both = std::make_shared<HeldNoteNode>(0.125F);
        for (auto* node : {verse.get(), chorus.get(), both.get()}) node->received.reserve(16);
        std::vector<StripSpec> specs;
        specs.push_back(strip(verse));
        specs.push_back(strip(chorus));
        specs.push_back(strip(both));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        graph.strip(0)->setSections(0b01);
        graph.strip(1)->setSections(0b10);
        graph.strip(2)->setSections(0b11); // plays in both: nothing to move

        MidiEvent trigger = noteOn(53); // the key that completed the chord, at the switch
        trigger.sampleOffset = 20;
        MidiEvent handOn = noteOn(48);
        handOn.sampleOffset = 20;
        MidiEvent handOff = cc(0x80, 48, 0);
        handOff.sampleOffset = 20;
        const std::array events{trigger};
        const std::array handover{handOn, handOff};
        Output out;
        graph.render(events, out.block(), 1.0F, {}, {},
                     SectionGate{.before = 0, .after = 1, .switchAt = 20, .handover = handover});

        // The verse lets go of 48 and gets nothing new.
        QCOMPARE(verse->received.size(), std::size_t{1});
        QCOMPARE(verse->received.at(0).status, uint8_t{0x80});
        QCOMPARE(verse->received.at(0).data1, uint8_t{48});
        // The chorus gets 48 (handed over) and 53 (the trigger).
        QCOMPARE(chorus->received.size(), std::size_t{2});
        QVERIFY(std::ranges::all_of(chorus->received, [](const MidiEvent& e) { return (e.status & 0xF0) == 0x90; }));
        // A strip in both sections already had 48: only the trigger.
        QCOMPARE(both->received.size(), std::size_t{1});
        QCOMPARE(both->received.at(0).data1, uint8_t{53});
    }

    void withoutSectionsEveryStripPlays()
    {
        auto a = std::make_shared<HeldNoteNode>(0.25F);
        auto b = std::make_shared<HeldNoteNode>(0.5F);
        std::vector<StripSpec> specs;
        specs.push_back(strip(a));
        specs.push_back(strip(b));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        graph.strip(0)->setSections(0);
        graph.strip(1)->setSections(0);
        const std::array events{noteOn(60)};
        Output out;
        graph.render(events, out.block(), 1.0F); // no gate: no sections
        QCOMPARE(a->received.size(), std::size_t{1});
        QCOMPARE(b->received.size(), std::size_t{1});
        QCOMPARE(out.left.at(0), 0.75F);
    }

    void aGatedStripStillHearsPedalsAndKnobs()
    {
        auto node = std::make_shared<HeldNoteNode>(0.25F);
        std::vector<StripSpec> specs;
        specs.push_back(strip(node));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        graph.strip(0)->setSections(0b10); // not in section 0
        const std::array events{noteOn(60), cc(0xB0, 64, 127), cc(0xE0, 0, 80), cc(0x90, 60, 0)};
        Output out;
        graph.render(events, out.block(), 1.0F, {}, {}, SectionGate{.before = 0, .after = 0, .switchAt = 0});
        QCOMPARE(node->received.size(), std::size_t{3}); // sustain, bend, the note-on with velocity 0 (a note-off)
        QCOMPARE(node->received.at(0).status, uint8_t{0xB0});
        QCOMPARE(out.left.at(0), 0.0F); // no note sounded
    }

    void renderDoesNotAllocate()
    {
        auto first = std::make_shared<HeldNoteNode>(0.3F);
        auto second = std::make_shared<HeldNoteNode>(0.2F);
        first->received.reserve(1024); // the test node's own bookkeeping must not count
        second->received.reserve(1024);
        // A tail from an earlier patch rings while the new graph plays.
        auto tailNode = std::make_shared<HeldNoteNode>(0.2F);
        tailNode->received.reserve(1024);
        std::vector<StripSpec> tailSpecs;
        tailSpecs.push_back(strip(tailNode));
        RenderGraph old(std::move(tailSpecs), 48000.0, kFrames);
        StripSpec spec = strip(first, RouteSettings{0, 127, 5, 0});
        spec.effects.push_back(std::make_shared<MathEffect>(0.0F, 0.5F));
        spec.mappings.push_back(ParameterMapping{.midiChannel = 0, .controller = 74, .target = -1, .parameter = 3,
                                                 .minimum = 0.0, .maximum = 1.0});
        std::vector<StripSpec> specs;
        specs.push_back(std::move(spec));
        specs.push_back(strip(second));
        RenderGraph graph(std::move(specs), 48000.0, kFrames, {}, old.strips());
        Output out;
        const std::array events{noteOn(60), noteOn(61), cc(0xB0, 64, 127), cc(0xB0, 74, 90)};
        const TimeInfo time{.tempo = 128.0, .sampleRate = 48000.0, .samplePosition = 0, .ppqPosition = 0.0,
                            .barStartPpq = 0.0, .timeSigNumerator = 4, .timeSigDenominator = 4};

        g_allocations = 0;
        t_countAllocations = true;
        for (int i = 0; i < 100; ++i) {
            graph.render(events, out.block(), 1.0F, time);
            (void)graph.strip(0)->takeLevel();
        }
        t_countAllocations = false;
        QCOMPARE(g_allocations.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRenderGraph)
#include "tst_render_graph.moc"
