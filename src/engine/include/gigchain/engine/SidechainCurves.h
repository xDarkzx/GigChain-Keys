#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Saw Stack Pro's sidechain shapes, as Nicky Romero's Kickstart offers them:
// a bank of curves, each the sound's level over one cycle (one beat, or
// whatever length is chosen). The synth ducks by them and its window draws
// them, from this one place.
namespace gigchain::engine::sidechain {

enum class Group
{
    Duck,    // down on the beat, back up before the next
    Sub,     // slower back up: smoother under a kick, for bass
    Reverse, // swells up into the next beat ("suction")
    Rhythm,  // more than one dip a cycle: groove on held chords
};

enum class Ease
{
    Linear,
    Smooth,   // S-shaped
    Smoother, // S-shaped, flatter at both ends
    Out,      // fast back up, easing in
    In,       // slow back up, then quickly
};

struct Curve
{
    const char* name;
    Group group;
    Ease ease;
    double hold; // how long it stays down (part of the cycle)
    double rise; // how long it takes to come back (part of the cycle)
};

// Order: the window's bank, and the Sidechain Curve choices (saved by index: append only).
inline constexpr std::array<Curve, 12> kCurves{{
    {"Classic", Group::Duck, Ease::Smooth, 0.02, 0.45},
    {"Smooth", Group::Duck, Ease::Smooth, 0.0, 0.7},
    {"Punchy", Group::Duck, Ease::Out, 0.06, 0.25},
    {"Fast", Group::Duck, Ease::Out, 0.0, 0.18},
    {"Linear", Group::Duck, Ease::Linear, 0.0, 0.6},
    {"Long", Group::Duck, Ease::In, 0.1, 0.85},
    {"Sub Soft", Group::Sub, Ease::Smoother, 0.05, 0.75},
    {"Sub Deep", Group::Sub, Ease::Smooth, 0.3, 0.6},
    {"Suction", Group::Reverse, Ease::In, 0.0, 0.5},
    {"Swell", Group::Reverse, Ease::Linear, 0.0, 0.95},
    {"Gate", Group::Rhythm, Ease::Linear, 0.0, 0.5},
    {"Bounce", Group::Rhythm, Ease::Smooth, 0.02, 0.45},
}};

struct Rate
{
    const char* name;
    double beats; // quarter notes per cycle
};

// Shortest first (the Sidechain Rate choices, saved by index: append only).
// T: triplet, D: dotted.
inline constexpr std::array<Rate, 9> kRates{{
    {"1/16", 0.25},
    {"1/8T", 1.0 / 3.0},
    {"1/8", 0.5},
    {"1/4T", 2.0 / 3.0},
    {"1/8D", 0.75},
    {"1/4", 1.0},
    {"1/4D", 1.5},
    {"1/2", 2.0},
    {"1/1", 4.0},
}};
inline constexpr int kDefaultRate = 5; // 1/4, as Kickstart starts

inline constexpr std::array<const char*, 2> kTriggers{"Sync", "Note"}; // the song's beat, or each note played

// Saw Stack Pro's read-only parameter (IEngine::pluginParameterValue): where
// its sidechain is in its cycle, 0-1, for the window's playhead. Not a setting.
inline constexpr uint32_t kPositionParameter = 0x5C00;

[[nodiscard]] inline double ease(Ease e, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::Smooth: return t * t * (3.0 - (2.0 * t));
    case Ease::Smoother: return t * t * t * ((t * ((t * 6.0) - 15.0)) + 10.0);
    case Ease::Out: return 1.0 - ((1.0 - t) * (1.0 - t) * (1.0 - t));
    case Ease::In: return t * t;
    }
    return t;
}

// The level (0: silent, 1: untouched) of curve `index` at `x` (0-1 through
// the cycle). `slope` 0-1 stretches the dip (0.5: as designed, 0: half as
// long, 1: twice), as Kickstart's Slope.
[[nodiscard]] inline double curveGain(int index, double x, double slope)
{
    const Curve& c = kCurves.at(static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(kCurves.size()) - 1)));
    x = x - std::floor(x);
    const double stretch = std::exp2((std::clamp(slope, 0.0, 1.0) - 0.5) * 2.0);
    const auto duck = [&](double at, double hold, double rise) {
        hold = std::min(hold * stretch, 0.5);
        rise = std::max(std::min(rise * stretch, 0.98 - hold), 0.01);
        return at < hold ? 0.0 : ease(c.ease, (at - hold) / rise);
    };
    switch (c.group) {
    case Group::Duck:
    case Group::Sub: return duck(x, c.hold, c.rise);
    case Group::Reverse: {
        const double rise = std::clamp(c.rise * stretch, 0.05, 0.98);
        return x < 1.0 - rise ? 0.0 : ease(c.ease, (x - (1.0 - rise)) / rise);
    }
    case Group::Rhythm: {
        // Twice a cycle.
        const double half = (x * 2.0) - std::floor(x * 2.0);
        if (c.ease != Ease::Linear) return duck(half, c.hold, c.rise);
        // A gate: open for the first part of each half, with short edges (no clicks).
        constexpr double kEdge = 0.03;
        const double open = std::clamp(c.rise * stretch, 0.1, 0.9);
        return std::clamp(std::min(half / kEdge, (open - half) / kEdge), 0.0, 1.0);
    }
    }
    return 1.0;
}

} // namespace gigchain::engine::sidechain
