// Editing a chart in place, as the player does in the Chart tab: chords put
// over a word, dragged to another word, renamed, removed; words typed with
// the chords staying over them; lines split and joined; sections named.
#include "gigchain/core/ChartEdit.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

class TestChartEdit : public QObject
{
    Q_OBJECT

private slots:
    // "I need your love", a Gm dropped on "love": over its first letter.
    void aChordIsPlacedOverAWord()
    {
        const auto placed = placeChord(u"I need your love"_s, 0, 12, u"Gm"_s);
        QVERIFY2(placed.has_value(), placed ? "" : qPrintable(placed.error().message));
        QCOMPARE(*placed, u"I need your [Gm]love"_s);
        // Another before it, on "I".
        QCOMPARE(*placeChord(*placed, 0, 0, u"C"_s), u"[C]I need your [Gm]love"_s);
        // After the last word: at the end of the line.
        QCOMPARE(*placeChord(u"I need your love"_s, 0, 16, u"D"_s), u"I need your love[D]"_s);
    }

    // Dropped part way through a word: onto the word's first letter.
    void aDropLandsOnTheStartOfTheWord()
    {
        QCOMPARE(wordStartAt(u"I need your love"_s, 14), 12); // "lo|ve"
        QCOMPARE(wordStartAt(u"I need your love"_s, 12), 12);
        QCOMPARE(wordStartAt(u"I need your love"_s, 11), 12); // the space before "love": the word after
        QCOMPARE(wordStartAt(u"I need your love"_s, 3), 2);   // "ne|ed"
        QCOMPARE(wordStartAt(u"I need your love"_s, 99), 16); // past the end: the end
        QCOMPARE(wordStartAt(u""_s, 0), 0);
    }

    // Dragged from "your" to "love".
    void aChordIsMovedAlongItsLine()
    {
        const QString chart = u"[C]I need [Gm]your love"_s;
        const auto moved = moveChord(chart, 0, 1, 0, 12);
        QVERIFY2(moved.has_value(), moved ? "" : qPrintable(moved.error().message));
        QCOMPARE(*moved, u"[C]I need your [Gm]love"_s);
        // Back to the start of the line, before the C.
        QCOMPARE(*moveChord(chart, 0, 1, 0, 0), u"[Gm][C]I need your love"_s);
    }

    // To another line.
    void aChordIsMovedToAnotherLine()
    {
        const auto moved = moveChord(u"[Am]First line\nSecond line"_s, 0, 0, 1, 7);
        QVERIFY(moved.has_value());
        QCOMPARE(*moved, u"First line\nSecond [Am]line"_s);
    }

    void aChordIsRenamedAndRemoved()
    {
        QCOMPARE(*changeChord(u"I need [Gm]your love"_s, 0, 0, u"Gm7"_s), u"I need [Gm7]your love"_s);
        QCOMPARE(*changeChord(u"I need [Gm]your love"_s, 0, 0, u""_s), u"I need your love"_s);
        // A name with brackets in it would break the chart: refused.
        const auto broken = changeChord(u"I need [Gm]your love"_s, 0, 0, u"G]m"_s);
        QVERIFY(!broken);
        QVERIFY(broken.error().code == ErrorCode::InvalidData);
    }

    // Words typed: the chords stay over the words they were over.
    void typingKeepsTheChordsOverTheirWords()
    {
        const QString chart = u"[C]I need your [Gm]love"_s;
        // A word added before "love": Gm moves along with "love".
        QCOMPARE(*editLyrics(chart, 0, u"I need all your love"_s), u"[C]I need all your [Gm]love"_s);
        // A word taken away before it.
        QCOMPARE(*editLyrics(chart, 0, u"I need love"_s), u"[C]I need [Gm]love"_s);
        // A word typed right before a chord's word: the chord stays on its word.
        QCOMPARE(*editLyrics(u"I need [Gm]your love"_s, 0, u"I need all your love"_s), u"I need all [Gm]your love"_s);
        // Words added at the end: nothing moves.
        QCOMPARE(*editLyrics(chart, 0, u"I need your love tonight"_s), u"[C]I need your [Gm]love tonight"_s);
        // A blank line typed into becomes a line of words.
        QCOMPARE(*editLyrics(u"{comment: Verse}\n\nNext"_s, 1, u"New words"_s), u"{comment: Verse}\nNew words\nNext"_s);
    }

