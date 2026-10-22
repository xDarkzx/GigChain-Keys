#pragma once

#include "SawStackDsp.h"

#include <array>
#include <span>

namespace gigchain::engine::sawstack {

// One layer's design inside a stack type.
struct LayerDesign
{
    bool on = false;
    double levelDb = -12.0;
    int octave = 0;
    int voices = 1;
    double detune = 0.0, mix = 0.5, width = 0.0;
    int wave = 0; // Wave, or NoiseColour for the air layer
    double cutoffHz = 20000.0, resonance = 0.0, filterEnv = 0.0, filterDecay = 0.2;
    double attack = 0.003, decay = 0.1, sustain = 1.0, release = 0.35;
    double lowCutHz = 20.0;
    bool lowestOnly = false;
};

// The effects behind the scenes that make a stack sound finished: players
// never see them, each type sets them.
struct FxDesign
{
    double driveDb = 0.0;       // soft clipping before the rest: warmth, then edge
    bool cutMud = true;         // the producers' EQ: -2.5 dB at 500 Hz, -2 dB at 3.5 kHz, +2 dB air at 10 kHz
    double chorus = 0.0;        // thickness (0-1)
    double ott = 0.0;           // the 3-band "OTT" squash, its depth (0-1)
    double width = 0.0;         // the Dimension-style widening (0-1)
    double delay = 0.0;         // the echo's level (0-1)
    double delayFeedback = 0.3;
    double delayBeats = 0.75;   // the echo's length in quarter notes (0.75: a dotted eighth)
    double reverb = 0.0;        // the reverb's mix (0-1)
    double reverbDecay = 3.0;   // seconds
    double reverbSize = 0.7;    // 0-1
    double sidechain = 0.0;      // how far it ducks on each beat (0-1)
    double sidechainBeats = 1.0; // one duck per this many quarter notes (one of sidechain::kRates)
    double sidechainSlope = 0.5; // how long the dip lasts (0.5: as the curve is)
    int sidechainCurve = 0;      // its shape: sidechain::kCurves (0: Classic)
    double trimDb = 0.0;        // evens the types' loudness out (after the effects)
};

// A kind of saw stack: its eight layers, its effects, and where its knobs start.
struct StackType
{
    const char* key = "";  // in saved settings (never changes once released)
    const char* name = ""; // as players see it
    std::array<LayerDesign, 8> layers{};
    FxDesign fx{};
    double attack = 0.003;  // where the Attack knob starts (seconds)
    double release = 0.35;  // where the Release knob starts (seconds)
};

[[nodiscard]] std::span<const StackType> stackTypes();
// Type `index` (clamped to the ones there are).
[[nodiscard]] const StackType& stackType(int index);
inline constexpr int kDefaultType = 1; // Supersaw Chords

} // namespace gigchain::engine::sawstack
