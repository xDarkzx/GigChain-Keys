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

    // A chord's inversions for the right hand, from middle C up: root
    // position, then each next note of the chord at the bottom.
    void aChordHasItsInversions()
    {
        QCOMPARE(inversionCount(chord(u"C"_s)), 3);
        QCOMPARE(chordInversion(chord(u"C"_s), 0), (std::vector<int>{60, 64, 67})); // C E G
        QCOMPARE(chordInversion(chord(u"C"_s), 1), (std::vector<int>{64, 67, 72})); // E G C
        QCOMPARE(chordInversion(chord(u"C"_s), 2), (std::vector<int>{67, 72, 76})); // G C E
        QVERIFY(chordInversion(chord(u"C"_s), 3).empty());                          // a triad has three
        QCOMPARE(inversionCount(chord(u"Cmaj7"_s)), 4);
        QCOMPARE(chordInversion(chord(u"Cmaj7"_s), 3), (std::vector<int>{71, 72, 76, 79})); // B C E G
        // A slash chord: the inversions of its chord (the slash note is the left hand's).
        QCOMPARE(chordInversion(chord(u"E/D#"_s), 0), (std::vector<int>{64, 68, 71})); // E G# B
        QCOMPARE(inversionName(0), u"Root position"_s);
        QCOMPARE(inversionName(1), u"1st inversion"_s);
        QCOMPARE(inversionName(2), u"2nd inversion"_s);
        QCOMPARE(inversionName(3), u"3rd inversion"_s);
    }

    // What the left hand plays: the bass alone, its octave, root and fifth,
    // or the whole chord low (from the bass note, between G2 and F#3).
    void theLeftHandHasItsStyles()
    {
        QCOMPARE(leftHandNotes(chord(u"C"_s), LeftHand::Bass), (std::vector<int>{36}));
        QCOMPARE(leftHandNotes(chord(u"C"_s), LeftHand::Octave), (std::vector<int>{36, 48}));
        QCOMPARE(leftHandNotes(chord(u"C"_s), LeftHand::RootFifth), (std::vector<int>{36, 43}));
        QCOMPARE(leftHandNotes(chord(u"C"_s), LeftHand::Full), (std::vector<int>{48, 52, 55}));
        QCOMPARE(leftHandNotes(chord(u"E/D#"_s), LeftHand::Bass), (std::vector<int>{39})); // the slash note
        QCOMPARE(leftHandNotes(chord(u"E/D#"_s), LeftHand::Full), (std::vector<int>{51, 52, 56, 59})); // D# E G# B
    }

    // A timeline in a style: root position everywhere, an octave in the left
    // hand, and one chord (F) in the inversion chosen for it.
    void aTimelineFollowsTheStyle()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[C]a [F]b [G]c"_s));
        VoicingStyle root;
        root.right = RightHand::Root;
        root.left = LeftHand::Octave;
        const PracticeTimeline plain = practiceTimeline(map, {}, {}, 4, root);
        QCOMPARE(plain.chords.at(1).right, (std::vector<int>{65, 69, 72})); // F A C, root position
        QCOMPARE(plain.chords.at(1).left, (std::vector<int>{41, 53}));
        QCOMPARE(plain.chords.at(1).bass, 41);
        VoicingStyle chosen;
        chosen.right = RightHand::Chosen;
        chosen.chosen = {{u"F"_s, 2}};
        const PracticeTimeline mine = practiceTimeline(map, {}, {}, 4, chosen);
        QCOMPARE(mine.chords.at(1).right, chordInversion(chord(u"F"_s), 2)); // the chosen one
        QCOMPARE(mine.chords.at(0).right, (std::vector<int>{60, 64, 67}));   // the others: smooth
        QCOMPARE(mine.chords.at(1).left, (std::vector<int>{41}));            // the bass alone (the default)
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
