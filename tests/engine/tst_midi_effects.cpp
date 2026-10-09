#include "MidiEffects.h"

#include <QtTest>

#include <array>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;

namespace {

constexpr int kFrames = 4800; // 0.1 s at 48 kHz
constexpr double kRate = 48000.0;
constexpr double kTempo = 120.0; // a quarter = 0.5 s = 24000 samples; an eighth = 12000

MidiEvent key(int note, int velocity, int offset)
{
    return MidiEvent{.status = static_cast<uint8_t>(0x90), .data1 = static_cast<uint8_t>(note),
                     .data2 = static_cast<uint8_t>(velocity), .sampleOffset = offset};
}

struct Played
{
    int note;
    bool on;
    long long at; // samples from the start
};

// Runs `blocks` blocks of kFrames through the effects, with `keys` (by
// absolute sample) going in, and returns every note it played.
std::vector<Played> run(MidiEffects& effects, const std::vector<std::pair<long long, MidiEvent>>& keys, int blocks)
{
    std::vector<Played> played;
    std::array<MidiEvent, 64> out{};
    for (int b = 0; b < blocks; ++b) {
        const long long from = static_cast<long long>(b) * kFrames;
        std::vector<MidiEvent> in;
        for (const auto& [at, e] : keys) {
            if (at < from || at >= from + kFrames) continue;
            MidiEvent local = e;
            local.sampleOffset = static_cast<int32_t>(at - from);
            in.push_back(local);
        }
        const TimeInfo time{.tempo = kTempo, .sampleRate = kRate, .samplePosition = from,
                            .ppqPosition = static_cast<double>(from) * kTempo / 60.0 / kRate};
        const std::size_t n = effects.process(in, out, kFrames, time);
        for (std::size_t i = 0; i < n; ++i) {
            const MidiEvent& e = out.at(i);
            const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
            played.push_back(Played{.note = e.data1, .on = on, .at = from + e.sampleOffset});
        }
    }
    return played;
}

} // namespace

class TestMidiEffects : public QObject
{
    Q_OBJECT

private slots:
    // One key plays the chord, and letting go of it lets the whole chord go.
    void aKeyPlaysItsChordAndReleasesIt()
    {
        MidiEffects minor(MidiEffectSettings{.chord = static_cast<int>(core::ChordTrigger::Minor)});
        const auto played = run(minor, {{100, key(57, 90, 0)}, {3000, key(57, 0, 0)}}, 1);
        QCOMPARE(played.size(), std::size_t{6});
        QCOMPARE(played.at(0).note, 57); // A minor: A, C, E
        QCOMPARE(played.at(1).note, 60);
        QCOMPARE(played.at(2).note, 64);
        for (std::size_t i = 0; i < 3; ++i) QVERIFY(played.at(i).on && played.at(i).at == 100);
        for (std::size_t i = 3; i < 6; ++i) QVERIFY(!played.at(i).on && played.at(i).at == 3000);
    }

    // Up, eighths, two keys: the low key at once, then the high one on the
    // next eighth of the song's grid, and so on, each let go half way; when
    // the keys are up, it stops (its note let go at once).
    void anArpeggioStepsOnTheBeatAndStopsWithTheKeys()
    {
        MidiEffects arp(MidiEffectSettings{.arpeggio = static_cast<int>(core::ArpPattern::Up), .arpRate = 1, .arpOctaves = 1});
        // C and E held from sample 1000 (inside the first eighth), let go at 40000.
        const auto played = run(arp, {{1000, key(60, 100, 0)}, {1000, key(64, 100, 0)}, {40000, key(60, 0, 0)}, {40000, key(64, 0, 0)}},
                                12);
        std::vector<Played> ons;
        std::ranges::copy_if(played, std::back_inserter(ons), [](const Played& p) { return p.on; });
        QCOMPARE(ons.size(), std::size_t{4}); // at 1000, 12000, 24000, 36000
        QCOMPARE(ons.at(0).note, 60);
        QCOMPARE(ons.at(0).at, 1000LL); // the first at once, on the key
        QCOMPARE(ons.at(1).note, 64);
        QCOMPARE(ons.at(1).at, 12000LL); // then on the eighths
        QCOMPARE(ons.at(2).note, 60);
        QCOMPARE(ons.at(2).at, 24000LL);
        QCOMPARE(ons.at(3).at, 36000LL);
        // Every note let go, the last when the keys went up; nothing after.
        QCOMPARE(std::ranges::count_if(played, [](const Played& p) { return !p.on; }), 4);
        QVERIFY(std::ranges::all_of(played, [](const Played& p) { return p.at <= 40000; }));
        // The second note let go half an eighth after it started.
        const auto secondOff = std::ranges::find_if(played, [](const Played& p) { return !p.on && p.note == 64; });
        QVERIFY(secondOff != played.end());
        QCOMPARE(secondOff->at, 18000LL);
    }

    // Up and down over two octaves of one key: C, C', C, C'...; down plays from the top.
    void patternsAndOctaves()
    {
        MidiEffects upDown(MidiEffectSettings{.arpeggio = static_cast<int>(core::ArpPattern::UpDown), .arpRate = 3, .arpOctaves = 3});
        const auto played = run(upDown, {{0, key(48, 100, 0)}}, 8); // sixteenths: a step every 6000 samples
        std::vector<int> notes;
        for (const Played& p : played) {
            if (p.on) notes.push_back(p.note);
        }
        QVERIFY(notes.size() >= 6);
        QCOMPARE((std::vector<int>(notes.begin(), notes.begin() + 6)), (std::vector<int>{48, 60, 72, 60, 48, 60}));

        MidiEffects down(MidiEffectSettings{.arpeggio = static_cast<int>(core::ArpPattern::Down), .arpRate = 3, .arpOctaves = 2});
        const auto fromTop = run(down, {{0, key(48, 100, 0)}}, 1);
        QCOMPARE(fromTop.front().note, 60);
    }
};

QTEST_GUILESS_MAIN(TestMidiEffects)
#include "tst_midi_effects.moc"
