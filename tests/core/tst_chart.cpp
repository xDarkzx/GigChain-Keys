// Song charts: ChordPro, and plain "chords above the lyrics" sheets.
#include "gigchain/core/Chart.h"

#include <QFile>
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

    void aMessyPastedSheetComesOutClean()
    {
        // Typical copy from a chord site: header junk, tab staff lines,
        // separator rows, non-breaking spaces, tabs and too many blank lines.
        const QString pasted = u"Hallelujah chords by Leonard Cohen\n"
                               "Tabbed by someone\n"
                               "Tuning: E A D G B E\n"
                               "Capo: 2\n"
                               "Key: C\n"
                               "-------------------------------------\n"
                               "\n"
                               "\n"
                               "[Intro]\n"
                               "e|-----0-----0---|\n"
                               "B|---1-----1-----|\n"
                               "G|-0-----0-------|\n"
                               "\n"
                               "[Verse 1]\n"
                               "C                     Am\n" // Am above "secret"
                               "I heard there was   a secret chord   \n"
                               "=====================\n"
                               "\n\n\n"
                               "F     G              C\n" // C past the end of the words
                               "That David   played\n"_s;
        QCOMPARE(tidyChordSheet(pasted), u"Hallelujah chords by Leonard Cohen\n"
                                          "{comment: Capo 2}\n"
                                          "{key: C}\n"
                                          "\n"
                                          "{comment: Intro}\n"
                                          "\n"
                                          "{comment: Verse 1}\n"
                                          "[C]I heard there was a [Am]secret chord\n"
                                          "\n"
                                          "[F]That D[G]avid played [C]\n"_s);
    }

    void tabsCountAsColumns()
    {
        // A tab character moves to the next multiple of 8, as in a text editor.
        QCOMPARE(tidyChordSheet(u"C\tG\nHello there\n"_s), u"[C]Hello th[G]ere\n"_s);
    }

    void chordProIsOnlyTidied()
    {
        const QString chordPro = u"{title: X}\n[G]One   [D]two   \n\n\n\nthree\n"_s;
        QCOMPARE(tidyChordSheet(chordPro), u"{title: X}\n[G]One [D]two\n\nthree\n"_s);
    }

    void aChordOverTheSpaceBeforeAWordGoesOnTheWord()
    {
        QCOMPARE(tidyChordSheet(u"C        G\nHello my   friend\n"_s), u"[C]Hello my [G]friend\n"_s);
    }

    void aCopiedChordPageKeepsOnlyTheSong()
    {
        // Laid out the way a chord site's page copies: header clutter, the
        // song, then footer clutter.
        const QString page = u"Hallelujah Chords by Leonard Cohen\n"
                             "Leonard Cohen\n"
                             "Tuning: E A D G B E\n"
                             "Key: C\n"
                             "Capo: 2nd fret\n"
                             "BPM: 56\n"
                             "Author: someone 12,345. 3 contributors total, last edit on Jan 1, 2021\n"
                             "View official tab\n"
                             "We have an official Hallelujah tab made by UG professional guitarists.\n"
                             "Difficulty: novice\n"
                             "Chords used: C Am F G\n"
                             "Am  x02210\n"
                             "[Intro]\n"
                             "C  Am  C  Am\n"
                             "\n"
                             "[Verse 1]\n"
                             "C                   Am\n" // Am above "secret"
                             "I heard there was a secret chord\n"
                             "\n"
                             "Last update: Jan 1, 2021\n"
                             "Rating\n"
                             "4.9\n"
                             "Please, rate this tab\n"
                             "12 Comments\n"_s;
        const ImportedSheet sheet = importChordSheet(page);
        QCOMPARE(sheet.title, u"Hallelujah"_s);
        QCOMPARE(sheet.artist, u"Leonard Cohen"_s);
        QCOMPARE(sheet.key, u"C"_s);
        QCOMPARE(sheet.capo, 2);
        QCOMPARE(sheet.tempo, 56.0);
        QCOMPARE(sheet.chart, u"{comment: Capo 2}\n"
                               "{comment: Intro}\n"
                               "[C] [Am] [C] [Am]\n"
                               "\n"
                               "{comment: Verse 1}\n"
                               "[C]I heard there was a [Am]secret chord\n"_s);
    }

    void aSheetWithoutSectionsStartsAtItsFirstChords()
    {
        const ImportedSheet sheet = importChordSheet(u"Wonderwall - Oasis\n"
                                                     "Some site text\n"
                                                     "Em7        G\n"
                                                     "Today is gonna be the day\n"_s);
        QCOMPARE(sheet.title, u"Wonderwall"_s);
        QCOMPARE(sheet.artist, u"Oasis"_s);
        QCOMPARE(sheet.chart, u"[Em7]Today is go[G]nna be the day\n"_s);
    }

    void chordProKeepsItsTitle()
    {
        const ImportedSheet sheet = importChordSheet(u"{title: Let It Be}\n{artist: The Beatles}\n[C]When I find\n"_s);
        QCOMPARE(sheet.title, u"Let It Be"_s);
        QCOMPARE(sheet.artist, u"The Beatles"_s);
        QVERIFY(sheet.chart.contains(u"[C]When I find"_s));
    }

    void realUltimateGuitarPage()
    {
        // Copied from ultimate-guitar.com by the user (Creep, Radiohead).
        QFile file(QFINDTESTDATA("data/ug_creep.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const ImportedSheet sheet = importChordSheet(QString::fromUtf8(file.readAll()));
        QCOMPARE(sheet.title, u"Creep"_s);
        QCOMPARE(sheet.artist, u"Radiohead"_s);
        QCOMPARE(sheet.key, u"G"_s);
        QCOMPARE(sheet.capo, 0); // "Capo:No capo"
        // The chart starts at [Intro]: views, difficulty, tuning, the chord
        // list, strumming counts and chord shapes above it are gone.
        QVERIFY2(sheet.chart.startsWith(u"{comment: Intro}\n[G] [B] [C] [Cm]\n"_s), qPrintable(sheet.chart.left(120)));
        for (const QString& clutter : {u"views"_s, u"Difficulty"_s, u"Tuning"_s, u"Strumming"_s, u"3-5-5-4-3-3"_s,
                                       u"contributors"_s, u"official"_s}) {
            QVERIFY2(!sheet.chart.contains(clutter), qPrintable(clutter));
        }
        // Chords over the syllables the site put them on.
        QVERIFY(sheet.chart.contains(u"When you were here be[G]fore, couldn't look you in the [B]eyes"_s));
        // "[Chorus] (play loud)" is a heading with its note, not a chord.
        QVERIFY(sheet.chart.contains(u"{comment: Chorus (play loud)}"_s));
        QVERIFY(!sheet.chart.contains(u"[Chorus]"_s));
        QVERIFY(sheet.chart.contains(u"{comment: Verse 3 (play soft until the end)}"_s));
        QVERIFY(sheet.chart.trimmed().endsWith(u"I don't be[G]long here"_s)); // the last G sits over "long"
    }

    // Sections: what each gets called, which one it is, and how many bars.
    void sectionsFromAPastedSheet()
    {
        const ImportedSheet sheet = importChordSheet(u"[Intro]\n"
                                                     "C  G\n"
                                                     "[Verse 1]\n"
                                                     "C        G\n"
                                                     "Hello there\n"
                                                     "Am       F\n"
                                                     "Goodbye now\n"
                                                     "[Chorus] (x2)\n"
                                                     "F  G  C\n"_s);
        const auto sections = chartSections(parseChordPro(sheet.chart));
        QCOMPARE(sections.size(), std::size_t{3});
        QCOMPARE(sections.at(0).name, u"Intro"_s);
        QCOMPARE(sections.at(0).guessedBars, 2);
        QCOMPARE(sections.at(1).name, u"Verse 1"_s);
        QCOMPARE(sections.at(1).guessedBars, 4);
        QCOMPARE(sections.at(2).name, u"Chorus"_s);
        QCOMPARE(sections.at(2).label, u"Chorus (x2)"_s);
        QCOMPARE(sections.at(2).guessedBars, 6); // three chords, twice
        for (const ChartSection& s : sections) QCOMPARE(s.occurrence, 1);
        // Each points at its own title line.
        const Chart chart = parseChordPro(sheet.chart);
        QCOMPARE(chart.lines.at(static_cast<std::size_t>(sections.at(1).line)).label, u"Verse 1"_s);
    }

    void sectionsFromChordPro()
    {
        const auto sections = chartSections(parseChordPro(u"{soc}\n[C]la [G]la\n{eoc}\n"
                                                          "{sov: Verse 2}\n[Am]words\n{eov}\n"
                                                          "{start_of_chorus}\n[C]la [G]la\n{end_of_chorus}\n"_s));
        QCOMPARE(sections.size(), std::size_t{3});
        QCOMPARE(sections.at(0).name, u"Chorus"_s);
        QCOMPARE(sections.at(0).occurrence, 1);
        QCOMPARE(sections.at(1).name, u"Verse 2"_s);
        QCOMPARE(sections.at(1).guessedBars, 1);
        QCOMPARE(sections.at(2).name, u"Chorus"_s);
        QCOMPARE(sections.at(2).occurrence, 2); // the second chorus is its own section

        // A tab block is notation, not a part of the song.
        const auto withTab = chartSections(parseChordPro(u"{sov}\n[C]la\n{eov}\n{sot}\ne|--3--|\n{eot}\n"_s));
        QCOMPARE(withTab.size(), std::size_t{1});
        QCOMPARE(withTab.at(0).name, u"Verse"_s);
    }

    void commentsThatAreNotSections()
    {
        QVERIFY(isSectionName(u"Verse"_s));
        QVERIFY(isSectionName(u"pre-chorus"_s));
        QVERIFY(isSectionName(u"Pre Chorus 2"_s));
        QVERIFY(isSectionName(u"Chorus (x2)"_s));
        QVERIFY(isSectionName(u"Outro"_s));
        QVERIFY(isSectionName(u"Drop"_s));
        QVERIFY(!isSectionName(u"Capo 2"_s));
        QVERIFY(!isSectionName(u"play softly"_s));
        QVERIFY(!isSectionName(u"Versed in song"_s));
        const auto sections = chartSections(parseChordPro(u"{comment: Capo 2}\n{comment: Verse}\n[C]words\n"_s));
        QCOMPARE(sections.size(), std::size_t{1});
        QCOMPARE(sections.at(0).name, u"Verse"_s);
    }

    void aSectionWithoutChordsGuessesFourBars()
    {
        const auto sections = chartSections(parseChordPro(u"{comment: Intro}\n{comment: Verse}\njust words\n"_s));
        QCOMPARE(sections.size(), std::size_t{2});
        QCOMPARE(sections.at(0).guessedBars, 4);
        QCOMPARE(sections.at(1).guessedBars, 4);
    }

    void aRepeatedLineCountsTwice()
    {
        const auto chordPro = chartSections(parseChordPro(u"{comment: Intro}\n[Am] [F] x2\n[C]\n"_s));
        QCOMPARE(chordPro.at(0).guessedBars, 5);
        // The repeat mark survives a pasted sheet's chord-only line.
        const ImportedSheet sheet = importChordSheet(u"[Intro]\nAm  F  x2\n"_s);
        const auto pasted = chartSections(parseChordPro(sheet.chart));
        QCOMPARE(pasted.size(), std::size_t{1});
        QCOMPARE(pasted.at(0).guessedBars, 4);
    }

    void tempoAndTimeFromASheet()
    {
        const ImportedSheet a = importChordSheet(u"My Song Chords by Someone\nTempo: 96\nTime: 6/8\n[Verse]\nC G\n"_s);
        QCOMPARE(a.tempo, 96.0);
        QCOMPARE(a.timeNumerator, 6);
        QCOMPARE(a.timeDenominator, 8);
        const ImportedSheet b = importChordSheet(u"Other Song\n88 BPM\n[Verse]\nC G\n"_s);
        QCOMPARE(b.tempo, 88.0);
        QCOMPARE(b.timeNumerator, 0); // not given
        const ImportedSheet c = importChordSheet(u"{title: X}\n{tempo: 120}\n{time: 3/4}\n[C]la\n"_s);
        QCOMPARE(c.tempo, 120.0);
        QCOMPARE(c.timeNumerator, 3);
        QCOMPARE(c.timeDenominator, 4);
        const ImportedSheet bad = importChordSheet(u"{time: 5/5}\n[C]la\n"_s);
        QCOMPARE(bad.timeNumerator, 0); // not a time signature
    }

    void writingBackGivesTheSameChart()
    {
        const QString text = u"{title: Test}\n{key: G}\n[G]One [D]two\nlyrics\n"_s;
        QCOMPARE(toChordPro(parseChordPro(text)), text);
    }
};

QTEST_GUILESS_MAIN(TestChart)
#include "tst_chart.moc"