    // Enter part way through a line: two lines, each keeping its chords.
    void enterSplitsALine()
    {
        const auto split = splitLyricLine(u"[C]I need your [Gm]love"_s, 0, 7);
        QVERIFY(split.has_value());
        QCOMPARE(*split, u"[C]I need \nyour [Gm]love"_s);
        // At the end: a new empty line under it.
        QCOMPARE(*splitLyricLine(u"Line one\nLine three"_s, 0, 8), u"Line one\n\nLine three"_s);
    }

    // Backspace at the start of a line: joined to the line above.
    void backspaceJoinsALineToTheOneAbove()
    {
        const auto joined = joinWithPrevious(u"[C]I need \nyour [Gm]love"_s, 1);
        QVERIFY(joined.has_value());
        QCOMPARE(*joined, u"[C]I need your [Gm]love"_s);
        // The first line has nothing above: refused, said.
        QVERIFY(!joinWithPrevious(u"Only line"_s, 0));
    }

    void aSectionIsRenamed()
    {
        QCOMPARE(*renameSection(u"{comment: Verse}\nWords"_s, 0, u"Verse 1"_s), u"{comment: Verse 1}\nWords"_s);
        QCOMPARE(*renameSection(u"{start_of_chorus}\nWords\n{end_of_chorus}"_s, 0, u"Big Chorus"_s),
                 u"{start_of_chorus: Big Chorus}\nWords\n{end_of_chorus}"_s);
        QCOMPARE(*renameSection(u"{soc: Chorus}\nWords"_s, 0, u"Last Chorus"_s), u"{soc: Last Chorus}\nWords"_s);
        // A line of words is not a section.
        QVERIFY(!renameSection(u"Words"_s, 0, u"Verse"_s));
        // Nor an empty name.
        QVERIFY(!renameSection(u"{comment: Verse}"_s, 0, u"  "_s));
    }

    // "+ Section": a new section at the end, with a line to type into.
    void aSectionIsAdded()
    {
        QCOMPARE(*appendSection(u"[C]Words"_s, u"Chorus"_s), u"[C]Words\n\n{comment: Chorus}\n"_s);
        QCOMPARE(*appendSection(u""_s, u"Verse"_s), u"{comment: Verse}\n"_s);
        const Chart parsed = parseChordPro(*appendSection(u"[C]Words"_s, u"Chorus"_s));
        QCOMPARE(chartSections(parsed).size(), std::size_t{1});
    }

    // Lines are counted as the parsed chart counts them; a line that is not
    // there, or not words, is refused with the reason (never a silent no-op).
    void aLineThatIsNotThereIsRefused()
    {
        const auto missing = placeChord(u"Words"_s, 5, 0, u"C"_s);
        QVERIFY(!missing);
        QVERIFY(missing.error().code == ErrorCode::OutOfRange);
        const auto notWords = placeChord(u"{comment: Verse}"_s, 0, 0, u"C"_s);
        QVERIFY(!notWords);
        QVERIFY(notWords.error().code == ErrorCode::InvalidData);
        QVERIFY(!changeChord(u"[C]Words"_s, 0, 3, u"D"_s)); // no fourth chord
    }

    // The lyrics and where each chord sits, as the editor shows them.
    void aLineIsReadAsWordsAndPlacedChords()
    {
        const Chart chart = parseChordPro(u"[C]I need [Gm][D]your love[A]"_s);
        const LyricLine line = lyricLineOf(chart.lines.at(0));
        QCOMPARE(line.lyrics, u"I need your love"_s);
        QCOMPARE(line.chords.size(), std::size_t{4});
        QCOMPARE(line.chords.at(0).at, 0);
        QCOMPARE(line.chords.at(1).at, 7);
        QCOMPARE(line.chords.at(2).at, 7);
        QCOMPARE(line.chords.at(2).name, u"D"_s);
        QCOMPARE(line.chords.at(3).at, 16);
    }
};

QTEST_GUILESS_MAIN(TestChartEdit)
#include "tst_chart_edit.moc"
