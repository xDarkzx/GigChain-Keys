#include "gigchain/core/ChartEdit.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

using Kind = ChartLine::Kind;

// The chart, with `line` checked: there, and a line of words (or a blank
// line, when `blankToo`: it becomes one).
Result<Chart> chartWithLine(const QString& chordPro, int line, bool blankToo)
{
    Chart chart = parseChordPro(chordPro);
    if (line < 0 || std::cmp_greater_equal(line, chart.lines.size())) {
        return fail(ErrorCode::OutOfRange,
                    u"Line %1 is not in the chart (it has %2 lines)"_s.arg(line + 1).arg(chart.lines.size()));
    }
    const Kind kind = chart.lines.at(static_cast<std::size_t>(line)).kind;
    if (kind != Kind::Lyrics && !(blankToo && kind == Kind::Blank)) {
        return fail(ErrorCode::InvalidData, u"Line %1 of the chart is not a line of words"_s.arg(line + 1));
    }
    return chart;
}

Result<void> checkChordName(const QString& name)
{
    static const QRegularExpression forbidden(uR"([\[\]{}\r\n])"_s);
    if (name.trimmed().isEmpty()) return fail(ErrorCode::InvalidData, u"A chord needs a name"_s);
    if (forbidden.match(name).hasMatch()) {
        return fail(ErrorCode::InvalidData, u"\"%1\" cannot be a chord name (no brackets or braces)"_s.arg(name));
    }
    return {};
}

// A line of words rebuilt from its lyrics and chords.
ChartLine lineFrom(const LyricLine& lyric)
{
    ChartLine line;
    line.kind = Kind::Lyrics;
    line.segments.push_back({});
    int done = 0;
    for (const PlacedChord& chord : lyric.chords) {
        const int at = std::clamp(chord.at, done, static_cast<int>(lyric.lyrics.size()));
        line.segments.back().text += lyric.lyrics.mid(done, at - done);
        done = at;
        line.segments.push_back(ChartSegment{.chord = chord.name, .text = {}});
    }
    line.segments.back().text += lyric.lyrics.mid(done);
    // No empty piece before the first chord.
    if (line.segments.size() > 1 && line.segments.front().chord.isEmpty() && line.segments.front().text.isEmpty()) {
        line.segments.erase(line.segments.begin());
    }
    return line;
}

LyricLine lyricAt(const Chart& chart, int line)
{
    return lyricLineOf(chart.lines.at(static_cast<std::size_t>(line)));
}

void setLine(Chart& chart, int line, const LyricLine& lyric)
{
    chart.lines.at(static_cast<std::size_t>(line)) = lineFrom(lyric);
}

// A chord into its line's list: before any already at that place.
void insertChord(LyricLine& lyric, int at, const QString& name)
{
    const auto before = std::ranges::find_if(lyric.chords, [at](const PlacedChord& c) { return c.at >= at; });
    lyric.chords.insert(before, PlacedChord{.at = at, .name = name});
}

Result<void> checkChordIndex(const LyricLine& lyric, int line, int chord)
{
    if (chord < 0 || std::cmp_greater_equal(chord, lyric.chords.size())) {
        return fail(ErrorCode::OutOfRange,
                    u"Line %1 has %2 chords: there is no chord %3"_s.arg(line + 1).arg(lyric.chords.size()).arg(chord + 1));
    }
    return {};
}

} // namespace

LyricLine lyricLineOf(const ChartLine& line)
{
    LyricLine lyric;
    for (const ChartSegment& segment : line.segments) {
        if (!segment.chord.isEmpty()) lyric.chords.push_back({.at = static_cast<int>(lyric.lyrics.size()), .name = segment.chord});
        lyric.lyrics += segment.text;
    }
    return lyric;
}

int wordStartAt(const QString& lyrics, int at)
{
    const int length = static_cast<int>(lyrics.size());
    int place = std::clamp(at, 0, length);
    if (place == length) return length;
    if (lyrics.at(place).isSpace()) {
        while (place < length && lyrics.at(place).isSpace()) ++place;
        return place;
    }
    while (place > 0 && !lyrics.at(place - 1).isSpace()) --place;
    return place;
}

