#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace gigchain::core {

// A song chart in ChordPro, the open format chord-chart apps share
// (OnSong, SongbookPro...): lyrics with chords in brackets placed before the
// syllable they change on, plus {directives} for title, key, sections...
//
//   {title: My Song}
//   {start_of_chorus}
//   [Dm]I love [C#m7]you so much[D/E]
//   {end_of_chorus}

// Text with the chord that starts on its first character (either may be empty).
struct ChartSegment
{
    QString chord;
    QString text;

    friend bool operator==(const ChartSegment&, const ChartSegment&) = default;
};

struct ChartLine
{
    enum class Kind
    {
        Lyrics,     // segments: lyrics and/or chords
        Section,    // a section starts: label = "Chorus", "Verse 2"...
        SectionEnd,
        Comment,    // a note to the player: label
        Meta,       // title, artist, key, tempo and other directives
        Blank,
    };

    Kind kind = Kind::Blank;
    std::vector<ChartSegment> segments; // Lyrics only
    QString label;                      // Section and Comment
    QString source;                     // the line as written (kept for non-lyric lines)

    // The chords on this line, in order.
    [[nodiscard]] QStringList chords() const;
    // The words on this line without chords.
    [[nodiscard]] QString lyrics() const;
};

struct Chart
{
    QString title;
    QString artist;
    QString key;
    double tempo = 0.0; // 0 = not given
    std::vector<ChartLine> lines;
};

// Never fails: anything not understood stays as text on a lyric line.
[[nodiscard]] Chart parseChordPro(const QString& text);
// Lyric lines are rebuilt from their segments; other lines are kept as written.
[[nodiscard]] QString toChordPro(const Chart& chart);

// True when every word on the line is a chord (or a bar/repeat mark), e.g.
// "Am  F  C  G" or "Cmaj7 Dsus4 E7(b9) Bb/D N.C.". A line of words is not.
[[nodiscard]] bool isChordLine(const QString& line);

// A plain chord sheet (chords on their own line above the lyrics, as on
// most chord sites) to ChordPro: each chord goes before the character it sat
// above. Ultimate Guitar's [ch]/[tab] markup is understood and removed, and
// section labels like "[Chorus]" become comments.
[[nodiscard]] QString chordSheetToChordPro(const QString& sheet);

} // namespace gigchain::core
