#pragma once

#include "gigchain/core/Chart.h"
#include "gigchain/core/Model.h"

#include <optional>
#include <vector>

// A song's sections (from its chart) with what each plays in a patch.
namespace gigchain::core {

struct ResolvedSection
{
    ChartSection chart;
    int bars = 4;         // the set length, or the chart's guess
    bool guessed = true;  // no length set: `bars` is the guess
    bool assigned = false;
    std::vector<ChannelId> channels; // as set (may name channels of other patches)
    std::vector<ChannelId> live;     // what plays in the patch
};

// The patch's first channel with an instrument (audio inputs are not
// instruments): what a section plays until told otherwise.
[[nodiscard]] std::optional<ChannelId> firstInstrument(const Patch& patch);

// The sections of the song's chart, in order, resolved against `patch`:
// - not assigned: the patch's first instrument;
// - assigned: those of its channels in the patch, in the patch's order; when
//   none of them is in this patch (they belong to another), the default;
// - assigned none at all: silent (a break).
// A chart without sections gives an empty list: everything plays.
[[nodiscard]] std::vector<ResolvedSection> resolveSections(const Song& song, const Patch& patch);

// The stored setup of a chart's section, if there is one.
[[nodiscard]] const SectionSetup* findSectionSetup(const Song& song, const ChartSection& section);

} // namespace gigchain::core
