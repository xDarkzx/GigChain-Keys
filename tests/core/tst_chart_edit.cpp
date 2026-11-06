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

    // Any name keeps it a section (issue #23): a name a chart would not call
    // a section ("Big Ending", "Tag") is written as a section of its own,
    // never left as a note that ends the section and cannot be removed.
    void aSectionKeepsBeingASectionWhateverItIsCalled()
    {
        const QString chart = u"{comment: Verse}\n[C]Words\n\n{comment: Chorus}\n[F]More"_s;
        for (const QString& name : {u"Big Ending"_s, u"Tag"_s, u"Vamp out"_s, u"Verse 2"_s}) {
            const auto renamed = renameSection(chart, 3, name);
            QVERIFY2(renamed.has_value(), qPrintable(name));
            const auto sections = chartSections(parseChordPro(*renamed));
            QCOMPARE(sections.size(), std::size_t{2});
            QCOMPARE(sections.at(1).label, name);
            QCOMPARE(sections.at(1).line, 3);
            // And again: still a section, renamed in place.
            const auto again = renameSection(*renamed, 3, u"Outro"_s);
            QVERIFY(again.has_value());
            QCOMPARE(chartSections(parseChordPro(*again)).at(1).label, u"Outro"_s);
        }
        // Added under a name of its own: a section too.
        const auto added = appendSection(u"[C]Words"_s, u"Big Ending"_s);
        QVERIFY(added.has_value());
        const auto sections = chartSections(parseChordPro(*added));
        QCOMPARE(sections.size(), std::size_t{1});
        QCOMPARE(sections.front().label, u"Big Ending"_s);
    }

    // A section title (or a note line) taken out: its words join the section
    // above; a {start_of_...} section's closing line goes with it. Words are
    // never taken out this way.
    void aSectionTitleOrANoteIsRemoved()
    {
        QCOMPARE(*removeSectionTitle(u"{comment: Verse}\n[C]One\n{comment: Big Ending}\n[F]Two"_s, 2), u"{comment: Verse}\n[C]One\n[F]Two"_s);
        QCOMPARE(*removeSectionTitle(u"[C]One\n{start_of_section: Tag}\n[F]Two\n{end_of_section}\n[G]Three"_s, 1),
                 u"[C]One\n[F]Two\n[G]Three"_s);
        QCOMPARE(*removeSectionTitle(u"{c: Capo 2}\n[C]One"_s, 0), u"[C]One"_s);
        QVERIFY(!removeSectionTitle(u"[C]One"_s, 0));   // words: refused
        QVERIFY(!removeSectionTitle(u"[C]One"_s, 7));   // not there: refused
        const auto sections = chartSections(parseChordPro(*removeSectionTitle(u"{comment: Verse}\n[C]One\n{comment: Chorus}\n[F]Two"_s, 2)));
        QCOMPARE(sections.size(), std::size_t{1});
    }

    // Rows of chords (#25): an empty line becomes a row; chords go in by
    // their order, move along the row and between rows, and onto new rows;
    // bar lines are kept; each chord is a bar.
    void rowsOfChordsTakeChordsByTheirOrder()
    {
        const QString chart = u"{comment: Intro}\n\n{comment: Verse}\n[C]Words"_s;
        // The empty line under the Intro: a row.
        auto row = placeChordInRow(chart, 1, 0, u"Am"_s);
        QVERIFY(row.has_value());
        QCOMPARE(*row, u"{comment: Intro}\n[Am]\n{comment: Verse}\n[C]Words"_s);
        row = placeChordInRow(*row, 1, 9, u"F"_s); // (past the end: last)
        row = placeChordInRow(*row, 1, 1, u"C"_s);
        QCOMPARE(*row, u"{comment: Intro}\n[Am] [C] [F]\n{comment: Verse}\n[C]Words"_s);
        QVERIFY(isChordRow(parseChordPro(*row).lines.at(1)));
        QVERIFY(!isChordRow(parseChordPro(*row).lines.at(3))); // (words)
        QCOMPARE(chartSections(parseChordPro(*row)).front().guessedBars, 3); // a bar each
        // Along the row: F first.
        auto moved = moveChordInRow(*row, 1, 2, 1, 0);
        QCOMPARE(*moved, u"{comment: Intro}\n[F] [Am] [C]\n{comment: Verse}\n[C]Words"_s);
        // Forward: Am after C (counted as the row was).
        moved = moveChordInRow(*moved, 1, 1, 1, 3);
        QCOMPARE(*moved, u"{comment: Intro}\n[F] [C] [Am]\n{comment: Verse}\n[C]Words"_s);
        // From the words into the row: the words keep theirs.
        moved = moveChordInRow(*moved, 3, 0, 1, 1);
        QCOMPARE(*moved, u"{comment: Intro}\n[F] [C] [C] [Am]\n{comment: Verse}\nWords"_s);
        // A line of words takes no chord by order.
        QVERIFY(!placeChordInRow(u"[C]Words"_s, 0, 0, u"G"_s));
        // Bar lines are kept.
        QCOMPARE(*placeChordInRow(u"| [Am] | [C] |"_s, 0, 2, u"G"_s), u"| [Am] | [C] | [G] |"_s);
    }

    // #27: how long a row's chords last, read from its bar lines and set from
    // a chord's menu (half a bar, or whole bars: the empty bars it holds).
    void aRowChordsLengthIsSetInBars()
    {
        QCOMPARE(rowChordBars(parseChordPro(u"| [Am] | [C] [G] |  | [F] |"_s).lines.at(0)), (std::vector<double>{1.0, 0.5, 1.5, 1.0}));
        QVERIFY(rowChordBars(parseChordPro(u"[Am] [C]"_s).lines.at(0)).empty()); // (no bar lines: not written)
        QVERIFY(rowChordBars(parseChordPro(u"[Am]Words"_s).lines.at(0)).empty());
        // C for 2 bars: the row gets its bar lines, a bar each for the others.
        auto set = setRowChordLength(u"{comment: Intro}\n[Am] [C] [G]\n"_s, 1, 1, 4);
        QVERIFY(set.has_value());
        const auto barsOf = [](const QString& chart) { return rowChordBars(parseChordPro(chart).lines.at(1)); };
        QCOMPARE(barsOf(*set), (std::vector<double>{1.0, 2.0, 1.0}));
        QCOMPARE(chartSections(parseChordPro(*set)).front().guessedBars, 4); // (the bars it writes)
        // Am for half a bar: it shares a bar with C.
        set = setRowChordLength(*set, 1, 0, 1);
        QCOMPARE(barsOf(*set), (std::vector<double>{0.5, 1.5, 1.0}));
        // Back to a bar: the bar is its own again.
        set = setRowChordLength(*set, 1, 0, 2);
        QCOMPARE(barsOf(*set), (std::vector<double>{1.0, 2.0, 1.0}));
        // Moved along the row or taken out, the other chords keep their bars.
        auto moved = moveChordInRow(*set, 1, 2, 1, 0); // G first
        QCOMPARE(barsOf(*moved), (std::vector<double>{1.0, 1.0, 2.0}));
        auto taken = changeChord(*moved, 1, 1, QString()); // (a row's chord removed: its bar goes)
        QVERIFY(taken.has_value());
        QCOMPARE(barsOf(*taken), (std::vector<double>{1.0, 2.0}));
        // Not a row, not a length, not a chord: refused.
        QVERIFY(!setRowChordLength(u"[Am]Words"_s, 0, 0, 2));
        QVERIFY(!setRowChordLength(u"[Am] [C]"_s, 0, 0, 3));
        QVERIFY(!setRowChordLength(u"[Am] [C]"_s, 0, 5, 2));
    }

    // A new row before a line (or at the end); a chord moved onto one; the
    // row it left, empty, becomes an empty line.
    void newRowsAreMadeWhereAChordIsDropped()
    {
        const QString chart = u"{comment: Intro}\n{comment: Verse}\n[C]Words"_s;
        QCOMPARE(*insertChordRow(chart, 1, u"Am"_s), u"{comment: Intro}\n[Am]\n{comment: Verse}\n[C]Words"_s);
        QCOMPARE(*insertChordRow(chart, 3, u"G"_s), u"{comment: Intro}\n{comment: Verse}\n[C]Words\n[G]"_s);
        QVERIFY(!insertChordRow(chart, 9, u"G"_s));     // not in the chart
        QVERIFY(!insertChordRow(chart, 1, u"[x]"_s));   // not a chord name
        QCOMPARE(*moveChordToNewRow(chart, 2, 0, 1), u"{comment: Intro}\n[C]\n{comment: Verse}\nWords"_s);
        // The only chord of a row moved away: the row is an empty line.
        QCOMPARE(*moveChordToNewRow(u"[Am]\n[C]Words"_s, 0, 0, 2), u"\n[C]Words\n[Am]"_s);
    }

    // A whole line deleted: words and chords, a row, an empty line, a note;
    // never a section's title (it has Remove section).
    void aWholeLineIsDeleted()
    {
        const QString chart = u"{comment: Verse}\n[C]One [G]two\n[Am] [F]\n\n{c: Capo 2}\nThree"_s;
        QCOMPARE(*deleteLine(chart, 1), u"{comment: Verse}\n[Am] [F]\n\n{c: Capo 2}\nThree"_s);
        QCOMPARE(*deleteLine(chart, 2), u"{comment: Verse}\n[C]One [G]two\n\n{c: Capo 2}\nThree"_s);
        QCOMPARE(*deleteLine(chart, 3), u"{comment: Verse}\n[C]One [G]two\n[Am] [F]\n{c: Capo 2}\nThree"_s);
        QCOMPARE(*deleteLine(chart, 4), u"{comment: Verse}\n[C]One [G]two\n[Am] [F]\n\nThree"_s);
        const auto title = deleteLine(chart, 0);
        QVERIFY(!title);
        QVERIFY2(title.error().message.contains(u"Remove section"_s), qPrintable(title.error().message));
        QVERIFY(!deleteLine(chart, 99));
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
