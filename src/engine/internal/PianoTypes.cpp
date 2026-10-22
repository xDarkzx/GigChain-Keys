#include "PianoTypes.h"

#include "gigchain/engine/SidechainCurves.h"

#include <algorithm>
#include <string_view>

namespace gigchain::engine::piano {

namespace {

constexpr int kSmooth = 1; // sidechain::kCurves
constexpr int kPunchy = 2;
static_assert(std::string_view(sidechain::kCurves.at(kSmooth).name) == "Smooth");
static_assert(std::string_view(sidechain::kCurves.at(kPunchy).name) == "Punchy");

constexpr LayerDesign on(double db)
{
    return {.on = true, .levelDb = db};
}
constexpr LayerDesign off(double db)
{
    return {.on = false, .levelDb = db};
}

// Layers: piano, octave up, strings, sub.
constexpr std::array<PianoType, 10> kTypes{{
    // Korg M1 "Piano 16'": bright and hard, a quiet octave up in it, short room.
    {.key = "house-piano",
     .name = "House Piano",
     .source = Source::Grand,
     .layers = {on(-6), on(-16), off(-18), off(-12)},
     .fx = {.driveDb = 3, .chorus = 0.1, .ott = 0.45, .width = 0.3, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.75,
            .reverb = 0.22, .reverbDecay = 1.8, .reverbSize = 0.5, .sidechain = 0.35, .trimDb = 4.5},
     .lowCutHz = 120,
     .release = 0.3},
    // Piano for the body, an octave up for the top, strings two octaves up.
    {.key = "big-house-piano",
     .name = "Big House Piano",
     .source = Source::Grand,
     .layers = {on(-6), on(-9), on(-15), off(-12)},
     .fx = {.driveDb = 5, .chorus = 0.15, .ott = 0.6, .width = 0.6, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 0.75,
            .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.7, .sidechain = 0.6, .sidechainCurve = kPunchy, .trimDb = 2.7},
     .lowCutHz = 150,
     .release = 0.4},
    // Avicii's: bass and treble cut hard, then squashed; a darker room.
    {.key = "prog-house-piano",
     .name = "Prog House Piano",
     .source = Source::Grand,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.driveDb = 2, .ott = 0.55, .width = 0.35, .delay = 0.2, .delayFeedback = 0.35, .delayBeats = 0.75, .reverb = 0.35,
            .reverbDecay = 3.0, .reverbSize = 0.75, .sidechain = 0.45, .trimDb = 5.0},
     .toneHz = 6500,
     .lowCutHz = 300,
     .release = 0.5},
    // Future bass breakdowns: long tails, OTT about 60%, a gentle pump.
    {.key = "emotional-piano",
     .name = "Emotional Piano",
     .source = Source::Grand,
     .layers = {on(-6), off(-14), on(-20), off(-12)},
     .fx = {.chorus = 0.1, .ott = 0.6, .width = 0.5, .delay = 0.22, .delayFeedback = 0.4, .delayBeats = 1.0, .reverb = 0.55,
            .reverbDecay = 6.0, .reverbSize = 0.95, .sidechain = 0.3, .sidechainCurve = kSmooth, .trimDb = 4.3},
     .lowCutHz = 60,
     .release = 1.2},
    // Soft hammers, dark, on tape, a long room and a half-note echo.
    {.key = "felt-piano",
     .name = "Felt Piano",
     .source = Source::Upright,
     .softness = 0.6,
     .layers = {on(-4), off(-14), off(-18), off(-12)},
     .fx = {.ott = 0.2, .width = 0.3, .delay = 0.2, .delayFeedback = 0.35, .delayBeats = 2.0, .reverb = 0.5, .reverbDecay = 5.0,
            .reverbSize = 0.9, .sidechain = 0.15, .sidechainCurve = kSmooth, .trimDb = 10.7},
     .toneHz = 3200,
     .lowCutHz = 60,
     .attack = 0.004,
     .release = 0.8,
     .tape = 0.45,
     .sidechainOn = false},
    {.key = "grand-piano",
     .name = "Grand Piano",
     .source = Source::Grand,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.cutMud = false, .reverb = 0.2, .reverbDecay = 2.2, .reverbSize = 0.6, .sidechain = 0.3, .trimDb = 4.5},
     .release = 0.5,
     .sidechainOn = false},
    {.key = "upright-piano",
     .name = "Upright Piano",
     .source = Source::Upright,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.cutMud = false, .reverb = 0.18, .reverbDecay = 1.8, .reverbSize = 0.5, .sidechain = 0.3, .trimDb = 2.9},
     .release = 0.4,
     .sidechainOn = false},
    // Rhodes-style tines, chorused, a little tape.
    {.key = "electric-piano",
     .name = "Electric Piano",
     .source = Source::Electric,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.chorus = 0.45, .ott = 0.3, .width = 0.4, .delay = 0.15, .delayFeedback = 0.3, .delayBeats = 0.75, .reverb = 0.25,
            .reverbDecay = 2.5, .reverbSize = 0.6, .sidechain = 0.25, .trimDb = 3.9},
     .release = 0.4,
     .tape = 0.12},
    // The DX7 electric piano: brighter, glassier.
    {.key = "dx-piano",
     .name = "DX Piano",
     .source = Source::Dx,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.chorus = 0.35, .ott = 0.35, .width = 0.45, .delay = 0.2, .delayFeedback = 0.3, .delayBeats = 0.75, .reverb = 0.3,
            .reverbDecay = 3.0, .reverbSize = 0.7, .sidechain = 0.3, .trimDb = 3.8},
     .release = 0.5},
    // Lo-fi house: a soft, dark, wobbly Rhodes.
    {.key = "lofi-keys",
     .name = "Lo-fi Keys",
     .source = Source::Electric,
     .softness = 0.3,
     .layers = {on(-6), off(-14), off(-18), off(-12)},
     .fx = {.chorus = 0.3, .ott = 0.25, .width = 0.3, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 1.5, .reverb = 0.3,
            .reverbDecay = 2.5, .reverbSize = 0.6, .sidechain = 0.3, .sidechainCurve = kSmooth, .trimDb = 9.3},
     .toneHz = 4200,
     .release = 0.4,
     .tape = 0.7},
}};

} // namespace

std::span<const PianoType> pianoTypes()
{
    return kTypes;
}

const PianoType& pianoType(int index)
{
    return kTypes.at(static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(kTypes.size()) - 1)));
}

} // namespace gigchain::engine::piano
