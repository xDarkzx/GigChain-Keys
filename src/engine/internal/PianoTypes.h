#pragma once

#include "SawStackTypes.h"

#include <array>
#include <cstdint>
#include <span>

// Piano Pro's types: a stage keyboard's sounds. Real recorded pianos (a
// concert grand, an upright), real recorded electric pianos (Rhodes,
// Wurlitzer, a CP80 electric grand, a Pianet), the DX's FM piano, an 80s
// polysynth's keys, strings and brass, and the house pianos dance records
// use (a grand with an octave, strings and the effects that finish it).
// Each is a piano (its source), the layers that go with it, and its effects.
namespace gigchain::engine::piano {

// Where the piano layer's sound comes from.
enum class Source : uint8_t
{
    Grand,         // the concert grand (recorded: Salamander, Yamaha C5)
    Upright,       // the upright (recorded: VCSL's Knight)
    Rhodes,        // a Rhodes Mark I (recorded: jRhodes3d)
    Wurlitzer,     // a Wurlitzer EP200 (recorded)
    ElectricGrand, // a Yamaha CP80 (recorded)
    Pianet,        // a Hohner Pianet T (recorded)
    Dx,            // FM: the DX7-style electric piano
    Poly,          // an 80s polysynth (saw, pulse, sub, a low-pass and its envelope)
};
inline constexpr int kSources = 8;
[[nodiscard]] constexpr bool recorded(Source s)
{
    return s == Source::Grand || s == Source::Upright || s == Source::Rhodes || s == Source::Wurlitzer
        || s == Source::ElectricGrand || s == Source::Pianet;
}

// The layers, in order: the piano itself, the same piano an octave up (the
// big house trick), soft strings two octaves up, and a sine sub under the
// lowest note.
inline constexpr int kLayers = 4;
enum class Layer : uint8_t { Piano, Octave, Strings, Sub };

struct LayerDesign
{
    bool on = false;
    double levelDb = -12.0;
};

// The polysynth's voice (Source::Poly): two oscillators and a sub into a
// 24 dB low-pass that its own envelope opens, as the 80s Rolands are built.
struct PolyDesign
{
    double saw = 1.0;        // the saw's level (0-1)
    double pulse = 0.0;      // the pulse's level (0-1)
    double pulseWidth = 0.5; // 0.5: a square
    double pwm = 0.0;        // how far the width moves (0-0.45), at pwmHz
    double pwmHz = 0.6;
    double sub = 0.0;        // a square an octave down (0-1)
    double cutoffHz = 2000;  // where the low-pass sits (at middle C)
    double resonance = 0.0;  // 0-1
    double envOctaves = 2.0; // how far its envelope opens it
    double keyTrack = 0.5;   // 0-1: higher notes open it more
    double fAttack = 0.005, fDecay = 0.6, fSustain = 0.2, fRelease = 0.4;
    double decay = 1.0, sustain = 1.0; // the level's (attack and release: the knobs)
};

struct PianoType
{
    const char* key = "";    // in saved settings (never changes once released)
    const char* name = "";   // as players see it
    const char* family = ""; // ACOUSTIC, ELECTRIC, SYNTH, HOUSE (the window groups by it)
    Source source = Source::Grand;
    double softness = 0.0;   // -1 to 1: above 0 plays softer recordings (felt), below 0 harder (bright)
    std::array<LayerDesign, kLayers> layers{};
    sawstack::FxDesign fx{};
    double toneHz = 20000.0; // where the Tone knob (the main low-pass) starts
    double lowCutHz = 30.0;  // the piano's own low cut (prog house cuts the bass)
    double attack = 0.002;   // where the Attack knob starts (seconds)
    double release = 0.4;    // where the Release knob starts: how long a let-go key takes to stop
    double tape = 0.0;       // where the Tape knob starts
    double tremolo = 0.0;    // the suitcase's stereo tremolo, its depth (0-1)
    double tremoloHz = 4.5;
    PolyDesign poly{};
    bool sidechainOn = true;
};

[[nodiscard]] std::span<const PianoType> pianoTypes();
[[nodiscard]] const PianoType& pianoType(int index);
inline constexpr int kDefaultType = 0; // Concert Grand

} // namespace gigchain::engine::piano
