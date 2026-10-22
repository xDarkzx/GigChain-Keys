#pragma once

#include "SawStackTypes.h"

#include <array>
#include <cstdint>
#include <span>

// Piano Pro's types: the pianos EDM records use, each a piano (recorded or
// FM), the layers that go with it, and the effects that finish it. Every
// setting comes from the research in the Piano Pro design (house piano =
// Korg M1 style, big house = piano + octave + strings, prog house = bass and
// treble cut then squashed, emotional = long reverb and OTT, felt = soft,
// dark and taped, electric = Rhodes and DX FM pianos, lo-fi = taped Rhodes).
namespace gigchain::engine::piano {

// Where the piano layer's sound comes from.
enum class Source : uint8_t
{
    Grand,    // VCSL's Kawai grand (recorded)
    Upright,  // VCSL's Yamaha upright (recorded)
    Electric, // FM: a Rhodes-style tine piano
    Dx,       // FM: the DX7-style electric piano, brighter and glassier
};
inline constexpr int kSources = 4;
[[nodiscard]] constexpr bool recorded(Source s) { return s == Source::Grand || s == Source::Upright; }

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

struct PianoType
{
    const char* key = "";  // in saved settings (never changes once released)
    const char* name = ""; // as players see it
    Source source = Source::Grand;
    double softness = 0.0;  // 0-1: plays the softer recordings (felt): how hard keys are heard as struck
    std::array<LayerDesign, kLayers> layers{};
    sawstack::FxDesign fx{};
    double toneHz = 20000.0; // where the Tone knob (the main low-pass) starts
    double lowCutHz = 30.0;  // the piano's own low cut (prog house cuts the bass)
    double attack = 0.002;   // where the Attack knob starts (seconds)
    double release = 0.4;    // where the Release knob starts: how long a let-go key takes to stop
    double tape = 0.0;       // where the Tape knob starts
    bool sidechainOn = true;
};

[[nodiscard]] std::span<const PianoType> pianoTypes();
[[nodiscard]] const PianoType& pianoType(int index);
inline constexpr int kDefaultType = 0; // House Piano

} // namespace gigchain::engine::piano
