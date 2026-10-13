// Measures what a player hears when the sound changes: a patch or song
// change, or a fader, mute or solo moved while playing. A pure tone (whose
// steepest slope is known) through an echo (a tail that rings on), rendered
// block by block, with patch changes built as RealEngine::applyPatch builds
// them: strips the new patch does not share ring out as tails; a strip
// sharing an instrument with the new patch does not.
//
// For each case: the largest step between two samples around the change
// against the largest before it (a click is a step well above the sound's
// own), any silence where the sound should go on (a gap), and how long the
// old sound rings on after the change.
#include "RenderGraph.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <numeric>
#include <set>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// A sine while a key is held: a 10 ms fade in, and a smooth 100 ms
// release (as a real instrument, so only the change itself can click).
class ToneNode final : public INode
{
public:
    explicit ToneNode(double hz) : m_step(2.0 * std::numbers::pi * hz / kRate) {}
    core::Result<void> prepare(double, int) override { return {}; }
    void process(std::span<const MidiEvent> events, AudioBlock out, const TimeInfo&) override
    {
        for (const MidiEvent& e : events) {
            const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
            const bool off = (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
            if (on) m_held = true;
            if (off) m_held = false;
        }
        const double attack = 1.0 / (0.010 * kRate);
        const double release = std::exp(-1.0 / (0.100 * kRate));
        const auto frames = static_cast<std::size_t>(out.frames);
        const std::span left(out.left, frames);
        const std::span right(out.right, frames);
        for (float& sample : left) {
            m_level = m_held ? std::min(1.0, m_level + attack) : m_level * release;
            sample = static_cast<float>(0.5 * m_level * std::sin(m_phase));
            m_phase += m_step;
        }
        std::ranges::copy(left, right.begin());
    }
    [[nodiscard]] bool holdsNotes() const noexcept override { return m_held; }

private:
    double m_step;
    double m_phase = 0.0;
    double m_level = 0.0;
    bool m_held = false;
};

// A feedback echo (30 ms, 0.9): the sound plus its fading repeats, for
// about two seconds after the sound stops. Stands in for a reverb or delay.
class EchoNode final : public INode
{
public:
    core::Result<void> prepare(double, int) override { return {}; }
    void process(std::span<const MidiEvent>, AudioBlock io, const TimeInfo&) override
    {
        const auto frames = static_cast<std::size_t>(io.frames);
        const std::span left(io.left, frames);
        const std::span right(io.right, frames);
        for (float& sample : left) {
            sample += m_line.at(m_at);
            m_line.at(m_at) = sample * 0.9F;
            m_at = (m_at + 1) % m_line.size();
        }
        std::ranges::copy(left, right.begin());
    }

private:
    std::vector<float> m_line = std::vector<float>(static_cast<std::size_t>(0.030 * kRate), 0.0F);
    std::size_t m_at = 0;
};

MidiEvent on(uint8_t note) { return MidiEvent{.status = 0x90, .data1 = note, .data2 = 100}; }
MidiEvent off(uint8_t note) { return MidiEvent{.status = 0x80, .data1 = note, .data2 = 0}; }

StripSpec spec(const std::shared_ptr<INode>& instrument, std::vector<std::shared_ptr<INode>> effects = {}, double volumeDb = 0.0)
{
    StripSpec s;
    s.id = core::ChannelId::generate();
    s.instrument = instrument;
    s.effects = std::move(effects);
    s.volumeDb = volumeDb;
    return s;
}

// The patch change as RealEngine::applyPatch makes it (carryOver: what
// rings out of the old graph, and the levels a sound playing on starts at).
std::shared_ptr<RenderGraph> changeTo(const std::shared_ptr<RenderGraph>& previous, std::vector<StripSpec> specs)
{
    CarryOver carried = carryOver(previous.get(), specs, {}, 8);
    return std::make_shared<RenderGraph>(std::move(specs), kRate, kBlock, std::vector<std::shared_ptr<INode>>{},
                                         std::move(carried.tails));
}

// Renders blocks of the current graph, keeping the left channel.
struct Run
{
    explicit Run(std::shared_ptr<RenderGraph> first) : graph(std::move(first)) {}

    std::shared_ptr<RenderGraph> graph;
    std::vector<float> heard;
    std::vector<float> left = std::vector<float>(kBlock);
    std::vector<float> right = std::vector<float>(kBlock);
    int64_t position = 0;

