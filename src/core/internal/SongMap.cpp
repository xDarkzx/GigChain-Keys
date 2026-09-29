#include "gigchain/core/SongMap.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace gigchain::core {

SongMap buildSongMap(const Chart& chart)
{
    const std::vector<ChartSection> sections = chartSections(chart);
    SongMap map;
    map.sectionStarts.assign(sections.size(), -1);

    std::vector<SongStep> played; // every chord as played, before twins are merged
    std::vector<SongStep> part;   // the current section's chords, played once
    int section = -1;
    std::size_t nextSection = 0;
    // Repeat marks multiply (a line and its section up to 16 times each):
    // past the most chords a song can be followed through, stop reading.
    constexpr auto limit = static_cast<std::size_t>(limits::kMaxFollowSteps);
    const auto repeat = [&map](std::vector<SongStep>& to, const std::vector<SongStep>& chords, int times) {
        for (int t = 0; t < times && !map.tooLong; ++t) {
            if (to.size() + chords.size() > limit) {
                map.tooLong = true;
                return;
            }
            to.insert(to.end(), chords.begin(), chords.end());
        }
    };
    const auto finishPart = [&] {
        const int times = section >= 0 ? sectionRepeats(sections.at(static_cast<std::size_t>(section))) : 1;
        if (played.size() + part.size() > limit) map.tooLong = true;
        else repeat(played, part, times);
        part.clear();
    };
    for (std::size_t i = 0; i < chart.lines.size() && !map.tooLong; ++i) {
        if (nextSection < sections.size() && std::cmp_equal(sections.at(nextSection).line, i)) {
            finishPart();
            section = static_cast<int>(nextSection++);
            continue;
        }
        const ChartLine& line = chart.lines.at(i);
        if (line.kind != ChartLine::Kind::Lyrics) continue;
        std::vector<SongStep> once;
        int chordIndex = 0;
        for (const ChartSegment& segment : line.segments) {
            if (segment.chord.isEmpty()) continue;
            const int at = chordIndex++;
            if (const auto shape = parseChordName(segment.chord)) {
                once.push_back(SongStep{.shape = *shape, .name = segment.chord, .section = section,
                                        .places = {{static_cast<int>(i), at}}});
            }
        }
        if (played.size() + part.size() + once.size() > limit) map.tooLong = true;
        else repeat(part, once, lineRepeats(line));
    }
    if (!map.tooLong) finishPart();
    if (map.tooLong) {
        map.steps.clear();
        std::ranges::fill(map.sectionStarts, -1);
        return map;
    }

    // The same chord twice in a row (in one section) is one step, lit in every place.
    for (SongStep& step : played) {
        if (!map.steps.empty() && map.steps.back().section == step.section && map.steps.back().shape == step.shape) {
            auto& places = map.steps.back().places;
            const std::vector<std::pair<int, int>> earlier = places;
            std::ranges::copy_if(step.places, std::back_inserter(places),
                                 [&earlier](const auto& place) { return std::ranges::find(earlier, place) == earlier.end(); });
            continue;
        }
        map.steps.push_back(std::move(step));
    }
    for (std::size_t i = map.steps.size(); i-- > 0;) {
        const int s = map.steps.at(i).section;
        if (s >= 0) map.sectionStarts.at(static_cast<std::size_t>(s)) = static_cast<int>(i);
    }
    return map;
}

} // namespace gigchain::core
