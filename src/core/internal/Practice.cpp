#include "gigchain/core/Practice.h"

#include <algorithm>
#include <cmath>
#include <iterator>
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

int bassClassOf(const ChordShape& chord)
{
    return (((chord.bass >= 0 ? chord.bass : chord.root) % 12) + 12) % 12;
}

// The notes of `classes` from `bottom` up, each the next one above the last.
std::vector<int> stacked(int bottom, const std::vector<int>& classes, std::size_t first)
{
    std::vector<int> notes{bottom};
    for (std::size_t k = 1; k < classes.size(); ++k) {
        int next = notes.back() + 1;
        while (next % 12 != classes.at((first + k) % classes.size())) ++next;
        notes.push_back(next);
    }
    return notes;
}

// `count` chords sharing `beats`, each change on the beat grid: on bar lines
// when there are bars enough for them, else on beats (else evenly).
std::vector<double> shared(double beats, int count, int beatsPerBar)
{
    std::vector<double> out;
    if (count <= 0) return out;
    const double unit = beats / beatsPerBar >= count - 1e-9 ? beatsPerBar : (beats >= count - 1e-9 ? 1.0 : beats / count);
    const double units = std::floor(beats / unit + 1e-9);
    double last = 0.0;
    for (int j = 1; j <= count; ++j) {
        const double edge = j == count ? beats : std::round(j * units / count) * unit;
        out.push_back(edge - last);
        last = edge;
    }
    return out;
}

} // namespace

std::vector<double> chordLengths(const SongMap& song, const std::vector<int>& sectionBars, int beatsPerBar)
{
    const int perBar = std::max(1, beatsPerBar);
    std::vector<double> lengths(song.steps.size(), static_cast<double>(perBar));
    // Part by part (a section each time it is played).
    for (std::size_t first = 0; first < song.steps.size();) {
        std::size_t end = first + 1;
        while (end < song.steps.size() && song.steps.at(end).part == song.steps.at(first).part) ++end;
        const int section = song.steps.at(first).section;
        const int bars = section >= 0 && std::cmp_less(section, sectionBars.size()) ? sectionBars.at(static_cast<std::size_t>(section)) : 0;
        double written = 0.0; // beats the chart gives its chords
        int open = 0;         // chords it does not
        for (std::size_t i = first; i < end; ++i) {
            const double b = song.steps.at(i).bars;
            if (b > 0.0) {
                lengths.at(i) = b * perBar;
                written += lengths.at(i);
            } else {
                ++open;
            }
        }
        if (open > 0) {
            // The section's bars left over, shared on the grid (no bars set,
            // or too few left: a bar each).
            double left = bars > 0 ? bars * static_cast<double>(perBar) - written : 0.0;
            if (left < open - 1e-9) left = static_cast<double>(open) * perBar;
            const std::vector<double> share = shared(left, open, perBar);
            std::size_t next = 0;
            for (std::size_t i = first; i < end; ++i) {
                if (song.steps.at(i).bars <= 0.0) lengths.at(i) = share.at(next++);
            }
        }
        first = end;
    }
    return lengths;
}

int inversionCount(const ChordShape& chord)
{
    return static_cast<int>(pitchClasses(chord).size());
}

std::vector<int> chordInversion(const ChordShape& chord, int inversion)
{
    const std::vector<int> classes = pitchClasses(chord);
    if (inversion < 0 || std::cmp_greater_equal(inversion, classes.size())) return {};
    const int lowest = classes.at(static_cast<std::size_t>(inversion));
    const int bottom = kRightLowest + ((lowest - kRightLowest) % 12 + 12) % 12;
    return stacked(bottom, classes, static_cast<std::size_t>(inversion));
}

QString inversionName(int inversion)
{
    switch (inversion) {
    case 0: return QStringLiteral("Root position");
    case 1: return QStringLiteral("1st inversion");
    case 2: return QStringLiteral("2nd inversion");
    case 3: return QStringLiteral("3rd inversion");
    default: return QStringLiteral("%1th inversion").arg(inversion);
    }
}

std::vector<int> leftHandNotes(const ChordShape& chord, LeftHand style)
{
    const int bass = kBassLowest + bassClassOf(chord);
    switch (style) {
    case LeftHand::Bass: return {bass};
    case LeftHand::Octave: return {bass, bass + 12};
    case LeftHand::RootFifth: return {bass, bass + 7};
    case LeftHand::Full: {
        // The bass note from G2 (43) up, then the chord's own notes above it.
        constexpr int kFullLowest = 43;
        const int bottom = kFullLowest + ((bassClassOf(chord) - kFullLowest) % 12 + 12) % 12;
        std::vector<int> notes{bottom};
        std::vector<int> classes = pitchClasses(chord);
        std::erase(classes, bassClassOf(chord));
        std::ranges::transform(classes, std::back_inserter(notes), [bottom](int pc) { return bottom + ((pc - bottom) % 12 + 12) % 12; });
        std::ranges::sort(notes);
        return notes;
    }
    }
    return {bass};
}

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
                                  int beatsPerBar, const VoicingStyle& style)
{
    PracticeTimeline timeline;
    timeline.beatsPerBar = std::max(1, beatsPerBar);
    const double bar = timeline.beatsPerBar;
    if (song.steps.empty()) return timeline;
    const std::vector<double> lengths = chordLengths(song, sectionBars, timeline.beatsPerBar);
    double at = bar; // the count-in bar
    int lastPart = -2;
    std::vector<int> hand;
    for (std::size_t i = 0; i < song.steps.size(); ++i) {
        const SongStep& step = song.steps.at(i);
        const int section = step.section;
        const double length = lengths.at(i);
        if (step.part != lastPart && section >= 0) { // (each time a section is played: a mark, Chorus x2 two)
            const QString name = section < sectionNames.size() ? sectionNames.at(section) : QString();
            timeline.sections.push_back({.start = at, .name = name, .section = section, .part = step.part});
        }
        lastPart = step.part;
        std::vector<int> right;
        const auto chosen = style.chosen.find(step.name);
        if (style.right == RightHand::Root) right = chordInversion(step.shape, 0);
        else if (style.right == RightHand::Chosen && chosen != style.chosen.end()) right = chordInversion(step.shape, chosen->second);
        if (right.empty()) right = voiceChord(step.shape, hand).right; // smooth (also a choice the chord does not have)
        hand = right;
        const std::vector<int> left = leftHandNotes(step.shape, style.left);
        timeline.chords.push_back({.name = step.name,
                                   .section = section,
                                   .start = at,
                                   .length = length,
                                   .bass = left.empty() ? kBassLowest : left.front(),
                                   .left = left,
                                   .right = right});
        at += length;
    }
    timeline.length = at;
    return timeline;
}

} // namespace gigchain::core
