// A chart as the chords a player goes through (chord follow).
#include "gigchain/core/Chart.h"
#include "gigchain/core/SongMap.h"

#include <QtTest>

#include <algorithm>
#include <iterator>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

QStringList names(const SongMap& map)
{
    QStringList out;
    for (const SongStep& step : map.steps) out << step.name;
    return out;
}

std::vector<int> sectionsOf(const SongMap& map)
{
    std::vector<int> out;
    out.reserve(map.steps.size());
    std::ranges::transform(map.steps, std::back_inserter(out), &SongStep::section);
    return out;
}

using Places = std::vector<std::pair<int, int>>;

} // namespace

class TestSongMap : public QObject
{
    Q_OBJECT

private slots:
    void everyChordInOrderWithItsSection()
    {
        const SongMap map = buildSongMap(parseChordPro(
            u"{sov: Verse 1}\n[Am]One [F]two\n[C]three [G]four\n{eov}\n{soc: Chorus}\n[F]Chorus [C]line\n{eoc}\n"_s));
        QCOMPARE(names(map), (QStringList{u"Am"_s, u"F"_s, u"C"_s, u"G"_s, u"F"_s, u"C"_s}));
        QCOMPARE(sectionsOf(map), (std::vector<int>{0, 0, 0, 0, 1, 1}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{0, 4}));
        QCOMPARE(map.steps.at(0).places, (Places{{1, 0}}));
        QCOMPARE(map.steps.at(3).places, (Places{{2, 1}}));
        QVERIFY(map.followable());
    }

    // The song's parts in playing order: by default each section as the
    // chart writes them; each step knows its part.
    void theChartsOrderIsTheFlowByDefault()
    {
        const SongMap map = buildSongMap(parseChordPro(
            u"{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n{comment: Verse 2}\n[Am]e [F]f\n"_s));
        QCOMPARE(map.partStarts, (std::vector<int>{0, 2, 4}));
        QCOMPARE(map.steps.at(3).part, 1);
        QCOMPARE(map.steps.at(4).part, 2);
    }

    // A flow of the song's own: the sections in the order it is played,
    // one played again (the chorus twice at the end); unknown names left out.
    void aFlowPlaysTheSectionsInItsOrder()
    {
        const Chart chart = parseChordPro(
            u"{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n{comment: Verse 2}\n[Dm]e [E]f\n"_s);
        const std::vector<SectionRef> flow{{.name = u"Verse 1"_s}, {.name = u"Verse 2"_s}, {.name = u"chorus"_s},
                                           {.name = u"Chorus"_s}, {.name = u"Bridge"_s}};
        const SongMap map = buildSongMap(chart, flow);
        QCOMPARE(names(map), (QStringList{u"Am"_s, u"F"_s, u"Dm"_s, u"E"_s, u"C"_s, u"G"_s, u"C"_s, u"G"_s}));
        QCOMPARE(sectionsOf(map), (std::vector<int>{0, 0, 2, 2, 1, 1, 1, 1}));
        QCOMPARE(map.partStarts, (std::vector<int>{0, 2, 4, 6})); // the chorus twice: two parts
        QCOMPARE(map.sectionStarts, (std::vector<int>{0, 4, 2})); // each section's first time
        // The chorus's chords lit in the same places, both times.
        QCOMPARE(map.steps.at(4).places, map.steps.at(6).places);
        // The chorus's last G and the next chorus's C stay apart (two parts).
        QCOMPARE(map.steps.at(5).part, 2);
        QCOMPARE(map.steps.at(6).part, 3);
    }

    // Each part knows its place in the flow, though parts without chords (a
    // spoken intro, a section the chart no longer has) are not parts to follow.
    void eachPartKnowsItsPlaceInTheFlow()
    {
        const Chart chart = parseChordPro(
            u"[G]count in\n{comment: Intro}\nspoken words\n{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n"_s);
        const SongMap byChart = buildSongMap(chart);
        QCOMPARE(byChart.partStarts, (std::vector<int>{0, 1, 3}));
        QCOMPARE(byChart.partFlow, (std::vector<int>{-1, 1, 2})); // the chords before Intro; Intro has none
        const std::vector<SectionRef> flow{{.name = u"Intro"_s}, {.name = u"Bridge"_s}, {.name = u"Verse 1"_s},
                                           {.name = u"Chorus"_s}, {.name = u"Chorus"_s}};
        const SongMap byFlow = buildSongMap(chart, flow);
        QCOMPARE(byFlow.partFlow, (std::vector<int>{-1, 2, 3, 4}));
        QCOMPARE(byFlow.partFlow.size(), byFlow.partStarts.size());
    }

