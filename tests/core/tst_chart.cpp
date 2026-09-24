// Song charts: ChordPro, and plain "chords above the lyrics" sheets.
#include "gigchain/core/Chart.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

class TestChart : public QObject
{
    Q_OBJECT

private slots:
    void chordsSitOnTheWordsTheyChangeOn()
    {
        const Chart chart = parseChordPro(u"[Dm]I love [C#m7]you so much[D/E]"_s);
        QCOMPARE(chart.lines.size(), std::size_t{1});
        const ChartLine& line = chart.lines[0];
        QVERIFY(line.kind == ChartLine::Kind::Lyrics);
        QCOMPARE(line.segments.size(), std::size_t{3});
        QCOMPARE(line.segments[0].chord, u"Dm"_s);
        QCOMPARE(line.segments[0].text, u"I love "_s);
        QCOMPARE(line.segments[1].chord, u"C#m7"_s);
        QCOMPARE(line.segments[1].text, u"you so much"_s);
        QCOMPARE(line.segments[2].chord, u"D/E"_s);
        QCOMPARE(line.segments[2].text, QString());
    }

    void lyricsWithoutChordsAndChordsWithoutLyrics()
    {
        const Chart chart = parseChordPro(u"Just the words\n[Am] [F] [C] [G]"_s);
        QCOMPARE(chart.lines[0].segments.size(), std::size_t{1});
        QCOMPARE(chart.lines[0].segments[0].chord, QString());
        QCOMPARE(chart.lines[0].segments[0].text, u"Just the words"_s);
        QCOMPARE(chart.lines[1].chords(), (QStringList{u"Am"_s, u"F"_s, u"C"_s, u"G"_s}));
    }

    void directivesGiveTitleKeyTempoAndSections()
    {
        const Chart chart = parseChordPro(u"{title: Hallelujah}\n{artist: Leonard Cohen}\n{key: C}\n{tempo: 56}\n"
                                          "{start_of_chorus}\n[F]Halle[Am]lujah\n{end_of_chorus}\n{c: Softly}\n\n"_s);
        QCOMPARE(chart.title, u"Hallelujah"_s);
        QCOMPARE(chart.artist, u"Leonard Cohen"_s);
        QCOMPARE(chart.key, u"C"_s);
        QCOMPARE(chart.tempo, 56.0);
        // Title, artist, key and tempo stay in the chart as Meta lines (so
        // writing it back keeps them); the song follows.
        for (int i = 0; i < 4; ++i) QVERIFY(chart.lines[i].kind == ChartLine::Kind::Meta);
        QVERIFY(chart.lines[4].kind == ChartLine::Kind::Section);
        QCOMPARE(chart.lines[4].label, u"Chorus"_s);
        QVERIFY(chart.lines[5].kind == ChartLine::Kind::Lyrics);
        QVERIFY(chart.lines[6].kind == ChartLine::Kind::SectionEnd);
        QVERIFY(chart.lines[7].kind == ChartLine::Kind::Comment);
        QCOMPARE(chart.lines[7].label, u"Softly"_s);
        QVERIFY(chart.lines[8].kind == ChartLine::Kind::Blank);
    }

    void namedSections()
    {
        const Chart chart = parseChordPro(u"{start_of_verse: Verse 2}\n{sov}\n{start_of_bridge}"_s);
        QCOMPARE(chart.lines[0].label, u"Verse 2"_s);
        QCOMPARE(chart.lines[1].label, u"Verse"_s);
        QCOMPARE(chart.lines[2].label, u"Bridge"_s);
    }

    void chordsAboveLyricsBecomeChordPro()
    {
        // As copied from a chord sheet: chords on their own line, above the
        // word where they change.
        const QString sheet = u"[Verse 1]\n"
                              "Dm      C#m7   D/E\n"
                              "I love  you so much\n"
                              "\n"
                              "Am   F   C   G\n"_s;
        QCOMPARE(chordSheetToChordPro(sheet), u"{comment: Verse 1}\n"
                                               "[Dm]I love  [C#m7]you so [D/E]much\n"
                                               "\n"
                                               "[Am] [F] [C] [G]\n"_s);
    }

    void chordsPastTheEndOfTheLyricLine()
    {
        QCOMPARE(chordSheetToChordPro(u"C           G\nHello\n"_s), u"[C]Hello [G]\n"_s);
    }

    void ultimateGuitarMarkup()
    {
        // Ultimate Guitar's own markup when copied from its page source.
        // The F sits above "myself" once the markup is removed.
        const QString sheet = u"[ch]Am[/ch]          [ch]F[/ch]\nWhen I find myself\n[tab]x[/tab]"_s;
        QCOMPARE(chordSheetToChordPro(sheet), u"[Am]When I find [F]myself\nx\n"_s);
    }

    void aLyricLineIsNotMistakenForChords()
    {
        // "A" and "Am" are chords, but a line of words is lyrics.
        QVERIFY(isChordLine(u"Am  F  C  G"_s));
        QVERIFY(isChordLine(u"Cmaj7   Dsus4  E7(b9)  F#m7b5  Bb/D  N.C."_s));
        QVERIFY(!isChordLine(u"A day in the life"_s));
        QVERIFY(!isChordLine(u"Am I the only one"_s));
        QVERIFY(!isChordLine(u""_s));
    }

    void writingBackGivesTheSameChart()
    {
        const QString text = u"{title: Test}\n{key: G}\n[G]One [D]two\nlyrics\n"_s;
        QCOMPARE(toChordPro(parseChordPro(text)), text);
    }
};

QTEST_GUILESS_MAIN(TestChart)
#include "tst_chart.moc"
