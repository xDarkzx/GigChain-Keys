#include "MidiRouter.h"
#include "RenderGraph.h"

#include <QtTest>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

// Counts heap allocations made while `t_countAllocations` is set on this thread,
// so tests can prove render() never allocates.
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

using namespace openstage;
using namespace openstage::engine;
using namespace Qt::StringLiterals;

namespace {

constexpr int kFrames = 64;

// Instrument that outputs a constant value while any note is held.
class HeldNoteNode final : public INode
{
public:
    explicit HeldNoteNode(float value) : m_value(value) {}
    void prepare(double, int) override {}
    void process(std::span<const MidiEvent> events, AudioBlock out) override
    {
        for (const MidiEvent& e : events) {
            received.push_back(e);
            if ((e.status & 0xF0) == 0x90 && e.data2 > 0) ++m_held;
            else if ((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90) --m_held;
        }
        const float v = m_held > 0 ? m_value : 0.0F;
        std::fill_n(out.left, out.frames, v);
        std::fill_n(out.right, out.frames, v);
    }
    std::vector<MidiEvent> received; // test-only; reserved before render
private:
    float m_value;
    int m_held = 0;
};

// Effect that computes out = (in + add) * mul, so ordering is observable.
class MathEffect final : public INode
{
public:
    MathEffect(float add, float mul) : m_add(add), m_mul(mul) {}
    void prepare(double, int) override {}
    void process(std::span<const MidiEvent>, AudioBlock io) override
    {
        for (int i = 0; i < io.frames; ++i) {
            io.left[i] = (io.left[i] + m_add) * m_mul;
            io.right[i] = (io.right[i] + m_add) * m_mul;
        }
    }
private:
    float m_add;
    float m_mul;
};

MidiEvent noteOn(uint8_t note, uint8_t channel = 0) { return MidiEvent{static_cast<uint8_t>(0x90 | channel), note, 100, 0}; }

struct Output
{
    std::vector<float> left = std::vector<float>(kFrames, -1.0F);
    std::vector<float> right = std::vector<float>(kFrames, -1.0F);
    AudioBlock block() { return AudioBlock{left.data(), right.data(), kFrames}; }
};

StripSpec strip(std::shared_ptr<INode> instrument, RouteSettings route = {}, double volumeDb = 0.0)
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
        QVERIFY(routed.has_value());
        QCOMPARE(int(routed->data1), 60);

        const RouteSettings channel2{0, 127, 0, 2};
        QVERIFY(!routeEvent(noteOn(60, 0), channel2).has_value());
        QVERIFY(routeEvent(noteOn(60, 1), channel2).has_value());

        const RouteSettings up{0, 127, 48, 0};
        QVERIFY(!routeEvent(noteOn(100), up).has_value()); // pushed past 127

        const MidiEvent sustain{0xB0, 64, 127, 0};
        const auto cc = routeEvent(sustain, split);
        QVERIFY(cc.has_value()); // controllers reach every layer
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

    void findsStripsById()
    {
        std::vector<StripSpec> specs;
        specs.push_back(strip(std::make_shared<HeldNoteNode>(0.1F)));
        const core::ChannelId id = specs[0].id;
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        QVERIFY(graph.findStrip(id) == graph.strip(0));
        QVERIFY(graph.findStrip(core::ChannelId::generate()) == nullptr);
    }

    void renderDoesNotAllocate()
    {
        auto first = std::make_shared<HeldNoteNode>(0.3F);
        auto second = std::make_shared<HeldNoteNode>(0.2F);
        first->received.reserve(1024); // the test node's own bookkeeping must not count
        second->received.reserve(1024);
        StripSpec spec = strip(first, RouteSettings{0, 127, 5, 0});
        spec.effects.push_back(std::make_shared<MathEffect>(0.0F, 0.5F));
        std::vector<StripSpec> specs;
        specs.push_back(std::move(spec));
        specs.push_back(strip(second));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        Output out;
        const MidiEvent events[] = {noteOn(60), noteOn(61), MidiEvent{0xB0, 64, 127, 0}};

        g_allocations = 0;
        t_countAllocations = true;
        for (int i = 0; i < 100; ++i) {
            graph.render(events, out.block(), 1.0F);
            (void)graph.strip(0)->takeLevel();
        }
        t_countAllocations = false;
        QCOMPARE(g_allocations.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRenderGraph)
#include "tst_render_graph.moc"
