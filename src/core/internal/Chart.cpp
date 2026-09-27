#include "gigchain/core/Chart.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

// Root, quality, extensions/alterations and an optional bass note:
// C, F#m7b5, Bbmaj9, Dsus4, E7(b9), A/C#, Cm(add9)...
// (Each pattern is built on first use, not before main: nothing thrown at start-up.)
const QRegularExpression& kChord()
{
    static const QRegularExpression pattern(
        uR"(^[A-G](?:#|b)?(?:maj|min|dim|aug|sus|add|m|M|°|ø|\+|-)?(?:\d+|maj\d*|sus\d*|add\d+|dim\d*|aug|alt|[b#]\d+|\([^)]*\))*(?:/[A-G](?:#|b)?)?$)"_s);
    return pattern;
}
// Marks that may sit on a chord line: bars, repeats, "no chord".
const QRegularExpression& kChordLineMark()
{
    static const QRegularExpression pattern(uR"(^(?:\||\|\||/|-|%|x\d+|\(x\d+\)|\d+x|N\.?C\.?|\(|\))$)"_s);
    return pattern;
}
const QRegularExpression& kDirective()
{
    static const QRegularExpression pattern(uR"(^\{\s*([A-Za-z_]+)\s*(?::\s*(.*?))?\s*\}$)"_s);
    return pattern;
}
// "[Verse 1]", "[Chorus]" on its own line in chord-site sheets.
// "[Verse 1]", "[Chorus] (play loud)": a label, maybe with a note after it.
const QRegularExpression& kSectionLabel()
{
    static const QRegularExpression pattern(uR"(^\[([A-Za-z][A-Za-z0-9 \-']*)\]\s*(.*?)\s*$)"_s);
    return pattern;
}

