#include "SawStackTypes.h"

#include "gigchain/engine/SidechainCurves.h"

#include <string_view>

namespace gigchain::engine::sawstack {

namespace {

constexpr int kSaw = static_cast<int>(Wave::Saw);
constexpr int kSquare = static_cast<int>(Wave::Square);
constexpr int kSine = static_cast<int>(Wave::Sine);
constexpr int kWhite = 0;
constexpr int kPink = 1;
constexpr int kPunchy = 2; // sidechain::kCurves
static_assert(std::string_view(sidechain::kCurves.at(kPunchy).name) == "Punchy");

constexpr LayerDesign kOff{};

// A sine two octaves (or `octave`) under the lowest held note: the weight.
constexpr LayerDesign sub(double db, int octave = -2)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = 1, .wave = kSine, .attack = 0.002, .release = 0.15,
            .lowestOnly = true};
}

// A few saws under the lowest held note, centred and darker: the body under the chords.
constexpr LayerDesign low(double db, double cutoffHz, int octave = -1)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = 3, .detune = 0.15, .mix = 0.3, .width = 0.0, .wave = kSaw,
            .cutoffHz = cutoffHz, .lowCutHz = 60.0, .lowestOnly = true};
}

// A supersaw layer: `voices` saws, spread by `detune` and `width`, kept above `lowCutHz`.
constexpr LayerDesign saw(double db, int octave, int voices, double detune, double width, double cutoffHz, double lowCutHz,
                          int wave = kSaw)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = voices, .detune = detune, .mix = 0.6, .width = width,
            .wave = wave, .cutoffHz = cutoffHz, .lowCutHz = lowCutHz};
}

// The same, closing after the hit: a pluck.
constexpr LayerDesign plucked(LayerDesign layer, double filterEnv, double filterDecay, double decay)
{
    layer.filterEnv = filterEnv;
    layer.filterDecay = filterDecay;
    layer.decay = decay;
    layer.sustain = 0.0;
    return layer;
}

// The hit at the start of each chord, then gone.
constexpr LayerDesign pluck(double db)
{
    // (+4 dB over the level each type gives: lower, the hit was lost under the saws.)
    return plucked(saw(db + 4.0, 1, 5, 0.3, 0.7, 800.0, 250.0), 0.7, 0.18, 0.3);
}

// Filtered noise, one for the whole chord: steady (sustain 1) or a burst.
constexpr LayerDesign air(double db, int colour, double lowCutHz, double cutoffHz, double attack, double decay, double sustain,
                          double release)
{
    // (+18 dB over the level each type gives: lower, it was lost under the saws.)
    return {.on = true, .levelDb = db + 18.0, .width = 1.0, .wave = colour, .cutoffHz = cutoffHz, .attack = attack, .decay = decay,
            .sustain = sustain, .release = release, .lowCutHz = lowCutHz};
}

