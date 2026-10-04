#include "gigchain/core/Practice.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace gigchain::core {
namespace {

constexpr int kBassLowest = 36;   // C2
constexpr int kRightLowest = 60;  // C4
constexpr int kRightHighest = 84; // C6
constexpr int kMiddle = 64;       // where a first chord's notes centre

// The chord's notes as pitch classes, at most four: the fifth goes first,
// then the 9th, 11th, 13th (the root, third and seventh stay).
std::vector<int> pitchClasses(const ChordShape& chord)
{
    std::vector<int> intervals;
    for (int i = 0; i < 12; ++i) {
        if ((chord.family & (1U << ((chord.root + i) % 12))) != 0) intervals.push_back(i);
    }
    for (const int drop : {7, 2, 5, 9}) {
        if (intervals.size() <= 4) break;
        std::erase(intervals, drop);
    }
    while (intervals.size() > 4) intervals.pop_back(); // (an odd chord: its highest)
    std::vector<int> classes(intervals.size());
    std::ranges::transform(intervals, classes.begin(), [&chord](int i) { return (chord.root + i) % 12; });
    return classes;
}

double sumOf(const std::vector<int>& notes)
{
    return std::accumulate(notes.begin(), notes.end(), 0.0);
}

// How far the hand moves from `previous` to `next` (both low to high).
double movement(const std::vector<int>& previous, const std::vector<int>& next)
{
    if (previous.empty()) return std::abs(sumOf(next) / static_cast<double>(next.size()) - kMiddle);
    if (previous.size() == next.size()) {
        double moved = 0.0;
        for (std::size_t i = 0; i < next.size(); ++i) moved += std::abs(next.at(i) - previous.at(i));
        return moved;
    }
    // Other sizes: how far the middle of the hand moves, per note.
    return std::abs(sumOf(previous) / static_cast<double>(previous.size()) -
                    sumOf(next) / static_cast<double>(next.size())) *
           static_cast<double>(next.size());
}

} // namespace

Voicing voiceChord(const ChordShape& chord, const std::vector<int>& previous)
{
    Voicing voicing;
    const int bassClass = chord.bass >= 0 ? chord.bass : chord.root;
    voicing.bass = kBassLowest + ((bassClass % 12) + 12) % 12;
    const std::vector<int> classes = pitchClasses(chord);
    if (classes.empty()) return voicing;
    double best = 1e9;
    // Every inversion, its lowest note anywhere from C4 up an octave.
    for (std::size_t first = 0; first < classes.size(); ++first) {
        for (int bottom = kRightLowest; bottom < kRightLowest + 12; ++bottom) {
            if (bottom % 12 != classes.at(first)) continue;
            std::vector<int> notes{bottom};
            for (std::size_t k = 1; k < classes.size(); ++k) {
                int next = notes.back() + 1;
                while (next % 12 != classes.at((first + k) % classes.size())) ++next;
                notes.push_back(next);
            }
            if (notes.back() > kRightHighest) continue;
            const double cost = movement(previous, notes);
            if (cost < best - 1e-9) {
                best = cost;
                voicing.right = notes;
            }
        }
    }
    return voicing;
}

PracticeTimeline practiceTimeline(const SongMap& song, const std::vector<int>& sectionBars, const QStringList& sectionNames,
                                  int beatsPerBar)
{
    PracticeTimeline timeline;
    timeline.beatsPerBar = std::max(1, beatsPerBar);
    const double bar = timeline.beatsPerBar;
    if (song.steps.empty()) return timeline;
    // How many chords each section has.
    std::map<int, int> chordsIn;
    for (const SongStep& step : song.steps) ++chordsIn[step.section];
    double at = bar; // the count-in bar
    int lastSection = -2;
    std::vector<int> hand;
    for (const SongStep& step : song.steps) {
        const int section = step.section;
        double length = bar;
        if (section >= 0 && std::cmp_less(section, sectionBars.size()) && sectionBars.at(static_cast<std::size_t>(section)) > 0) {
            length = sectionBars.at(static_cast<std::size_t>(section)) * bar / chordsIn.at(section);
        }
        if (section != lastSection && section >= 0) {
            const QString name = section < sectionNames.size() ? sectionNames.at(section) : QString();
            timeline.sections.push_back({.start = at, .name = name, .section = section});
        }
        lastSection = section;
        const Voicing voicing = voiceChord(step.shape, hand);
        hand = voicing.right;
        timeline.chords.push_back(
            {.name = step.name, .section = section, .start = at, .length = length, .bass = voicing.bass, .right = voicing.right});
        at += length;
    }
    timeline.length = at;
    return timeline;
}

} // namespace gigchain::core
