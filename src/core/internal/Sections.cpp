#include "gigchain/core/Sections.h"

#include <algorithm>

namespace gigchain::core {

std::optional<ChannelId> firstInstrument(const Patch& patch)
{
    const auto it = std::ranges::find_if(patch.channels, [](const Channel& c) { return c.instrument.has_value(); });
    if (it == patch.channels.end()) return std::nullopt;
    return it->id;
}

const SectionSetup* findSectionSetup(const Song& song, const ChartSection& section)
{
    const auto it = std::ranges::find_if(song.sections, [&section](const SectionSetup& s) {
        return s.occurrence == section.occurrence && s.name.compare(section.name, Qt::CaseInsensitive) == 0;
    });
    return it == song.sections.end() ? nullptr : &*it;
}

std::vector<ResolvedSection> resolveSections(const Song& song, const Patch& patch)
{
    const std::optional<ChannelId> fallback = firstInstrument(patch);
    std::vector<ResolvedSection> resolved;
    for (const ChartSection& section : chartSections(parseChordPro(song.chart))) {
        ResolvedSection r;
        r.chart = section;
        r.bars = section.guessedBars;
        const SectionSetup* setup = findSectionSetup(song, section);
        if (setup != nullptr) {
            if (setup->bars > 0) {
                r.bars = setup->bars;
                r.guessed = false;
            }
            r.assigned = setup->assigned;
            r.channels = setup->channels;
        }
        if (r.assigned) {
            for (const Channel& channel : patch.channels) {
                if (std::ranges::find(r.channels, channel.id) != r.channels.end()) r.live.push_back(channel.id);
            }
        }
        // Not assigned, or assigned only channels of another patch: the default.
        if ((!r.assigned || (r.live.empty() && !r.channels.empty())) && fallback) r.live = {*fallback};
        resolved.push_back(std::move(r));
    }
    return resolved;
}

} // namespace gigchain::core