// Order of layers: sub, low saw, main saw, wide saw, octave up, body, pluck, air.
constexpr std::array<StackType, 10> kTypes{{
    {.key = "supersaw-lead",
     .name = "Supersaw Lead",
     .layers = {kOff, kOff, saw(-4, 0, 9, 0.4, 0.6, 18000, 150), saw(-8, 0, 12, 0.5, 1.0, 20000, 400),
                saw(-12, 1, 7, 0.4, 0.8, 14000, 600), saw(-16, 0, 3, 0.2, 0.3, 4000, 200, kSquare), kOff,
                air(-30, kWhite, 4000, 14000, 0.002, 0.1, 1.0, 0.3)},
     .fx = {.driveDb = 3, .chorus = 0.25, .ott = 0.55, .width = 0.4, .delay = 0.2, .delayFeedback = 0.3, .delayBeats = 0.75,
            .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.6, .sidechain =0.3, .trimDb = 1.4},
     .attack = 0.003,
     .release = 0.3},
    {.key = "supersaw-chords",
     .name = "Supersaw Chords",
     .layers = {sub(-8), low(-12, 1200), saw(-4, 0, 9, 0.35, 0.6, 18000, 150), saw(-7, 0, 12, 0.5, 1.0, 20000, 400),
                saw(-10, 1, 9, 0.45, 1.0, 20000, 600), saw(-14, 0, 5, 0.25, 0.5, 5000, 200, kSquare), pluck(-9),
                air(-26, kWhite, 3000, 12000, 0.001, 0.2, 0.25, 0.3)},
     .fx = {.driveDb = 3, .chorus = 0.3, .ott = 0.6, .width = 0.5, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 0.75,
            .reverb = 0.35, .reverbDecay = 3.0, .reverbSize = 0.7, .sidechain =0.45, .trimDb = -0.7},
     .attack = 0.003,
     .release = 0.35},
    {.key = "future-bass-chords",
     .name = "Future Bass Chords",
     .layers = {sub(-8), low(-12, 1200), saw(-4, 0, 9, 0.35, 0.6, 16000, 150), saw(-7, 0, 12, 0.5, 1.0, 20000, 400),
                saw(-10, 1, 9, 0.45, 1.0, 20000, 600), saw(-12, 0, 5, 0.25, 0.5, 5000, 200, kSquare), pluck(-7),
                air(-24, kWhite, 3000, 12000, 0.001, 0.15, 0.1, 0.2)},
     .fx = {.driveDb = 5, .chorus = 0.2, .ott = 0.8, .width = 0.6, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.5,
            .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.65, .sidechain =0.7, .sidechainCurve = kPunchy, .trimDb = -1.9},
     .attack = 0.001,
     .release = 0.2},
    {.key = "melodic-dubstep-lead",
     .name = "Melodic Dubstep Lead",
     .layers = {kOff, kOff, saw(-4, 0, 9, 0.3, 0.5, 17000, 150), saw(-8, 0, 12, 0.45, 1.0, 20000, 400),
                saw(-9, 1, 9, 0.4, 1.0, 14000, 600), kOff, pluck(-12), air(-30, kWhite, 4000, 12000, 0.002, 0.1, 1.0, 0.4)},
     .fx = {.driveDb = 5, .chorus = 0.3, .ott = 0.7, .width = 0.5, .delay = 0.25, .delayFeedback = 0.35, .delayBeats = 0.75,
            .reverb = 0.45, .reverbDecay = 4.0, .reverbSize = 0.8, .sidechain =0.35, .trimDb = 1.0},
     .attack = 0.004,
     .release = 0.45},
    {.key = "trance-lead",
     .name = "Trance Lead",
     .layers = {kOff, kOff, saw(-4, 0, 16, 0.4, 0.7, 15000, 150), saw(-8, 0, 16, 0.5, 1.0, 16000, 300), kOff, kOff, kOff,
                kOff},
     .fx = {.driveDb = 2, .chorus = 0.35, .ott = 0.35, .width = 0.4, .delay = 0.3, .delayFeedback = 0.4, .delayBeats = 0.75,
            .reverb = 0.4, .reverbDecay = 3.5, .reverbSize = 0.75, .sidechain =0.3, .trimDb = 2.8},
     .attack = 0.002,
     .release = 0.3},
    {.key = "anthem-lead",
     .name = "Anthem Lead",
     .layers = {kOff, kOff, saw(-4, 0, 16, 0.4, 0.7, 15000, 150), saw(-8, 0, 16, 0.5, 1.0, 16000, 300),
                saw(-9, 1, 9, 0.45, 1.0, 16000, 600), kOff, kOff, air(-30, kPink, 3000, 12000, 0.01, 0.1, 1.0, 0.6)},
     .fx = {.driveDb = 3, .chorus = 0.4, .ott = 0.5, .width = 0.6, .delay = 0.3, .delayFeedback = 0.35, .delayBeats = 1.5,
            .reverb = 0.5, .reverbDecay = 5.0, .reverbSize = 0.9, .sidechain =0.35, .trimDb = 2.2},
     .attack = 0.003,
     .release = 0.6},
    {.key = "big-room-lead",
     .name = "Big Room Lead",
     .layers = {kOff, low(-12, 2500), saw(-3, 0, 9, 0.5, 0.4, 18000, 150), kOff, saw(-7, 1, 7, 0.45, 0.5, 18000, 500),
                saw(-10, 0, 5, 0.3, 0.3, 8000, 200, kSquare), kOff, air(-28, kWhite, 4000, 14000, 0.001, 0.12, 0.0, 0.1)},
     .fx = {.driveDb = 9, .chorus = 0.1, .ott = 0.8, .width = 0.3, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.5,
            .reverb = 0.25, .reverbDecay = 2.0, .reverbSize = 0.5, .sidechain =0.6, .trimDb = -0.9},
     .attack = 0.001,
     .release = 0.15},
    {.key = "supersaw-pad",
     .name = "Supersaw Pad",
     .layers = {sub(-14), kOff, saw(-6, 0, 9, 0.4, 0.8, 5000, 150), saw(-8, 0, 12, 0.5, 1.0, 8000, 400),
                saw(-12, 1, 9, 0.45, 1.0, 7000, 600), kOff, kOff, air(-24, kPink, 2000, 9000, 0.8, 0.1, 1.0, 2.5)},
     .fx = {.driveDb = 1, .chorus = 0.45, .ott = 0.4, .width = 0.7, .delay = 0.15, .delayFeedback = 0.4, .delayBeats = 1.5,
            .reverb = 0.6, .reverbDecay = 6.0, .reverbSize = 0.95, .sidechain =0.25, .trimDb = 4.2},
     .attack = 0.6,
     .release = 2.5},
    {.key = "supersaw-pluck",
     .name = "Supersaw Pluck",
     .layers = {kOff, kOff, plucked(saw(-4, 0, 9, 0.35, 0.7, 700, 150), 0.8, 0.25, 0.6),
                plucked(saw(-8, 0, 12, 0.45, 1.0, 1200, 400), 0.7, 0.2, 0.5),
                plucked(saw(-11, 1, 7, 0.4, 1.0, 1500, 600), 0.6, 0.2, 0.4),
                plucked(saw(-14, 0, 5, 0.25, 0.5, 600, 200, kSquare), 0.6, 0.2, 0.4), pluck(-9),
                air(-24, kWhite, 4000, 12000, 0.001, 0.12, 0.0, 0.1)},
     .fx = {.driveDb = 3, .chorus = 0.25, .ott = 0.7, .width = 0.5, .delay = 0.3, .delayFeedback = 0.4, .delayBeats = 0.75,
            .reverb = 0.35, .reverbDecay = 2.5, .reverbSize = 0.65, .sidechain =0.3, .trimDb = 6.0},
     .attack = 0.001,
     .release = 0.3},
    {.key = "reese-bass",
     .name = "Reese Bass",
     .layers = {sub(-6, -1), low(-6, 2000, 0),
                {.on = true, .levelDb = -9, .voices = 3, .detune = 0.35, .mix = 0.6, .width = 0.5, .wave = kSaw,
                 .cutoffHz = 900, .resonance = 0.1, .lowCutHz = 120, .lowestOnly = true},
                kOff, kOff, kOff, kOff, kOff},
     .fx = {.driveDb = 7, .cutMud = false, .ott = 0.6, .sidechain =0.5, .trimDb = 0.4}, // a bass stays mono: no chorus, no width
     .attack = 0.003,
     .release = 0.12},
}};

} // namespace

std::span<const StackType> stackTypes()
{
    return kTypes;
}

const StackType& stackType(int index)
{
    return kTypes.at(static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(kTypes.size()) - 1)));
}

} // namespace gigchain::engine::sawstack
