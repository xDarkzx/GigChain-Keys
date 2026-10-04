#pragma once

#include "gigchain/core/Chords.h"
#include "gigchain/core/SongMap.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace gigchain::core {

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
    int bass = 36;
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
// a section without bars, a bar each. Voiced one from the other.
[[nodiscard]] PracticeTimeline practiceTimeline(const SongMap& song, const std::vector<int>& sectionBars,
                                                const QStringList& sectionNames, int beatsPerBar);

} // namespace gigchain::core
