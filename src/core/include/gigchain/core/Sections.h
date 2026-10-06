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

// What `patch` plays where nothing says otherwise (its play mode): every
// instrument (PlayMode::All), or only `selected` (PlayMode::Selected; none,
// or not one of the patch's instruments: its first instrument).
[[nodiscard]] std::vector<ChannelId> defaultLive(const Patch& patch, const std::optional<ChannelId>& selected);

// What plays outside any section (a song without sections): nullopt =
// every channel (PlayMode::All); else defaultLive().
[[nodiscard]] std::optional<std::vector<ChannelId>> unsectionedLive(const Patch& patch,
                                                                    const std::optional<ChannelId>& selected);

// The sections of the song's chart, in order, resolved against `patch`:
// - not assigned: the default (defaultLive: every channel, or the selected one);
// - assigned: those of its channels in the patch, in the patch's order; when
//   none of them is in this patch (they belong to another), the default;
// - assigned none at all: silent (a break).
// A chart without sections gives an empty list (unsectionedLive() plays).
[[nodiscard]] std::vector<ResolvedSection> resolveSections(const Song& song, const Patch& patch,
                                                           const std::optional<ChannelId>& selected = std::nullopt);

// The stored setup of a chart's section, if there is one.
[[nodiscard]] const SectionSetup* findSectionSetup(const Song& song, const ChartSection& section);

} // namespace gigchain::core