Result<QString> placeChord(const QString& chordPro, int line, int at, const QString& chord)
{
    if (auto named = checkChordName(chord); !named) return tl::unexpected(named.error());
    auto chart = chartWithLine(chordPro, line, true);
    if (!chart) return tl::unexpected(chart.error());
    LyricLine lyric = lyricAt(*chart, line);
    insertChord(lyric, wordStartAt(lyric.lyrics, at), chord.trimmed());
    setLine(*chart, line, lyric);
    return toChordPro(*chart);
}

Result<QString> changeChord(const QString& chordPro, int line, int chord, const QString& name)
{
    if (!name.isEmpty()) {
        if (auto named = checkChordName(name); !named) return tl::unexpected(named.error());
    }
    auto chart = chartWithLine(chordPro, line, false);
    if (!chart) return tl::unexpected(chart.error());
    LyricLine lyric = lyricAt(*chart, line);
    if (auto there = checkChordIndex(lyric, line, chord); !there) return tl::unexpected(there.error());
    const auto it = lyric.chords.begin() + chord;
    if (name.isEmpty()) lyric.chords.erase(it);
    else it->name = name.trimmed();
    setLine(*chart, line, lyric);
    return toChordPro(*chart);
}

Result<QString> moveChord(const QString& chordPro, int line, int chord, int toLine, int toAt)
{
    auto chart = chartWithLine(chordPro, line, false);
    if (!chart) return tl::unexpected(chart.error());
    if (auto target = chartWithLine(chordPro, toLine, true); !target) return tl::unexpected(target.error());
    LyricLine from = lyricAt(*chart, line);
    if (auto there = checkChordIndex(from, line, chord); !there) return tl::unexpected(there.error());
    const QString name = from.chords.at(static_cast<std::size_t>(chord)).name;
    from.chords.erase(from.chords.begin() + chord);
    setLine(*chart, line, from);
    LyricLine to = lyricAt(*chart, toLine);
    insertChord(to, wordStartAt(to.lyrics, toAt), name);
    setLine(*chart, toLine, to);
    return toChordPro(*chart);
}

Result<QString> editLyrics(const QString& chordPro, int line, const QString& lyrics)
{
    if (lyrics.contains(u'\n') || lyrics.contains(u'\r')) {
        return fail(ErrorCode::InvalidData, u"A line of words cannot hold a line break (Enter makes a new line)"_s);
    }
    auto chart = chartWithLine(chordPro, line, true);
    if (!chart) return tl::unexpected(chart.error());
    LyricLine lyric = lyricAt(*chart, line);
    const QString& old = lyric.lyrics;
    const int oldLength = static_cast<int>(old.size());
    const int newLength = static_cast<int>(lyrics.size());
    // What stayed the same at the start and at the end.
    int same = 0;
    while (same < oldLength && same < newLength && old.at(same) == lyrics.at(same)) ++same;
    int sameEnd = 0;
    while (sameEnd < std::min(oldLength, newLength) - same &&
           old.at(oldLength - 1 - sameEnd) == lyrics.at(newLength - 1 - sameEnd)) {
        ++sameEnd;
    }
    for (PlacedChord& chord : lyric.chords) {
        // Its word after the change (also one typed right before it): moves with it.
        if (chord.at >= oldLength - sameEnd && chord.at < oldLength) chord.at += newLength - oldLength;
        else if (chord.at <= same) continue; // before the change (or after the last word): stays
        else chord.at = same;                // over words taken away: where they were
    }
    lyric.lyrics = lyrics;
    setLine(*chart, line, lyric);
    return toChordPro(*chart);
}

