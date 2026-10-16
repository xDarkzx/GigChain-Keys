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
    double driveDb = 0.0; // saturation: warmth and edge
    double glue = 0.0;    // the 3-band squash that brings the quiet detail up (0-1)
    double chorus = 0.0;  // a wide doubling (0-1)
    double trimDb = 0.0;  // evens the types' loudness out
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