    void play(int blocks, std::vector<MidiEvent> first = {})
    {
        for (int b = 0; b < blocks; ++b) {
            TimeInfo time;
            time.samplePosition = position;
            graph->render(b == 0 ? std::span<const MidiEvent>(first) : std::span<const MidiEvent>(),
                          {.left = left.data(), .right = right.data(), .frames = kBlock}, 1.0F, time);
            heard.insert(heard.end(), left.begin(), left.end());
            position += kBlock;
        }
    }
    [[nodiscard]] std::size_t now() const { return heard.size(); }
};

int blocksFor(double seconds) { return static_cast<int>(std::ceil(seconds * kRate / kBlock)); }

struct Measure
{
    double stepRatio = 0.0; // largest step near the change / largest before it
    double gapMs = 0.0;     // silence (< -60 dB of the sound before) inside the 300 ms after it
    double ringsMs = 0.0;   // how long after the change the sound is last above -60 dB of before
    double jumpDb = 0.0;    // largest loudness change between two 2.5 ms slices around the change
};

double rms(std::span<const float> s)
{
    const double sum = std::accumulate(s.begin(), s.end(), 0.0, [](double total, float x) { return total + (static_cast<double>(x) * x); });
    return s.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(s.size()));
}

// `at`: the sample where the change happened.
Measure measure(const std::vector<float>& heard, std::size_t at)
{
    Measure m;
    const auto steepest = [&](std::size_t from, std::size_t to) {
        double most = 0.0;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, heard.size()); ++i) {
            most = std::max(most, static_cast<double>(std::abs(heard.at(i) - heard.at(i - 1))));
        }
        return most;
    };
    const auto window = static_cast<std::size_t>(0.1 * kRate);
    const double before = steepest(at - window, at);
    m.stepRatio = before > 0.0 ? steepest(at - 2, at + (std::size_t{2} * kBlock)) / before : 0.0;
    const double level = rms(std::span(heard).subspan(at - window, window));
    const double floor = level * 0.001; // -60 dB
    const auto slice = static_cast<std::size_t>(0.0025 * kRate); // 2.5 ms
    for (std::size_t i = at - 2 * slice; i + slice <= std::min(heard.size(), at + 8 * slice); i += slice) {
        const double a = rms(std::span(heard).subspan(i - slice, slice));
        const double b = rms(std::span(heard).subspan(i, slice));
        if (a > floor && b > floor) m.jumpDb = std::max(m.jumpDb, std::abs(20.0 * std::log10(b / a)));
        if ((a > floor) != (b > floor)) m.jumpDb = std::max(m.jumpDb, 60.0); // to or from silence
    }
    // (Rings: up to the last slice above the floor: a tone and its echo can
    // cancel for a moment, which is not the end of the sound.)
    for (std::size_t i = at; i + slice <= heard.size(); i += slice) {
        const double r = rms(std::span(heard).subspan(i, slice));
        const double ms = 1000.0 * static_cast<double>(i - at) / kRate;
        if (r < floor) {
            if (ms < 300.0) m.gapMs += 2.5;
        } else {
            m.ringsMs = ms + 2.5;
        }
    }
    return m;
}

void report(const char* what, const Measure& m)
{
    qInfo("%-58s step x%6.2f  jump %5.1f dB  gap %6.1f ms  rings %7.1f ms", what, m.stepRatio, m.jumpDb, m.gapMs, m.ringsMs);
}

// No click: no step between two samples more than twice the sound's own
// steepest (a hard cut or a level jump is 10-35 times it).
constexpr double kMostStep = 2.0;
// The same sound carrying on: its loudness moves no more than this between 2.5 ms slices.
constexpr double kMostJumpDb = 3.0;

// How long the echo rings on when the next patch keeps it: the reference
// for a patch that drops it.
double ringsWithEchoKept()
{
    auto piano = std::make_shared<ToneNode>(220.0);
    auto echo = std::make_shared<EchoNode>();
    Run run{changeTo(nullptr, {spec(piano, {echo})})};
    run.play(blocksFor(0.5), {on(60)});
    run.play(blocksFor(0.05), {off(60)});
    const std::size_t at = run.now();
    run.graph = changeTo(run.graph, {spec(piano, {echo})});
    run.play(blocksFor(3.0));
    return measure(run.heard, at).ringsMs;
}

} // namespace