// "x2", "(x3)", "2x": play this twice (three times...).
const QRegularExpression& kRepeatMark()
{
    static const QRegularExpression pattern(uR"(^\(?(?:x\s*(\d+)|(\d+)\s*x)\)?$)"_s, QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
// A repeat mark somewhere in a section title: "Chorus (x2)", "Outro x4".
const QRegularExpression& kRepeatInLabel()
{
    static const QRegularExpression pattern(uR"((?:^|[\s(])(?:x\s*(\d+)|(\d+)\s*x)(?=[\s)]|$))"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
// A section's name, maybe numbered, maybe with a note after it:
// "Verse 2", "Pre-Chorus", "Chorus (x2)", "Bridge - quiet".
const QRegularExpression& kSectionName()
{
    static const QRegularExpression pattern(
        uR"(^\s*((?:intro|verse|pre[- ]?chorus|post[- ]?chorus|chorus|bridge|solo|instrumental|interlude|breakdown|break|build(?:[- ]?up)?|drop|hook|refrain|tag|outro|coda|ending)\b(?:\s*\d+(?![\dx]))?)(?:[\s:(\-–].*)?$)"_s,
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// How many times a repeat mark says to play something; 1 without one.
int repeatCount(const QRegularExpressionMatch& match)
{
    if (!match.hasMatch()) return 1;
    const int count = (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt();
    return std::clamp(count, 1, 16);
}

bool isChord(const QString& word)
{
    return kChord().match(word).hasMatch();
}

QString sectionName(const QString& type)
{
    if (type == u"chorus"_s || type == u"c"_s) return u"Chorus"_s;
    if (type == u"verse"_s || type == u"v"_s) return u"Verse"_s;
    if (type == u"bridge"_s || type == u"b"_s) return u"Bridge"_s;
    if (type == u"tab"_s || type == u"t"_s) return u"Tab"_s;
    if (type == u"grid"_s || type == u"g"_s) return u"Grid"_s;
    QString name = type;
    name.replace(u'_', u' ');
    if (!name.isEmpty()) name.front() = name.front().toUpper();
    return name;
}

std::vector<ChartSegment> parseSegments(const QString& line)
{
    std::vector<ChartSegment> segments;
    ChartSegment current;
    qsizetype i = 0;
    while (i < line.size()) {
        if (line.at(i) == u'[') {
            const qsizetype close = line.indexOf(u']', i + 1);
            if (close > i) {
                if (!current.chord.isEmpty() || !current.text.isEmpty()) segments.push_back(current);
                current = ChartSegment{.chord = line.mid(i + 1, close - i - 1).trimmed(), .text = {}};
                i = close + 1;
                continue;
            }
        }
        current.text += line.at(i);
        ++i;
    }
    if (!current.chord.isEmpty() || !current.text.isEmpty() || segments.empty()) segments.push_back(current);
    return segments;
}

ChartLine parseLine(const QString& raw, Chart& chart)
{
    ChartLine line;
    line.source = raw;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        line.kind = ChartLine::Kind::Blank;
        return line;
    }
    const auto directive = kDirective().match(trimmed);
    if (!directive.hasMatch()) {
        line.kind = ChartLine::Kind::Lyrics;
        line.segments = parseSegments(raw);
        return line;
    }
    const QString name = directive.captured(1).toLower();
    const QString value = directive.captured(2).trimmed();
    if (name == u"title"_s || name == u"t"_s) chart.title = value;
    else if (name == u"subtitle"_s || name == u"st"_s || name == u"artist"_s) chart.artist = value;
    else if (name == u"key"_s) chart.key = value;
    else if (name == u"tempo"_s) chart.tempo = value.toDouble();
    else if (name == u"time"_s) {
        const QStringList parts = value.split(u'/');
        const int numerator = parts.size() == 2 ? parts.at(0).trimmed().toInt() : 0;
        const int denominator = parts.size() == 2 ? parts.at(1).trimmed().toInt() : 0;
        if (isTimeSignature(numerator, denominator)) {
            chart.timeNumerator = numerator;
            chart.timeDenominator = denominator;
        }
    }

    if (name == u"comment"_s || name == u"c"_s || name == u"ci"_s || name == u"cb"_s ||
        name == u"comment_italic"_s || name == u"comment_box"_s || name == u"highlight"_s) {
        line.kind = ChartLine::Kind::Comment;
        line.label = value;
    } else if (name.startsWith(u"start_of_"_s) || name == u"soc"_s || name == u"sov"_s || name == u"sob"_s ||
               name == u"sot"_s || name == u"sog"_s) {
        const QString type = name.startsWith(u"start_of_"_s) ? name.mid(9) : name.mid(2);
        line.kind = ChartLine::Kind::Section;
        line.label = value.isEmpty() ? sectionName(type) : value;
    } else if (name.startsWith(u"end_of_"_s) || name == u"eoc"_s || name == u"eov"_s || name == u"eob"_s ||
               name == u"eot"_s || name == u"eog"_s) {
        line.kind = ChartLine::Kind::SectionEnd;
    } else {
        line.kind = ChartLine::Kind::Meta;
    }
    return line;
}

} // namespace

QStringList ChartLine::chords() const
{
    QStringList result;
    for (const ChartSegment& segment : segments) {
        if (!segment.chord.isEmpty()) result << segment.chord;
    }
    return result;
}

QString ChartLine::lyrics() const
{
    QString result;
    for (const ChartSegment& segment : segments) result += segment.text;
    return result;
}

bool isSectionName(const QString& label)
{
    return kSectionName().match(label).hasMatch();
}

bool isTimeSignature(int numerator, int denominator)
{
    return numerator >= 1 && numerator <= 32 && denominator >= 1 && denominator <= 32 &&
           (denominator & (denominator - 1)) == 0;
}

std::vector<ChartSection> chartSections(const Chart& chart)
{
    using Kind = ChartLine::Kind;
    std::vector<ChartSection> sections;
    // Where each starts; its bars are counted up to the next one (or the
    // end of a ChordPro section).
    for (std::size_t i = 0; i < chart.lines.size(); ++i) {
        const ChartLine& line = chart.lines.at(i);
        const bool named = line.kind == Kind::Comment && isSectionName(line.label);
        // A tab or chord grid block ({start_of_tab}) is notation, not a part of the song.
        const bool block = line.kind == Kind::Section && (line.label == u"Tab"_s || line.label == u"Grid"_s);
        if ((line.kind != Kind::Section || block) && !named) continue;
        ChartSection section;
        section.label = line.label.trimmed();
        const auto match = kSectionName().match(section.label);
        section.name = match.hasMatch() ? match.captured(1).simplified() : section.label;
        section.occurrence = 1 + static_cast<int>(std::ranges::count_if(sections, [&section](const ChartSection& s) {
            return s.name.compare(section.name, Qt::CaseInsensitive) == 0;
        }));
        section.line = static_cast<int>(i);

        int bars = 0;
        for (std::size_t j = i + 1; j < chart.lines.size(); ++j) {
            const ChartLine& inside = chart.lines.at(j);
            if (inside.kind == Kind::SectionEnd || inside.kind == Kind::Section ||
                (inside.kind == Kind::Comment && isSectionName(inside.label))) {
                break;
            }
            if (inside.kind != Kind::Lyrics) continue;
            const auto chords = static_cast<int>(inside.chords().size());
            bars += chords * repeatCount(kRepeatMark().match(inside.lyrics().trimmed()));
        }
        bars *= repeatCount(kRepeatInLabel().match(section.label));
        section.guessedBars = bars == 0 ? 4 : std::clamp(bars, 1, 999);
        sections.push_back(section);
    }
    return sections;
}

Chart parseChordPro(const QString& text)
{
    Chart chart;
    const QStringList rawLines = QString(text).replace(u"\r\n"_s, u"\n"_s).split(u'\n');
    for (const QString& raw : rawLines) chart.lines.push_back(parseLine(raw, chart));
    return chart;
}

QString toChordPro(const Chart& chart)
{
    QStringList out;
    for (const ChartLine& line : chart.lines) {
        if (line.kind != ChartLine::Kind::Lyrics) {
            out << line.source;
            continue;
        }
        QString text;
        for (const ChartSegment& segment : line.segments) {
            if (!segment.chord.isEmpty()) text += u'[' + segment.chord + u']';
            text += segment.text;
        }
        out << text;
    }
    return out.join(u'\n');
}

bool isChordLine(const QString& line)
{
    const QStringList words = line.simplified().split(u' ', Qt::SkipEmptyParts);
    bool anyChord = false;
    for (const QString& word : words) {
        if (isChord(word)) anyChord = true;
        else if (!kChordLineMark().match(word).hasMatch()) return false;
    }
    return anyChord;
}

QString chordSheetToChordPro(const QString& sheet)
{
    QString text = sheet;
    text.replace(u"\r\n"_s, u"\n"_s);
    // Ultimate Guitar markup: [ch]Am[/ch] marks a chord, [tab]...[/tab] a block.
    for (const QString& tag : {u"[ch]"_s, u"[/ch]"_s, u"[tab]"_s, u"[/tab]"_s}) text.remove(tag);
    QStringList lines = text.split(u'\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast(); // the final newline

    QStringList out;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        const auto label = kSectionLabel().match(line.trimmed());
        if (label.hasMatch() && !isChord(label.captured(1))) {
            const QString note = label.captured(2);
            out << u"{comment: "_s + label.captured(1).trimmed() + (note.isEmpty() ? QString() : u' ' + note) + u'}';
            continue;
        }
        if (!isChordLine(line)) {
            out << line;
            continue;
        }
        // Where each chord starts on the chord line.
        std::vector<std::pair<qsizetype, QString>> chords;
        static const QRegularExpression kWord(uR"(\S+)"_s);
        for (auto it = kWord.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            if (isChord(m.captured())) chords.emplace_back(m.capturedStart(), m.captured());
        }
        const bool lyricsBelow = i + 1 < lines.size() && !lines.at(i + 1).trimmed().isEmpty() &&
                                 !isChordLine(lines.at(i + 1)) && !kSectionLabel().match(lines.at(i + 1).trimmed()).hasMatch();
        if (!lyricsBelow) {
            // Chords in brackets; a repeat mark ("x2") stays, it counts bars.
            QStringList bracketed;
            for (const QString& word : line.simplified().split(u' ', Qt::SkipEmptyParts)) {
                if (isChord(word)) bracketed << u'[' + word + u']';
                else if (kRepeatMark().match(word).hasMatch()) bracketed << word;
            }
            out << bracketed.join(u' ');
            continue;
        }
        // Put each chord before the character it sat above; chords past the
        // end of the lyrics follow it, a space apart.
        const QString& lyric = lines.at(++i);
        QString merged;
        qsizetype taken = 0;
        for (const auto& [column, chord] : chords) {
            if (column <= lyric.size()) {
                merged += lyric.mid(taken, column - taken);
                taken = column;
                merged += u'[' + chord + u']';
            } else {
                merged += lyric.mid(taken);
                taken = lyric.size();
                merged += u" ["_s + chord + u']';
            }
        }
        merged += lyric.mid(taken);
        out << merged;
    }
    return out.join(u'\n') + u'\n';
}

namespace {

// A guitar tab staff line: "e|-----0-----|", "B|--1--1--|", "|-3-5-|".
const QRegularExpression& kTabStaff()
{
    static const QRegularExpression pattern(
        uR"(^\s*[A-Ga-g]?[#b]?\s*[|:][-0-9hpbrvx/\~|:.()\s]*-[-0-9hpbrvx/\~|:.()\s]*$)"_s);
    return pattern;
}
// A separator row: "-----", "=====", "*****", "_____", "~~~~~".
const QRegularExpression& kSeparator()
{
    static const QRegularExpression pattern(uR"(^\s*([-=*_~#])\1{3,}\s*$)"_s);
    return pattern;
}
// Site header lines that mean nothing to a keys player.
const QRegularExpression& kJunk()
{
    static const QRegularExpression pattern(
        uR"(^\s*(tuning|tabbed by|transcribed by|chords by|tab by|difficulty|author|standard tuning)\b.*$)"_s,
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
const QRegularExpression& kCapo()
{
    static const QRegularExpression pattern(uR"(^\s*capo\s*:?\s*(.+?)\s*$)"_s, QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
const QRegularExpression& kKeyLine()
{
    static const QRegularExpression pattern(uR"(^\s*key\s*:\s*(\S+)\s*$)"_s, QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
const QRegularExpression& kInlineChord()
{
    static const QRegularExpression pattern(uR"(\[([^\]]+)\])"_s);
    return pattern;
}

// Tabs to spaces (to the next multiple of 8), non-breaking spaces to spaces.
QString normaliseSpacing(const QString& line)
{
    QString out;
    for (const QChar c : line) {
        if (c == u'\t') {
            out += u' '; // at least one space, then on to the next tab stop (every 8)
            while (out.size() % 8 != 0) out += u' ';
        } else {
            out += (c == QChar(0x00A0) ? QChar(u' ') : c);
        }
    }
    return out;
}

// Already ChordPro: {directives} or chords in brackets inside the lines.
bool looksLikeChordPro(const QStringList& lines)
{
    return std::ranges::any_of(lines, [](const QString& line) {
        if (kDirective().match(line.trimmed()).hasMatch()) return true;
        for (auto it = kInlineChord().globalMatch(line); it.hasNext();) {
            if (isChord(it.next().captured(1).trimmed()) && line.trimmed() != it.peekNext().captured(0)) return true;
        }
        return false;
    });
}

// Lyric spacing collapsed, line ends trimmed, one blank line at most.
QString tidyChordPro(const QString& chordPro)
{
    Chart chart = parseChordPro(chordPro);
    QStringList out;
    bool lastBlank = true; // no blank lines at the start
    for (ChartLine& line : chart.lines) {
        if (line.kind == ChartLine::Kind::Lyrics) {
            // A chord that sat over the space before a word goes onto that
            // word (pasted sheets are often a column or two off).
            for (std::size_t i = 1; i < line.segments.size(); ++i) {
                QString& text = line.segments.at(i).text;
                qsizetype spaces = 0;
                while (spaces < text.size() && text.at(spaces) == u' ') ++spaces;
                if (spaces == 0 || spaces == text.size() || line.segments.at(i).chord.isEmpty()) continue;
                line.segments.at(i - 1).text += text.left(spaces);
                text.remove(0, spaces);
            }
            for (ChartSegment& segment : line.segments) {
                static const QRegularExpression kSpaces(uR"( {2,})"_s);
                segment.text.replace(kSpaces, u" "_s);
            }
            if (!line.segments.empty()) {
                // No spaces before the first word or after the last one.
                line.segments.front().text = line.segments.front().text.trimmed().isEmpty() && !line.segments.front().chord.isEmpty()
                    ? line.segments.front().text : QString(line.segments.front().text).remove(QRegularExpression(uR"(^\s+)"_s));
                QString& last = line.segments.back().text;
                while (last.endsWith(u' ')) last.chop(1);
            }
        } else {
            line.source = line.source.trimmed();
        }
        const bool blank = line.kind == ChartLine::Kind::Blank ||
                           (line.kind == ChartLine::Kind::Lyrics && line.chords().isEmpty() && line.lyrics().trimmed().isEmpty());
        if (blank) {
            if (lastBlank) continue;
            line = ChartLine{};
        }
        lastBlank = blank;
        Chart single;
        single.lines.push_back(line);
        out << toChordPro(single);
    }
    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out.join(u'\n') + u'\n';
}

} // namespace

QString tidyChordSheet(const QString& text)
{
    QString cleaned = text;
    cleaned.replace(u"\r\n"_s, u"\n"_s);
    for (const QString& tag : {u"[ch]"_s, u"[/ch]"_s, u"[tab]"_s, u"[/tab]"_s}) cleaned.remove(tag);

    // Decided on what was pasted, before Capo/Key lines become {directives}.
    const bool chordPro = looksLikeChordPro(cleaned.split(u'\n'));

    QStringList lines;
    for (const QString& raw : cleaned.split(u'\n')) {
        const QString line = normaliseSpacing(raw);
        if (kTabStaff().match(line).hasMatch() || kSeparator().match(line).hasMatch() || kJunk().match(line).hasMatch()) continue;
        if (const auto capo = kCapo().match(line); capo.hasMatch() && !isChordLine(line)) {
            lines << u"{comment: Capo "_s + capo.captured(1) + u'}';
            continue;
        }
        if (const auto key = kKeyLine().match(line); key.hasMatch()) {
            lines << u"{key: "_s + key.captured(1) + u'}';
            continue;
        }
        lines << line;
    }
    const QString joined = lines.join(u'\n');
    return tidyChordPro(chordPro ? joined : chordSheetToChordPro(joined));
}

namespace {

// "Hallelujah Chords by Leonard Cohen", "Wonderwall Tab", "Let It Be Lyrics".
const QRegularExpression& kSiteTitle()
{
    static const QRegularExpression pattern(
        uR"(^(.+?)\s+(?:guitar\s+|ukulele\s+|piano\s+)?(?:chords|tabs?|lyrics)(?:\s+by\s+(.+?))?\s*$)"_s,
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
// "Wonderwall - Oasis"
const QRegularExpression& kTitleDashArtist()
{
    static const QRegularExpression pattern(uR"(^(.+?)\s+[-–]\s+(.+?)\s*$)"_s);
    return pattern;
}
const QRegularExpression& kCapoFret()
{
    static const QRegularExpression pattern(uR"(^\s*capo\s*:?\s*(\d+))"_s, QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
const QRegularExpression& kTempoLine()
{
    static const QRegularExpression pattern(uR"(^\s*(?:(?:bpm|tempo)\s*:?\s*(\d+(?:\.\d+)?)|(\d+(?:\.\d+)?)\s*bpm\b))"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
// "Time: 6/8", "Time signature: 3/4".
const QRegularExpression& kTimeLine()
{
    static const QRegularExpression pattern(uR"(^\s*time(?:\s+signature)?\s*:?\s*(\d+)\s*/\s*(\d+)\s*$)"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
// Below the song on chord sites.
const QRegularExpression& kFooter()
{
    static const QRegularExpression pattern(
        uR"(^\s*(?:last update\b|rating\s*$|please,?\s+rate\b|\d+\s+comments?\s*$|report bad tab\b|add to playlist\b|download pdf\b))"_s,
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

bool isSectionLabel(const QString& line)
{
    const auto label = kSectionLabel().match(line.trimmed());
    return label.hasMatch() && !isChord(label.captured(1));
}

} // namespace

ImportedSheet importChordSheet(const QString& text)
{
    ImportedSheet sheet;
    QString cleaned = text;
    cleaned.replace(u"\r\n"_s, u"\n"_s);
    for (const QString& tag : {u"[ch]"_s, u"[/ch]"_s, u"[tab]"_s, u"[/tab]"_s}) cleaned.remove(tag);
    const QStringList lines = cleaned.split(u'\n');

    if (looksLikeChordPro(lines)) {
        const Chart chart = parseChordPro(cleaned);
        sheet.title = chart.title;
        sheet.artist = chart.artist;
        sheet.key = chart.key;
        sheet.tempo = chart.tempo;
        sheet.timeNumerator = chart.timeNumerator;
        sheet.timeDenominator = chart.timeDenominator;
        sheet.chart = tidyChordSheet(cleaned);
        return sheet;
    }

    // Where the song starts: the first section label, else the first chords.
    qsizetype start = -1;
    for (qsizetype i = 0; i < lines.size() && start < 0; ++i) {
        if (isSectionLabel(lines.at(i))) start = i;
    }
    for (qsizetype i = 0; i < lines.size() && start < 0; ++i) {
        if (isChordLine(normaliseSpacing(lines.at(i)))) start = i;
    }
    if (start < 0) start = 0;
    // Where it ends: the first line of site clutter after it.
    qsizetype end = lines.size();
    for (qsizetype i = start; i < lines.size(); ++i) {
        if (kFooter().match(lines.at(i)).hasMatch()) {
            end = i;
            break;
        }
    }

    // The song's details from the header.
    bool titleSeen = false;
    for (qsizetype i = 0; i < start; ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || kSeparator().match(line).hasMatch() || kTabStaff().match(line).hasMatch() ||
            kJunk().match(line).hasMatch()) {
            continue; // never a title
        }
        if (const auto key = kKeyLine().match(line); key.hasMatch()) sheet.key = key.captured(1);
        else if (const auto capo = kCapoFret().match(line); capo.hasMatch()) sheet.capo = capo.captured(1).toInt();
        else if (const auto tempo = kTempoLine().match(line); tempo.hasMatch()) {
            sheet.tempo = (tempo.captured(1).isEmpty() ? tempo.captured(2) : tempo.captured(1)).toDouble();
        } else if (const auto time = kTimeLine().match(line); time.hasMatch()) {
            const int numerator = time.captured(1).toInt();
            const int denominator = time.captured(2).toInt();
            if (isTimeSignature(numerator, denominator)) {
                sheet.timeNumerator = numerator;
                sheet.timeDenominator = denominator;
            }
        } else if (!titleSeen) {
            titleSeen = true;
            if (const auto site = kSiteTitle().match(line); site.hasMatch()) {
                sheet.title = site.captured(1).trimmed();
                sheet.artist = site.captured(2).trimmed();
            } else if (const auto dash = kTitleDashArtist().match(line); dash.hasMatch()) {
                sheet.title = dash.captured(1).trimmed();
                sheet.artist = dash.captured(2).trimmed();
            } else {
                sheet.title = line;
            }
        }
    }
    sheet.title = sheet.title.left(120);
    sheet.artist = sheet.artist.left(120);

    QString chart = tidyChordSheet(lines.mid(start, end - start).join(u'\n'));
    if (sheet.capo > 0) chart.prepend(u"{comment: Capo %1}\n"_s.arg(sheet.capo));
    sheet.chart = chart;
    return sheet;
}

} // namespace gigchain::core
