#pragma once

#include "gigchain/core/Chart.h"
#include "gigchain/core/Chords.h"
#include "gigchain/core/Model.h"

#include <QString>

#include <utility>
#include <vector>

namespace gigchain::core {

// One chord of a song as a player goes through it.
struct SongStep
{
    ChordShape shape;
    QString name;     // as the chart writes it
    int section = -1; // in chartSections(); -1 = before the first
    // Where it is written: (chart line, chord on that line counting from 0).
    // A chord written twice in a row, or a repeated line, has several.
    std::vector<std::pair<int, int>> places;
    int part = -1; // which part of the flow (SongMap::partStarts)
};

// A chart's chords in playing order: repeat marks played out, the same
// chord twice in a row (within a section) one step, chords it cannot read
// left out (docs/superpowers/specs/2026-09-29-chord-follow-design.md).
struct SongMap
{
    std::vector<SongStep> steps;
    std::vector<int> sectionStarts; // per chartSections() section: its first step; -1 = it has none
    // The flow: where each part (a section each time it is played, and the
    // chords before the first section) starts, in playing order.
    std::vector<int> partStarts;
    // Per part: its place in the flow (in the chart's sections when no flow
    // is set); -1: the chords before the first section. Parts without
    // chords are not parts, so this is not simply 0, 1, 2...
    std::vector<int> partFlow;
    // More than limits::kMaxFollowSteps chords as played (repeats played
    // out), or more than limits::kMaxSectionsPerSong sections: not read
    // further, nothing to follow.
    bool tooLong = false;

    // Enough chords to follow.
    [[nodiscard]] bool followable() const { return !tooLong && steps.size() >= 2; }
};

// `flow`: the order the sections are played in (Song::flow); empty: the
// chart's order. A part naming no section of the chart is left out.
// `mergeTwins`: the same chord twice in a row in a part is one step (chord
// follow cannot tell a held chord from the same chord again); false: a step
// each (the song's timeline places each in time).
[[nodiscard]] SongMap buildSongMap(const Chart& chart, const std::vector<SectionRef>& flow = {}, bool mergeTwins = true);
// The section of `sections` that `ref` names; -1: none.
[[nodiscard]] int sectionIndexOf(const std::vector<ChartSection>& sections, const SectionRef& ref);

} // namespace gigchain::core