    void repeatsArePlayedAgain()
    {
        // A line of chords "(x2)", and a section "Chorus (x2)".
        const SongMap line = buildSongMap(parseChordPro(u"[Am]  [G]  (x2)\n"_s));
        QCOMPARE(names(line), (QStringList{u"Am"_s, u"G"_s, u"Am"_s, u"G"_s}));
        const SongMap section = buildSongMap(parseChordPro(u"{soc: Chorus (x2)}\n[F]a [C]b\n{eoc}\n"_s));
        QCOMPARE(names(section), (QStringList{u"F"_s, u"C"_s, u"F"_s, u"C"_s}));
        QCOMPARE(section.sectionStarts, (std::vector<int>{0}));
    }

    void theSameChordTwiceIsOneStep()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[C]Hello [C]world [G]now [C]then [C/E]bass moves\n"_s));
        QCOMPARE(names(map), (QStringList{u"C"_s, u"G"_s, u"C"_s, u"C/E"_s}));
        QCOMPARE(map.steps.at(0).places, (Places{{0, 0}, {0, 1}})); // lit in both places
    }

    void aSectionsFirstChordIsNeverMergedIntoThePrevious()
    {
        const SongMap map = buildSongMap(parseChordPro(u"{sov: Verse}\n[Am]a [G]b\n{eov}\n{soc: Chorus}\n[G]c [C]d\n{eoc}\n"_s));
        QCOMPARE(names(map), (QStringList{u"Am"_s, u"G"_s, u"G"_s, u"C"_s}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{0, 2}));
    }

    void unreadableChordsAreNotSteps()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[C]a [N.C.]b [G]c\n"_s));
        QCOMPARE(names(map), (QStringList{u"C"_s, u"G"_s}));
        QCOMPARE(map.steps.at(1).places, (Places{{0, 2}})); // still where it is written
    }

    void chordsBeforeTheFirstSectionBelongToNone()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[D]intro\n{c: Verse}\n[A]words\n"_s));
        QCOMPARE(sectionsOf(map), (std::vector<int>{-1, 0}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{1}));
    }

    void aSectionWithoutChords()
    {
        const SongMap map = buildSongMap(parseChordPro(u"{c: Intro}\nspoken words\n{c: Verse}\n[A]x [D]y\n"_s));
        QCOMPARE(map.sectionStarts, (std::vector<int>{-1, 0}));
        QVERIFY(!buildSongMap(parseChordPro(u"[C]only one"_s)).followable());
        QVERIFY(!buildSongMap(parseChordPro(QString())).followable());
    }

    // The longest chart a setlist may hold, its chords repeated as often as
    // repeat marks allow: read in a moment (the app does not freeze), and
    // too long to follow.
    void theLongestChartIsReadQuickly_data()
    {
        QTest::addColumn<QString>("chord");
        QTest::newRow("one chord, merged") << u"[C]"_s;
        QTest::newRow("two chords, alternating") << u"[C][D]"_s;
    }
    void theLongestChartIsReadQuickly()
    {
        QFETCH(QString, chord);
        QString chart = u"{c: Verse x16}\n"_s;
        while (chart.size() < 99'000) chart += chord;
        chart += u" x16\n"_s;
        const Chart parsed = parseChordPro(chart);
        QElapsedTimer timer;
        timer.start();
        const SongMap map = buildSongMap(parsed);
        const qint64 ms = timer.elapsed();
        qInfo() << "song map of" << chart.size() << "characters:" << ms << "ms," << map.steps.size() << "steps";
        QVERIFY2(ms < 500, qPrintable(u"%1 ms"_s.arg(ms)));
        QVERIFY(map.tooLong);
        QVERIFY(!map.followable());
    }

    // More sections than the engine follows (one bit each): not followed,
    // like too many chords, never a map the engine refuses. (Found by the
    // chord follow fuzzer: 65 bridge marks.)
    void moreSectionsThanCanBeFollowedIsTooLong()
    {
        QString chart;
        for (int i = 0; i < 65; ++i) chart += u"{sob}\n[C]a [G]b\n"_s;
        const SongMap map = buildSongMap(parseChordPro(chart));
        QVERIFY(map.tooLong);
        QVERIFY(map.steps.empty());
        QVERIFY(!map.followable());
        QCOMPARE(map.sectionStarts.size(), std::size_t{65}); // (still one per section of the chart)

        QString most;
        for (int i = 0; i < 64; ++i) most += u"{sob}\n[C]a [G]b\n"_s;
        QVERIFY(buildSongMap(parseChordPro(most)).followable());
    }
};

QTEST_GUILESS_MAIN(TestSongMap)
#include "tst_song_map.moc"