Result<QString> splitLyricLine(const QString& chordPro, int line, int at)
{
    auto chart = chartWithLine(chordPro, line, true);
    if (!chart) return tl::unexpected(chart.error());
    const LyricLine lyric = lyricAt(*chart, line);
    const int length = static_cast<int>(lyric.lyrics.size());
    const int place = std::clamp(at, 0, length);
    LyricLine first{.lyrics = lyric.lyrics.left(place), .chords = {}};
    LyricLine second{.lyrics = lyric.lyrics.mid(place), .chords = {}};
    for (const PlacedChord& chord : lyric.chords) {
        // (Enter at the very end keeps a chord after the last word where it is.)
        if (chord.at < place || (place == length && chord.at == length)) first.chords.push_back(chord);
        else second.chords.push_back({.at = chord.at - place, .name = chord.name});
    }
    setLine(*chart, line, first);
    chart->lines.insert(chart->lines.begin() + line + 1, lineFrom(second));
    return toChordPro(*chart);
}

Result<QString> joinWithPrevious(const QString& chordPro, int line)
{
    auto chart = chartWithLine(chordPro, line, true);
    if (!chart) return tl::unexpected(chart.error());
    if (line == 0) return fail(ErrorCode::InvalidData, u"The first line has no line above to join"_s);
    const Kind above = chart->lines.at(static_cast<std::size_t>(line - 1)).kind;
    if (above != Kind::Lyrics && above != Kind::Blank) {
        return fail(ErrorCode::InvalidData, u"The line above line %1 is not words: it cannot be joined"_s.arg(line + 1));
    }
    LyricLine joined = lyricAt(*chart, line - 1);
    const LyricLine moving = lyricAt(*chart, line);
    const int shift = static_cast<int>(joined.lyrics.size());
    joined.lyrics += moving.lyrics;
    for (const PlacedChord& chord : moving.chords) joined.chords.push_back({.at = chord.at + shift, .name = chord.name});
    setLine(*chart, line - 1, joined);
    chart->lines.erase(chart->lines.begin() + line);
    return toChordPro(*chart);
}

Result<QString> renameSection(const QString& chordPro, int line, const QString& label)
{
    const QString name = label.trimmed();
    if (name.isEmpty()) return fail(ErrorCode::InvalidData, u"A section needs a name"_s);
    if (name.contains(u'}') || name.contains(u'{') || name.contains(u'\n')) {
        return fail(ErrorCode::InvalidData, u"\"%1\" cannot be a section name (no braces)"_s.arg(name));
    }
    Chart chart = parseChordPro(chordPro);
    if (line < 0 || std::cmp_greater_equal(line, chart.lines.size())) {
        return fail(ErrorCode::OutOfRange,
                    u"Line %1 is not in the chart (it has %2 lines)"_s.arg(line + 1).arg(chart.lines.size()));
    }
    ChartLine& title = chart.lines.at(static_cast<std::size_t>(line));
    if (title.kind != Kind::Section && title.kind != Kind::Comment) {
        return fail(ErrorCode::InvalidData, u"Line %1 of the chart is not a section title"_s.arg(line + 1));
    }
    static const QRegularExpression directive(uR"(^\s*\{\s*([A-Za-z_]+))"_s);
    const QRegularExpressionMatch found = directive.match(title.source);
    const QString kind = found.hasMatch() ? found.captured(1) : u"comment"_s;
    title.source = u"{%1: %2}"_s.arg(kind, name);
    title.label = name;
    return toChordPro(chart);
}

Result<QString> appendSection(const QString& chordPro, const QString& label)
{
    const QString name = label.trimmed();
    if (name.isEmpty()) return fail(ErrorCode::InvalidData, u"A section needs a name"_s);
    if (name.contains(u'}') || name.contains(u'{') || name.contains(u'\n')) {
        return fail(ErrorCode::InvalidData, u"\"%1\" cannot be a section name (no braces)"_s.arg(name));
    }
    QString base = chordPro;
    while (!base.isEmpty() && base.back().isSpace()) base.chop(1);
    return (base.isEmpty() ? QString() : base + u"\n\n"_s) + u"{comment: %1}\n"_s.arg(name);
}

} // namespace gigchain::core
