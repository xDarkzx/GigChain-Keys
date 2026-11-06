#pragma once

#include "gigchain/core/Chart.h"
#include "gigchain/core/Error.h"

#include <QString>

#include <vector>

namespace gigchain::core {

// Editing a ChordPro chart in place, as the player does in the Chart tab:
// each function takes the chart's text and returns it changed (or the reason
// it cannot). `line` counts the parsed chart's lines (Chart::lines, as
// DocumentController::chartLines() reports each one's "line"); `at` is a
// character in that line's words (its lyrics without chords).

// A chord over the words: where it changes (`at`, a character of the
// lyrics; the lyrics' length for one after the last word) and its name.
struct PlacedChord
{
    int at = 0;
    QString name;
    friend bool operator==(const PlacedChord&, const PlacedChord&) = default;
};

// A line of words as the editor sees it: the words, and the chords over them
// in order (two at one place play one after the other).
struct LyricLine
{
    QString lyrics;
    std::vector<PlacedChord> chords;
};
[[nodiscard]] LyricLine lyricLineOf(const ChartLine& line);

// The first letter of the word at `at` (a space: the word after it; past
// the end: the end): where a dropped chord lands.
[[nodiscard]] int wordStartAt(const QString& lyrics, int at);

// A new chord over the word at `at` (snapped to the word's first letter).
[[nodiscard]] Result<QString> placeChord(const QString& chordPro, int line, int at, const QString& chord);

// The line's chord number `chord` (0 = its first) renamed; an empty name
// removes it.
[[nodiscard]] Result<QString> changeChord(const QString& chordPro, int line, int chord, const QString& name);

// The line's chord number `chord` moved over the word at `toAt` of `toLine`
// (the same line or another line of words).
[[nodiscard]] Result<QString> moveChord(const QString& chordPro, int line, int chord, int toLine, int toAt);

// The line's words replaced by `lyrics` (a blank line becomes a line of
// words): chords stay over the words they were over; one over words taken
// away goes where they were.
[[nodiscard]] Result<QString> editLyrics(const QString& chordPro, int line, const QString& lyrics);

// ---- Rows of chords: the instrumental parts (an intro, a solo, a turnaround)
// A line of chords with no words, "[Am] [C] [G]" (or with bar lines,
// "| [Am] | [C] |"): its chords in a row, placed by their order, not by
// letters (it has none). Each chord is a bar, as everywhere in a chart.
[[nodiscard]] bool isChordRow(const ChartLine& line);

// How long each chord of a row with bar lines lasts, in bars, as a chart
// reads them: "| [Am] | [C] [G] |  | [F] |" is Am 1, C 1/2, G 1/2 + a bar it
// holds (an empty bar) = 1.5, F 1. Empty: not a row, or one without bar lines
// (its chords' lengths are not written).
[[nodiscard]] std::vector<double> rowChordBars(const ChartLine& line);

// The longest a chord of a row is set to last.
inline constexpr int kMaxRowChordBars = 8;

// Chord number `chord` of the row `line` set to last `halfBars` half bars:
// 1 (half a bar: it shares a bar with the next chord), or 2, 4... up to
// kMaxRowChordBars bars (whole bars: the empty bars after it it holds). A row
// without bar lines gets them (a bar each for the others).
[[nodiscard]] Result<QString> setRowChordLength(const QString& chordPro, int line, int chord, int halfBars);

// A new chord in a row at `index` (0: first; past the end: last). A blank
// line becomes a row.
[[nodiscard]] Result<QString> placeChordInRow(const QString& chordPro, int line, int index, const QString& chord);

// Chord number `chord` of `line` (words or a row) moved into the row
// `toLine` at `toIndex` (the same row: reordered; a blank line: a row).
[[nodiscard]] Result<QString> moveChordInRow(const QString& chordPro, int line, int chord, int toLine, int toIndex);

// A new row holding `chord`, put in before line `before` (the number of
// lines: at the end).
[[nodiscard]] Result<QString> insertChordRow(const QString& chordPro, int before, const QString& chord);

// Chord number `chord` of `line` moved onto a new row put in before line `before`.
[[nodiscard]] Result<QString> moveChordToNewRow(const QString& chordPro, int line, int chord, int before);

// A whole line taken out: its words and chords, a row, an empty line, a
// note. A section title is removed with removeSectionTitle (its lines stay).
[[nodiscard]] Result<QString> deleteLine(const QString& chordPro, int line);

// Enter at `at`: the words (and chords) from there on go to a new line under it.
[[nodiscard]] Result<QString> splitLyricLine(const QString& chordPro, int line, int at);

// Backspace at the start of `line`: its words and chords join the line above.
[[nodiscard]] Result<QString> joinWithPrevious(const QString& chordPro, int line);

// A section title ({comment: Verse}, {start_of_chorus}...) renamed.
[[nodiscard]] Result<QString> renameSection(const QString& chordPro, int line, const QString& label);

// A section title or a note line taken out (its words join the section
// above; a {start_of_...} section's {end_of_...} goes too). Refused for
// any other line.
[[nodiscard]] Result<QString> removeSectionTitle(const QString& chordPro, int line);

// A new section at the end ({comment: <label>}), with an empty line under it
// to type into.
[[nodiscard]] Result<QString> appendSection(const QString& chordPro, const QString& label);

} // namespace gigchain::core
