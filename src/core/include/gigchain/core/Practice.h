#pragma once

#include "gigchain/core/Chords.h"
#include "gigchain/core/SongMap.h"

#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace gigchain::core {

// ---- Ways to play a chord (the chord diagram, the Practice tab)

// A chord's right-hand shapes: root position (0), then each next note of the
// chord at the bottom (1st, 2nd and, for a four-note chord, 3rd inversion).
// At most four notes (as voiceChord); the lowest from C4 (60) to B4 (71).
[[nodiscard]] int inversionCount(const ChordShape& chord);
// `inversion` from 0 to inversionCount() - 1; empty otherwise. Low to high.
[[nodiscard]] std::vector<int> chordInversion(const ChordShape& chord, int inversion);
// "Root position", "1st inversion"...
[[nodiscard]] QString inversionName(int inversion);

// What the left hand plays (from the bass note: the slash note, else the root).
enum class LeftHand : int {
    Bass,      // the bass note alone, C2 to B2
    Octave,    // the bass and the octave above
    RootFifth, // the bass and the fifth above
    Full,      // the whole chord, low: the bass note between G2 and F#3, the chord above it
};
[[nodiscard]] std::vector<int> leftHandNotes(const ChordShape& chord, LeftHand style);

// What the right hand plays.
enum class RightHand : int {
    Smooth, // each chord's inversion nearest the last one (voiceChord)
    Root,   // root position every time
    Chosen, // the inversion chosen for the chord (VoicingStyle::chosen), else Smooth
};

struct VoicingStyle
{
    LeftHand left = LeftHand::Bass;
    RightHand right = RightHand::Smooth;
    std::map<QString, int> chosen; // chord name -> inversion (Song::chordInversions)
};

// A chord as a pianist plays it: the bass in the left hand (C2 to B2), the
// chord in the right hand (up to four notes, between C4 and C6). MIDI notes.
struct Voicing
{
    int bass = 36;
    std::vector<int> right; // low to high
};

// `chord` voiced from the right hand's last chord (`previous`, low to high;
// empty: the first, which sits nearest middle C): the inversion that moves
// the least. With more than four notes the fifth is left out first, then
// the highest extensions.
[[nodiscard]] Voicing voiceChord(const ChordShape& chord, const std::vector<int>& previous);

// One chord of the Practice tab's timeline, in beats from the start.
struct PracticeChord
{
    QString name;
    int section = -1; // in chartSections(); -1 = before the first
    double start = 0.0;
    double length = 0.0;
    int bass = 36;          // the left hand's lowest note
    std::vector<int> left;  // low to high
    std::vector<int> right;
};

struct PracticeSectionMark
{
    double start = 0.0;
    QString name;
    int section = 0;
};

struct PracticeTimeline
{
    std::vector<PracticeChord> chords;
    std::vector<PracticeSectionMark> sections;
    double length = 0.0;   // beats, the count-in included
    int beatsPerBar = 4;
};

// A chart's chords (`song`, in playing order) as a timeline: a count-in bar,
// then each section's bars (`sectionBars`, per chartSections() section)
// shared evenly between its chords; chords before the first section, or of
// a section without bars, a bar each. Voiced in `style` (by default, the
// bass alone and each chord from the one before).
[[nodiscard]] PracticeTimeline practiceTimeline(const SongMap& song, const std::vector<int>& sectionBars,
                                                const QStringList& sectionNames, int beatsPerBar,
                                                const VoicingStyle& style = {});

} // namespace gigchain::core
