#pragma once

#include "gigchain/core/Chart.h"
#include "gigchain/core/Chords.h"

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
};

// A chart's chords in playing order: repeat marks played out, the same
// chord twice in a row (within a section) one step, chords it cannot read
// left out (docs/superpowers/specs/2026-09-29-chord-follow-design.md).
struct SongMap
{
    std::vector<SongStep> steps;
    std::vector<int> sectionStarts; // per chartSections() section: its first step; -1 = it has none

    // Enough chords to follow.
    [[nodiscard]] bool followable() const { return steps.size() >= 2; }
};

[[nodiscard]] SongMap buildSongMap(const Chart& chart);

} // namespace gigchain::core
