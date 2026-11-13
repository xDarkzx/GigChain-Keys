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
    // The type's own, not settings (players never see them):
    int semitones = 0;          // above the note (7: a fifth over it, the anthem stack)
    bool ownEnvelope = false;   // keeps its own shape whatever Attack and Release say (a pluck)
    double vibratoCents = 0.0;  // pitch wobble, every note together (future bass: wide and slow)
    double vibratoHz = 5.0;
    double vibratoDelay = 0.0;  // seconds before it comes in (a trance lead's held note)
    double filterLfoOctaves = 0.0; // the filter breathing up and down
    double filterLfoHz = 0.2;
    double pwmDepth = 0.0;      // Wave::Pulse: how far the width sweeps (0-1)
    double pwmHz = 0.6;
    double pitchSemitones = 0.0; // where each note starts, gliding to the note (-12: the hoover's dive up)
    double pitchSeconds = 0.08;
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
    // The EQ when cutMud is on: the mud at 500 Hz, the harshness at 3.5 kHz, the air shelf at 10 kHz.
    double mudDb = -2.5, harshDb = -2.0, airDb = 2.0;
    double topHz = 20000.0;      // a smooth 24 dB/octave top over everything (20 kHz: none)
    double monoBelowHz = 0.0;    // the sides cut below this: the low end one in the middle (0: not)
};

// A kind of saw stack: its eight layers, its effects, and where its knobs start.
struct StackType
{
    const char* key = "";  // in saved settings (never changes once released)
    const char* name = ""; // as players see it
    const char* family = ""; // LEAD, CHORDS, PLUCK, PAD, BASS (the window groups by it)
    std::array<LayerDesign, 8> layers{};
    FxDesign fx{};
    double attack = 0.003;  // where the Attack knob starts (seconds)
    double release = 0.35;  // where the Release knob starts (seconds)
};

[[nodiscard]] std::span<const StackType> stackTypes();
// Type `index` (clamped to the ones there are).
[[nodiscard]] const StackType& stackType(int index);
inline constexpr int kDefaultType = 7; // Supersaw Chords

} // namespace gigchain::engine::sawstack
