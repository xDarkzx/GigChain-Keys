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

// Enter at `at`: the words (and chords) from there on go to a new line under it.
[[nodiscard]] Result<QString> splitLyricLine(const QString& chordPro, int line, int at);

// Backspace at the start of `line`: its words and chords join the line above.
[[nodiscard]] Result<QString> joinWithPrevious(const QString& chordPro, int line);

// A section title ({comment: Verse}, {start_of_chorus}...) renamed.
[[nodiscard]] Result<QString> renameSection(const QString& chordPro, int line, const QString& label);

// A new section at the end ({comment: <label>}), with an empty line under it
// to type into.
[[nodiscard]] Result<QString> appendSection(const QString& chordPro, const QString& label);

} // namespace gigchain::core
