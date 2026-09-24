#include "gigchain/core/Chart.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

// Root, quality, extensions/alterations and an optional bass note:
// C, F#m7b5, Bbmaj9, Dsus4, E7(b9), A/C#, Cm(add9)...
const QRegularExpression kChord(
    uR"(^[A-G](?:#|b)?(?:maj|min|dim|aug|sus|add|m|M|°|ø|\+|-)?(?:\d+|maj\d*|sus\d*|add\d+|dim\d*|aug|alt|[b#]\d+|\([^)]*\))*(?:/[A-G](?:#|b)?)?$)"_s);
// Marks that may sit on a chord line: bars, repeats, "no chord".
const QRegularExpression kChordLineMark(uR"(^(?:\||\|\||/|-|%|x\d+|\(x\d+\)|\d+x|N\.?C\.?|\(|\))$)"_s);
const QRegularExpression kDirective(uR"(^\{\s*([A-Za-z_]+)\s*(?::\s*(.*?))?\s*\}$)"_s);
// "[Verse 1]", "[Chorus]" on its own line in chord-site sheets.
const QRegularExpression kSectionLabel(uR"(^\[([A-Za-z][A-Za-z0-9 \-']*)\]$)"_s);

bool isChord(const QString& word)
{
    return kChord.match(word).hasMatch();
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
    if (!name.isEmpty()) name[0] = name[0].toUpper();
    return name;
}

std::vector<ChartSegment> parseSegments(const QString& line)
{
    std::vector<ChartSegment> segments;
    ChartSegment current;
    qsizetype i = 0;
    while (i < line.size()) {
        if (line[i] == u'[') {
            const qsizetype close = line.indexOf(u']', i + 1);
            if (close > i) {
                if (!current.chord.isEmpty() || !current.text.isEmpty()) segments.push_back(current);
                current = ChartSegment{line.mid(i + 1, close - i - 1).trimmed(), {}};
                i = close + 1;
                continue;
            }
        }
        current.text += line[i];
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
    const auto directive = kDirective.match(trimmed);
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
        else if (!kChordLineMark.match(word).hasMatch()) return false;
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
        const QString& line = lines[i];
        const auto label = kSectionLabel.match(line.trimmed());
        if (label.hasMatch() && !isChord(label.captured(1))) {
            out << u"{comment: "_s + label.captured(1).trimmed() + u'}';
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
        const bool lyricsBelow = i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty() &&
                                 !isChordLine(lines[i + 1]) && !kSectionLabel.match(lines[i + 1].trimmed()).hasMatch();
        if (!lyricsBelow) {
            QStringList bracketed;
            for (const auto& chord : chords) bracketed << u'[' + chord.second + u']';
            out << bracketed.join(u' ');
            continue;
        }
        // Put each chord before the character it sat above; chords past the
        // end of the lyrics follow it, a space apart.
        const QString lyric = lines[++i];
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
const QRegularExpression kTabStaff(uR"(^\s*[A-Ga-g]?[#b]?\s*[|:][-0-9hpbrvx/\~|:.()\s]*-[-0-9hpbrvx/\~|:.()\s]*$)"_s);
// A separator row: "-----", "=====", "*****", "_____", "~~~~~".
const QRegularExpression kSeparator(uR"(^\s*([-=*_~#])\1{3,}\s*$)"_s);
// Site header lines that mean nothing to a keys player.
const QRegularExpression kJunk(uR"(^\s*(tuning|tabbed by|transcribed by|chords by|tab by|difficulty|author|standard tuning)\b.*$)"_s,
                               QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kCapo(uR"(^\s*capo\s*:?\s*(.+?)\s*$)"_s, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kKeyLine(uR"(^\s*key\s*:\s*(\S+)\s*$)"_s, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kInlineChord(uR"(\[([^\]]+)\])"_s);

// Tabs to spaces (to the next multiple of 8), non-breaking spaces to spaces.
QString normaliseSpacing(const QString& line)
{
    QString out;
    for (const QChar c : line) {
        if (c == u'\t') {
            do { out += u' '; } while (out.size() % 8 != 0);
        } else {
            out += (c == QChar(0x00A0) ? QChar(u' ') : c);
        }
    }
    return out;
}

// Already ChordPro: {directives} or chords in brackets inside the lines.
bool looksLikeChordPro(const QStringList& lines)
{
    for (const QString& line : lines) {
        if (kDirective.match(line.trimmed()).hasMatch()) return true;
        for (auto it = kInlineChord.globalMatch(line); it.hasNext();) {
            if (isChord(it.next().captured(1).trimmed()) && line.trimmed() != it.peekNext().captured(0)) return true;
        }
    }
    return false;
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
                QString& text = line.segments[i].text;
                qsizetype spaces = 0;
                while (spaces < text.size() && text[spaces] == u' ') ++spaces;
                if (spaces == 0 || spaces == text.size() || line.segments[i].chord.isEmpty()) continue;
                line.segments[i - 1].text += text.left(spaces);
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
        if (kTabStaff.match(line).hasMatch() || kSeparator.match(line).hasMatch() || kJunk.match(line).hasMatch()) continue;
        if (const auto capo = kCapo.match(line); capo.hasMatch() && !isChordLine(line)) {
            lines << u"{comment: Capo "_s + capo.captured(1) + u'}';
            continue;
        }
        if (const auto key = kKeyLine.match(line); key.hasMatch()) {
            lines << u"{key: "_s + key.captured(1) + u'}';
            continue;
        }
        lines << line;
    }
    const QString joined = lines.join(u'\n');
    return tidyChordPro(chordPro ? joined : chordSheetToChordPro(joined));
}

} // namespace gigchain::core
