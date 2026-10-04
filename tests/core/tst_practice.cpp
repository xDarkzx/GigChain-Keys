// The Practice tab's notes: a chart's chords as a timeline in beats, each
// chord voiced as a pianist plays it (bass in the left hand, the chord in
// the right hand moving as little as it can).
#include "gigchain/core/Chart.h"
#include "gigchain/core/Chords.h"
#include "gigchain/core/Practice.h"
#include "gigchain/core/SongMap.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

ChordShape chord(const QString& name)
{
    const auto shape = parseChordName(name);
    return shape.has_value() ? *shape : ChordShape{};
}

} // namespace

class TestPractice : public QObject
{
    Q_OBJECT

private slots:
    // The first chord near middle C, its bass in the octave of C2.
    void theFirstChordSitsAtMiddleC()
    {
        const Voicing c = voiceChord(chord(u"C"_s), {});
        QCOMPARE(c.bass, 36); // C2
        QCOMPARE(c.right, (std::vector<int>{60, 64, 67})); // C4 E4 G4
    }

    // The next chord: the inversion closest to the last one (C then F: C F A).
    void theNextChordMovesAsLittleAsItCan()
    {
        const Voicing f = voiceChord(chord(u"F"_s), {60, 64, 67});
        QCOMPARE(f.bass, 41); // F2
        QCOMPARE(f.right, (std::vector<int>{60, 65, 69}));
        const Voicing g = voiceChord(chord(u"G"_s), f.right);
        QCOMPARE(g.right, (std::vector<int>{62, 67, 71})); // B D G would leave C4; D G B stays close
    }

    // A slash chord's bass is the written one.
    void aSlashBassIsPlayed()
    {
        QCOMPARE(voiceChord(chord(u"D/F#"_s), {}).bass, 42); // F#2
    }

    // At most four notes in the right hand: the fifth goes first.
    void aBigChordKeepsFourNotes()
    {
        const Voicing maj9 = voiceChord(chord(u"Cmaj9"_s), {});
        QCOMPARE(maj9.right.size(), std::size_t{4});
        const auto has = [&maj9](int pc) { return std::ranges::any_of(maj9.right, [pc](int n) { return n % 12 == pc; }); };
        QVERIFY(has(0) && has(4) && has(11) && has(2)); // C E B D
        QVERIFY(!has(7));                                // no G
        for (int n : maj9.right) QVERIFY(n >= 55 && n <= 84);
    }

    // A section's bars shared between its chords; a count-in bar first.
    void aChartBecomesATimeline()
    {
        const Chart chart = parseChordPro(u"{comment: Verse}\n[C]a [F]b [G]c [Am]d\n{comment: Chorus}\n[F]e [G]f [C]g [G]h\n"_s);
        const SongMap map = buildSongMap(chart);
        const PracticeTimeline timeline = practiceTimeline(map, {4, 2}, {u"Verse"_s, u"Chorus"_s}, 4);
        QCOMPARE(timeline.chords.size(), std::size_t{8});
        QCOMPARE(timeline.chords.at(0).start, 4.0); // after the count-in bar
        QCOMPARE(timeline.chords.at(0).length, 4.0); // a bar each in the verse
        QCOMPARE(timeline.chords.at(0).name, u"C"_s);
        QCOMPARE(timeline.chords.at(4).start, 20.0);
        QCOMPARE(timeline.chords.at(4).length, 2.0); // half a bar each in the chorus
        QCOMPARE(timeline.length, 28.0);
        QCOMPARE(timeline.sections.size(), std::size_t{2});
        QCOMPARE(timeline.sections.at(1).start, 20.0);
        QCOMPARE(timeline.sections.at(1).name, u"Chorus"_s);
        // Each chord voiced from the one before.
        QCOMPARE(timeline.chords.at(1).right, (std::vector<int>{60, 65, 69}));
        QCOMPARE(timeline.chords.at(1).bass, 41);
    }

    // No sections: a bar a chord; no chords: nothing to play.
    void aPlainChartGetsABarAChord()
    {
        const PracticeTimeline timeline = practiceTimeline(buildSongMap(parseChordPro(u"[C]a [G]b"_s)), {}, {}, 3);
        QCOMPARE(timeline.chords.size(), std::size_t{2});
        QCOMPARE(timeline.chords.at(1).start, 6.0); // count-in 3, then 3
        QCOMPARE(timeline.chords.at(1).length, 3.0);
        QVERIFY(practiceTimeline(buildSongMap(parseChordPro(u"words only"_s)), {}, {}, 4).chords.empty());
    }
};

QTEST_GUILESS_MAIN(TestPractice)
#include "tst_practice.moc"
