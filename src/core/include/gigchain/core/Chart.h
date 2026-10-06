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
    int timeNumerator = 0; // {time: 6/8}; 0 = not given
    int timeDenominator = 0;
    std::vector<ChartLine> lines;
};

// A section of a song (Intro, Verse 1, Chorus...) as its chart shows it.
struct ChartSection
{
    QString name;       // "Verse 1", "Chorus": the label without a note after it
    QString label;      // as written, e.g. "Chorus (x2)"
    int occurrence = 1; // 2 = the second section with this name (the second chorus)
    int guessedBars = 4;
    int line = 0; // its title's index in Chart::lines
};

// Whether a comment's text names a section: "Verse", "Pre-Chorus 2",
// "Chorus (x2)", "Outro"... ("Capo 2" or "play softly" do not).
[[nodiscard]] bool isSectionName(const QString& label);
// The chart's sections in order: ChordPro sections ({start_of_chorus},
// {sov: Verse 2}) and comments that name one (a pasted "[Verse 1]"). Bars
// are guessed as one per chord, a repeat mark ("x2") on a line or title
// multiplying it; a section without chords guesses 4.
[[nodiscard]] std::vector<ChartSection> chartSections(const Chart& chart);
// How many times a line or a section is played: 2 for a line whose words
// are only "x2" or "(x2)" (a line of chords played twice), or a title like
// "Chorus (x2)"; 1 without a repeat mark.
[[nodiscard]] int lineRepeats(const ChartLine& line);
[[nodiscard]] int sectionRepeats(const ChartSection& section);
// A time signature a player could mean: 1-32 beats of a 1, 2, 4, 8, 16 or 32.
[[nodiscard]] bool isTimeSignature(int numerator, int denominator);

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

// Anything pasted or downloaded that has chords and lyrics, to a clean
// ChordPro chart: guitar tab staves ("e|--3--|"), separator rows ("----",
// "===="), site headers ("Tabbed by", "Tuning:") and extra blank lines are
// removed; "Capo" and "Key" lines are kept as a comment and a key; chords
// written above the lyrics are placed over the right words; runs of spaces
// in the lyrics are collapsed. Text that is already ChordPro is only tidied.
[[nodiscard]] QString tidyChordSheet(const QString& text);

// A chord sheet or a whole copied chord-site page, split into the song's
// details and a clean chart: the site's clutter above the song (title bar,
// tuning, difficulty, "chords used", author...) and below it (last update,
// ratings, comments...) is dropped. The chart starts at the first section
// ("[Intro]", "[Verse 1]"...) or, without sections, at the first chords.
struct ImportedSheet
{
    QString chart;  // ChordPro
    QString title;  // e.g. "Morning Light" from "Morning Light Chords by The Example Band"
    QString artist;
    QString key;
    int capo = 0;   // fret; 0 = none (kept as a comment in the chart too)
    double tempo = 0.0;
    int timeNumerator = 0; // 0 = not given
    int timeDenominator = 0;
};
[[nodiscard]] ImportedSheet importChordSheet(const QString& text);

} // namespace gigchain::core
