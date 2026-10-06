#include "gigchain/core/SongMap.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <utility>

namespace gigchain::core {

int sectionIndexOf(const std::vector<ChartSection>& sections, const SectionRef& ref)
{
    const auto it = std::ranges::find_if(sections, [&ref](const ChartSection& s) {
        return s.occurrence == ref.occurrence && s.name.compare(ref.name.trimmed(), Qt::CaseInsensitive) == 0;
    });
    return it == sections.end() ? -1 : static_cast<int>(it - sections.begin());
}

SongMap buildSongMap(const Chart& chart, const std::vector<SectionRef>& flow, bool mergeTwins)
{
    const std::vector<ChartSection> sections = chartSections(chart);
    SongMap map;
    map.sectionStarts.assign(sections.size(), -1);
    // More sections than the engine follows (one bit each): nothing to follow.
    if (sections.size() > static_cast<std::size_t>(limits::kMaxSectionsPerSong)) {
        map.tooLong = true;
        return map;
    }
    const auto failLong = [&map] {
        map.tooLong = true;
        map.steps.clear();
        map.partStarts.clear();
        map.partFlow.clear();
        std::ranges::fill(map.sectionStarts, -1);
        return map;
    };

    // Each section's chords as played once through it (its repeat marks
    // played out), and the chords before the first section.
    std::vector<std::vector<SongStep>> blocks(sections.size());
    std::vector<SongStep> prelude;
    std::vector<SongStep> part; // the current section's chords, its lines' repeats played out
    std::size_t read = 0;       // every chord read so far, repeats included
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
        if (read + part.size() * static_cast<std::size_t>(times) > limit) map.tooLong = true;
        else repeat(section >= 0 ? blocks.at(static_cast<std::size_t>(section)) : prelude, part, times);
        read += part.size() * static_cast<std::size_t>(times);
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
        if (read + part.size() + once.size() > limit) map.tooLong = true;
        else repeat(part, once, lineRepeats(line));
    }
    if (!map.tooLong) finishPart();
    if (map.tooLong) return failLong();

    // The flow: the sections in the order they are played (by default the
    // chart's); -1: a part naming no section of the chart.
    std::vector<int> order;
    if (flow.empty()) {
        order.resize(sections.size());
        std::iota(order.begin(), order.end(), 0);
    } else {
        std::ranges::transform(flow, std::back_inserter(order), [&sections](const SectionRef& ref) { return sectionIndexOf(sections, ref); });
    }
    std::vector<SongStep> played; // every chord as played, before twins are merged
    int parts = 0;
    const auto add = [&](const std::vector<SongStep>& block, int place) {
        if (block.empty() || map.tooLong) return;
        if (played.size() + block.size() > limit) {
            map.tooLong = true;
            return;
        }
        for (SongStep step : block) {
            step.part = parts;
            played.push_back(std::move(step));
        }
        map.partFlow.push_back(place);
        ++parts;
    };
    add(prelude, -1);
    for (std::size_t place = 0; place < order.size(); ++place) {
        if (const int s = order.at(place); s >= 0) add(blocks.at(static_cast<std::size_t>(s)), static_cast<int>(place));
    }
    if (map.tooLong) return failLong();

    // The same chord twice in a row (in one part) is one step, lit in every place.
    for (SongStep& step : played) {
        if (mergeTwins && !map.steps.empty() && map.steps.back().part == step.part && map.steps.back().shape == step.shape) {
            auto& places = map.steps.back().places;
            const std::vector<std::pair<int, int>> earlier = places;
            std::ranges::copy_if(step.places, std::back_inserter(places),
                                 [&earlier](const auto& place) { return std::ranges::find(earlier, place) == earlier.end(); });
            continue;
        }
        map.steps.push_back(std::move(step));
    }
    for (std::size_t i = 0; i < map.steps.size(); ++i) {
        const SongStep& step = map.steps.at(i);
        if (i == 0 || step.part != map.steps.at(i - 1).part) map.partStarts.push_back(static_cast<int>(i));
        if (step.section >= 0 && map.sectionStarts.at(static_cast<std::size_t>(step.section)) < 0) {
            map.sectionStarts.at(static_cast<std::size_t>(step.section)) = static_cast<int>(i);
        }
    }
    return map;
}

} // namespace gigchain::core