class TestTransitions : public QObject
{
    Q_OBJECT

private slots:
    // Another song: different instruments. The old one, its key still held,
    // rings on as a tail until the key is let go, then fades with its echo.
    void anotherSongWhileAKeyIsHeld()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto echo = std::make_shared<EchoNode>();
        auto strings = std::make_shared<ToneNode>(330.0);
        Run run{changeTo(nullptr, {spec(piano, {echo})})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(strings)});
        run.play(blocksFor(0.2));
        run.play(blocksFor(3.0), {off(60)}); // let go after the change
        const Measure m = measure(run.heard, at);
        report("another song, key held (let go 200 ms later)", m);
        QVERIFY(m.stepRatio <= kMostStep);
        QVERIFY(m.jumpDb <= kMostJumpDb);
        QCOMPARE(m.gapMs, 0.0);
        QVERIFY(m.ringsMs > 1000.0); // held 200 ms, then its release and echo
    }

    // Another song after the key was let go: only the echo is ringing.
    void anotherSongWhileTheEchoRings()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto echo = std::make_shared<EchoNode>();
        auto strings = std::make_shared<ToneNode>(330.0);
        Run run{changeTo(nullptr, {spec(piano, {echo})})};
        run.play(blocksFor(0.5), {on(60)});
        run.play(blocksFor(0.05), {off(60)});
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(strings)});
        run.play(blocksFor(3.0));
        const Measure m = measure(run.heard, at);
        report("another song, echo ringing", m);
        QVERIFY(m.stepRatio <= kMostStep);
        QVERIFY(m.jumpDb <= kMostJumpDb);
        QVERIFY(m.ringsMs >= 0.9 * ringsWithEchoKept());
    }

    // The next patch keeps the instrument at another level (fader 0 -> -12 dB).
    void sameInstrumentAtAnotherLevel()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        Run run{changeTo(nullptr, {spec(piano)})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(piano, {}, -12.0)});
        run.play(blocksFor(0.5));
        const Measure m = measure(run.heard, at);
        report("same instrument, next patch 12 dB quieter", m);
        QVERIFY2(m.stepRatio <= kMostStep, "the level jumped (a click)");
        QCOMPARE(m.gapMs, 0.0);
    }

    // The instrument comes back (12 dB quieter) while it still rings as a tail.
    void anInstrumentComingBackFromItsTail()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto strings = std::make_shared<ToneNode>(330.0);
        Run run{changeTo(nullptr, {spec(piano)})};
        run.play(blocksFor(0.5), {on(60)});
        run.graph = changeTo(run.graph, {spec(strings)}); // the piano, its key held, rings on
        run.play(blocksFor(0.2));
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(piano, {}, -12.0)});
        run.play(blocksFor(0.5));
        const Measure m = measure(run.heard, at);
        report("instrument back from its tail, 12 dB quieter", m);
        QVERIFY2(m.stepRatio <= kMostStep, "the level jumped (a click)");
        QCOMPARE(m.gapMs, 0.0);
    }

    // The next patch keeps the instrument, without the echo, while it rings.
    void sameInstrumentWithoutItsEcho()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto echo = std::make_shared<EchoNode>();
        Run run{changeTo(nullptr, {spec(piano, {echo})})};
        run.play(blocksFor(0.5), {on(60)});
        run.play(blocksFor(0.05), {off(60)});
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(piano)});
        run.play(blocksFor(3.0));
        const Measure m = measure(run.heard, at);
        report("same instrument, next patch without its echo (ringing)", m);
        QVERIFY2(m.stepRatio <= kMostStep, "the echo was cut (a click)");
        QVERIFY2(m.jumpDb <= kMostJumpDb, "the echo was cut (the sound dropped)");
        QVERIFY2(m.ringsMs >= 0.9 * ringsWithEchoKept(), "the echo did not ring out");
    }

    // The same sound again (a song sharing the instrument and its effects).
    void theSameSoundAgain()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto echo = std::make_shared<EchoNode>();
        Run run{changeTo(nullptr, {spec(piano, {echo})})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph = changeTo(run.graph, {spec(piano, {echo})});
        run.play(blocksFor(0.5));
        const Measure m = measure(run.heard, at);
        report("same instrument and echo, key held", m);
        QVERIFY(m.stepRatio <= kMostStep);
        QVERIFY(m.jumpDb <= kMostJumpDb);
        QCOMPARE(m.gapMs, 0.0);
    }

    // Moved while playing: mute, a fader jump (a keyboard fader moved fast), solo.
    void muteWhilePlaying()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        Run run{changeTo(nullptr, {spec(piano)})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph->strip(0)->setMute(true);
        run.play(blocksFor(0.2));
        const Measure m = measure(run.heard, at);
        report("mute while playing", m);
        QVERIFY2(m.stepRatio <= kMostStep, "muting cut the sound (a click)");
        QVERIFY2(m.ringsMs <= 20.0, "muting took too long"); // silent within 20 ms
    }

    void faderJumpWhilePlaying()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        Run run{changeTo(nullptr, {spec(piano)})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph->strip(0)->setVolumeDb(-12.0);
        run.play(blocksFor(0.2));
        const Measure m = measure(run.heard, at);
        report("fader 0 -> -12 dB while playing", m);
        QVERIFY2(m.stepRatio <= kMostStep, "the fader jumped (a click)");
    }

    void soloAnotherWhilePlaying()
    {
        auto piano = std::make_shared<ToneNode>(220.0);
        auto pad = std::make_shared<ToneNode>(330.0);
        Run run{changeTo(nullptr, {spec(piano), spec(pad)})};
        run.play(blocksFor(0.5), {on(60)});
        const std::size_t at = run.now();
        run.graph->strip(1)->setSolo(true);
        run.play(blocksFor(0.2));
        const Measure m = measure(run.heard, at);
        report("solo another channel while playing", m);
        QVERIFY2(m.stepRatio <= kMostStep, "soloing cut the other channel (a click)");
    }
};

QTEST_GUILESS_MAIN(TestTransitions)
#include "tst_transitions.moc"
