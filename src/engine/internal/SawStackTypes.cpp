#include "SawStackTypes.h"

#include "gigchain/engine/SidechainCurves.h"

#include <algorithm>
#include <string_view>

namespace gigchain::engine::sawstack {

namespace {

constexpr int kSaw = static_cast<int>(Wave::Saw);
constexpr int kSquare = static_cast<int>(Wave::Square);
constexpr int kSine = static_cast<int>(Wave::Sine);
constexpr int kTriangle = static_cast<int>(Wave::Triangle);
constexpr int kPulse = static_cast<int>(Wave::Pulse);
constexpr int kWhite = 0;
constexpr int kPink = 1;
constexpr int kPunchy = 2; // sidechain::kCurves
constexpr int kSwell = 9;
constexpr int kGate = 10;
static_assert(std::string_view(sidechain::kCurves.at(kPunchy).name) == "Punchy");
static_assert(std::string_view(sidechain::kCurves.at(kSwell).name) == "Swell");
static_assert(std::string_view(sidechain::kCurves.at(kGate).name) == "Gate");

constexpr LayerDesign kOff{};

// How producers build a stack: every layer in its own band, so none of
// them fights another. The sub and the bass under the lowest note only, the
// mids for the chord, the highs above them, and the air on top; each cut
// above and below so the stack is big and smooth, never muddy or sharp.

// A sine two octaves (or `octave`) under the lowest held note: the weight.
constexpr LayerDesign sub(double db, int octave = -2)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = 1, .wave = kSine, .attack = 0.002, .release = 0.15,
            .lowestOnly = true};
}

// A few saws under the lowest held note, centred and dark: the bass under the chords.
constexpr LayerDesign low(double db, double cutoffHz, int octave = -1)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = 3, .detune = 0.15, .mix = 0.3, .width = 0.0, .wave = kSaw,
            .cutoffHz = cutoffHz, .lowCutHz = 50.0, .lowestOnly = true};
}

// A unison layer: `voices` of `wave`, spread by `detune` and `width`, kept between `lowCutHz` and `cutoffHz`.
constexpr LayerDesign saw(double db, int octave, int voices, double detune, double width, double cutoffHz, double lowCutHz,
                          int wave = kSaw)
{
    return {.on = true, .levelDb = db, .octave = octave, .voices = voices, .detune = detune, .mix = 0.6, .width = width,
            .wave = wave, .cutoffHz = cutoffHz, .lowCutHz = lowCutHz};
}

// The same layer `semitones` above the note (7: a fifth).
constexpr LayerDesign above(LayerDesign layer, int semitones)
{
    layer.semitones = semitones;
    return layer;
}

// Its pitch wobbling, every note together; `delay` seconds before it comes in.
constexpr LayerDesign vibrato(LayerDesign layer, double cents, double hz, double delay = 0.0)
{
    layer.vibratoCents = cents;
    layer.vibratoHz = hz;
    layer.vibratoDelay = delay;
    return layer;
}

// Its filter breathing up and down by `octaves`.
constexpr LayerDesign breathe(LayerDesign layer, double octaves, double hz)
{
    layer.filterLfoOctaves = octaves;
    layer.filterLfoHz = hz;
    return layer;
}

// A pulse whose width sweeps (the Juno's and the hoover's).
constexpr LayerDesign pwm(LayerDesign layer, double depth, double hz)
{
    layer.wave = kPulse;
    layer.pwmDepth = depth;
    layer.pwmHz = hz;
    return layer;
}

// Each note starting `semitones` off and gliding to the note over about `seconds`.
constexpr LayerDesign glide(LayerDesign layer, double semitones, double seconds)
{
    layer.pitchSemitones = semitones;
    layer.pitchSeconds = seconds;
    return layer;
}

// Under the lowest held note only (a bass's layers).
constexpr LayerDesign mono(LayerDesign layer)
{
    layer.lowestOnly = true;
    layer.width = 0.0;
    return layer;
}

constexpr LayerDesign resonant(LayerDesign layer, double resonance)
{
    layer.resonance = resonance;
    return layer;
}

// The same, closing after the hit and falling to `sustain`: a pluck or a stab.
constexpr LayerDesign plucked(LayerDesign layer, double filterEnv, double filterDecay, double decay, double sustain = 0.0)
{
    layer.filterEnv = filterEnv;
    layer.filterDecay = filterDecay;
    layer.decay = decay;
    layer.sustain = sustain;
    return layer;
}

// Its own short shape, whatever Attack and Release say.
constexpr LayerDesign own(LayerDesign layer, double attack, double decay, double sustain, double release)
{
    layer.ownEnvelope = true;
    layer.attack = attack;
    layer.decay = decay;
    layer.sustain = sustain;
    layer.release = release;
    return layer;
}

// The hit at the start of each chord, then gone.
constexpr LayerDesign pluck(double db)
{
    // (+4 dB over the level each type gives: lower, the hit was lost under the saws.)
    return own(plucked(saw(db + 4.0, 1, 5, 0.3, 0.7, 800.0, 400.0), 0.7, 0.18, 0.3), 0.001, 0.3, 0.0, 0.35);
}

// Filtered noise, one for the whole chord: steady (sustain 1) or a burst.
constexpr LayerDesign air(double db, int colour, double lowCutHz, double cutoffHz, double attack, double decay, double sustain,
                          double release)
{
    // (+18 dB over the level each type gives: lower, it was lost under the saws.)
    return {.on = true, .levelDb = db + 18.0, .width = 1.0, .wave = colour, .cutoffHz = cutoffHz, .attack = attack, .decay = decay,
            .sustain = sustain, .release = release, .lowCutHz = lowCutHz};
}

// The finish every type has: the mud and the harshness taken down, no added
// fizz, a smooth top at `topHz`, and the low end kept in the middle.
constexpr FxDesign smooth(FxDesign fx, double topHz)
{
    fx.mudDb = -3.0;
    fx.harshDb = -4.5;
    fx.airDb = -1.5;
    fx.topHz = topHz;
    fx.monoBelowHz = 160.0;
    return fx;
}

// Layer slots: 0 goes round the effects (a sub or a bass's weight), 7 is the
// noise; 1-6 are each type's own. In the window's order: LEAD, CHORDS,
// PLUCK, PAD, BASS (its pictures: tools/make-saw-art.py).
constexpr std::array<StackType, 28> kTypes{{
    // ================= LEAD
    {.key = "supersaw-lead",
     .name = "Supersaw Lead",
     .family = "LEAD",
     .layers = {kOff, kOff, saw(-4, 0, 9, 0.5, 0.7, 6500, 200), saw(-8, 0, 9, 0.55, 1.0, 10000, 900),
                saw(-12, 1, 7, 0.45, 0.9, 10500, 1500), saw(-15, 0, 3, 0.2, 0.3, 2500, 150, kSquare), kOff,
                air(-34, kPink, 5000, 13000, 0.002, 0.1, 1.0, 0.3)},
     .fx = smooth({.driveDb = 0.8, .chorus = 0.35, .ott = 0.3, .width = 0.45, .delay = 0.2, .delayFeedback = 0.3, .delayBeats = 0.75,
                   .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.6, .sidechain = 0.25, .trimDb = 0.1},
                  12000),
     .attack = 0.004,
     .release = 0.3},
    {.key = "trance-lead",
     .name = "Trance Lead",
     .family = "LEAD",
     .layers = {kOff, kOff, vibrato(saw(-4, 0, 9, 0.5, 0.6, 7000, 180), 14, 5.5, 0.35),
                vibrato(saw(-8, 0, 7, 0.6, 1.0, 9000, 600), 14, 5.5, 0.35),
                vibrato(saw(-9, 1, 9, 0.5, 1.0, 11000, 1000), 14, 5.5, 0.35), kOff, kOff,
                air(-36, kPink, 5000, 13000, 0.01, 0.1, 1.0, 0.5)},
     .fx = smooth({.driveDb = 0.5, .chorus = 0.3, .ott = 0.25, .width = 0.5, .delay = 0.32, .delayFeedback = 0.4, .delayBeats = 0.75,
                   .reverb = 0.45, .reverbDecay = 3.5, .reverbSize = 0.8, .sidechain = 0.2, .trimDb = 1.2},
                  12500),
     .attack = 0.004,
     .release = 0.4},
    {.key = "anthem-lead",
     .name = "Anthem Lead",
     .family = "LEAD",
     .layers = {kOff, saw(-13, -1, 1, 0.0, 0.0, 1500, 90, kTriangle), saw(-5, 0, 9, 0.5, 0.7, 7000, 200),
                above(saw(-9, 0, 7, 0.5, 1.0, 8000, 400), 7), saw(-10, 1, 7, 0.5, 1.0, 11000, 1000), kOff, kOff,
                air(-34, kPink, 4000, 12000, 0.01, 0.1, 1.0, 0.6)},
     .fx = smooth({.driveDb = 0.8, .chorus = 0.4, .ott = 0.3, .width = 0.55, .delay = 0.3, .delayFeedback = 0.35, .delayBeats = 1.5,
                   .reverb = 0.5, .reverbDecay = 5.0, .reverbSize = 0.9, .sidechain = 0.3, .trimDb = 1.2},
                  12000),
     .attack = 0.004,
     .release = 0.6},
    {.key = "big-room-lead",
     .name = "Big Room Lead",
     .family = "LEAD",
     .layers = {kOff, low(-12, 1500, 0), glide(saw(-4, 0, 7, 0.55, 0.5, 8000, 150), 12, 0.03),
                glide(saw(-7, 0, 3, 0.3, 0.3, 5000, 150, kSquare), 12, 0.03), glide(saw(-9, 1, 7, 0.5, 0.5, 10000, 800), 12, 0.03),
                kOff, kOff, air(-30, kWhite, 5000, 12000, 0.001, 0.12, 0.0, 0.1)},
     .fx = smooth({.driveDb = 2.5, .chorus = 0.1, .ott = 0.45, .width = 0.3, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.5,
                   .reverb = 0.25, .reverbDecay = 2.0, .reverbSize = 0.5, .sidechain = 0.55, .sidechainCurve = kPunchy, .trimDb = -3.4},
                  11000),
     .attack = 0.001,
     .release = 0.15},
    {.key = "melodic-dubstep-lead",
     .name = "Melodic Dubstep Lead",
     .family = "LEAD",
     .layers = {kOff, kOff, vibrato(glide(saw(-4, 0, 9, 0.5, 0.6, 6500, 200), -3, 0.06), 25, 4.0, 0.25),
                vibrato(glide(pwm(saw(-9, 0, 5, 0.4, 0.8, 4000, 300), 0.6, 0.4), -3, 0.06), 25, 4.0, 0.25),
                vibrato(glide(saw(-10, 1, 7, 0.5, 1.0, 10500, 1000), -3, 0.06), 25, 4.0, 0.25), kOff, kOff,
                air(-34, kPink, 5000, 12000, 0.002, 0.1, 1.0, 0.4)},
     .fx = smooth({.driveDb = 1.5, .chorus = 0.3, .ott = 0.35, .width = 0.5, .delay = 0.25, .delayFeedback = 0.35, .delayBeats = 0.75,
                   .reverb = 0.5, .reverbDecay = 4.0, .reverbSize = 0.85, .sidechain = 0.35, .trimDb = 0.7},
                  11500),
     .attack = 0.004,
     .release = 0.45},
    {.key = "hoover-lead",
     .name = "Hoover Lead",
     .family = "LEAD",
     .layers = {kOff, glide(pwm(saw(-8, -1, 5, 0.6, 0.6, 2500, 80), 0.7, 0.5), -12, 0.12),
                glide(pwm(saw(-5, 0, 5, 0.6, 0.8, 5000, 150), 0.8, 0.7), -12, 0.12),
                glide(saw(-11, 1, 5, 0.5, 0.8, 8000, 900), -12, 0.12), kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 2.0, .chorus = 0.6, .ott = 0.3, .width = 0.4, .delay = 0.15, .delayFeedback = 0.3, .delayBeats = 0.75,
                   .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.6, .sidechain = 0.3, .trimDb = -1.5},
                  10000),
     .attack = 0.004,
     .release = 0.3},
    {.key = "silk-lead",
     .name = "Silk Lead",
     .family = "LEAD",
     .layers = {kOff, kOff, vibrato(saw(-4, 0, 7, 0.45, 0.6, 6000, 150, kTriangle), 10, 5.0, 0.3),
                saw(-12, 0, 7, 0.5, 1.0, 4000, 300), vibrato(saw(-10, 1, 5, 0.4, 0.8, 9000, 800, kTriangle), 10, 5.0, 0.3), kOff,
                kOff, air(-36, kPink, 4000, 10000, 0.01, 0.1, 1.0, 0.6)},
     .fx = smooth({.chorus = 0.45, .ott = 0.15, .width = 0.5, .delay = 0.3, .delayFeedback = 0.4, .delayBeats = 0.75, .reverb = 0.45,
                   .reverbDecay = 3.0, .reverbSize = 0.75, .sidechain = 0.15, .trimDb = -3.0},
                  9000),
     .attack = 0.01,
     .release = 0.5},

    // ================= CHORDS
    {.key = "supersaw-chords",
     .name = "Supersaw Chords",
     .family = "CHORDS",
     .layers = {sub(-9), low(-12, 700), saw(-5, 0, 9, 0.5, 0.6, 5500, 200), saw(-8, 0, 9, 0.55, 1.0, 6500, 350),
                saw(-11, 1, 7, 0.5, 1.0, 11000, 1200), saw(-16, 0, 3, 0.25, 0.4, 2500, 200, kSquare), pluck(-10),
                air(-30, kPink, 5000, 13000, 0.001, 0.25, 0.3, 0.3)},
     .fx = smooth({.driveDb = 0.8, .chorus = 0.35, .ott = 0.3, .width = 0.5, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 0.75,
                   .reverb = 0.35, .reverbDecay = 3.0, .reverbSize = 0.7, .sidechain = 0.45, .trimDb = -0.9},
                  12000),
     .attack = 0.004,
     .release = 0.35},
    {.key = "future-bass-chords",
     .name = "Future Bass Chords",
     .family = "CHORDS",
     .layers = {sub(-9), low(-13, 600), vibrato(glide(saw(-5, 0, 9, 0.5, 0.6, 6000, 200), -2, 0.05), 30, 3.2),
                vibrato(glide(saw(-9, 0, 5, 0.4, 0.8, 5000, 250, kSquare), -2, 0.05), 30, 3.2),
                vibrato(glide(saw(-11, 1, 7, 0.5, 1.0, 10500, 1100), -2, 0.05), 30, 3.2), kOff, pluck(-8),
                air(-28, kPink, 4000, 12000, 0.001, 0.15, 0.1, 0.2)},
     .fx = smooth({.driveDb = 1.5, .chorus = 0.25, .ott = 0.45, .width = 0.6, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.5,
                   .reverb = 0.3, .reverbDecay = 2.5, .reverbSize = 0.65, .sidechain = 0.75, .sidechainCurve = kPunchy, .trimDb = -1.6},
                  11500),
     .attack = 0.002,
     .release = 0.2},
    {.key = "deep-house-chords",
     .name = "Deep House Chords",
     .family = "CHORDS",
     .layers = {kOff, low(-14, 450), plucked(pwm(saw(-5, 0, 3, 0.3, 0.5, 700, 150), 0.4, 0.3), 0.55, 0.35, 0.6, 0.35),
                plucked(saw(-9, 0, 5, 0.35, 0.8, 1000, 250), 0.5, 0.3, 0.6, 0.35), kOff, kOff, kOff, kOff},
     .fx = smooth({.chorus = 0.5, .ott = 0.25, .width = 0.4, .delay = 0.2, .delayFeedback = 0.35, .delayBeats = 0.75, .reverb = 0.4,
                   .reverbDecay = 2.5, .reverbSize = 0.7, .sidechain = 0.3, .trimDb = 5.9},
                  9000),
     .attack = 0.002,
     .release = 0.25},
    {.key = "gated-trance-chords",
     .name = "Gated Trance Chords",
     .family = "CHORDS",
     .layers = {kOff, low(-14, 600), saw(-5, 0, 9, 0.55, 0.6, 7000, 200), saw(-8, 0, 9, 0.6, 1.0, 9000, 400),
                saw(-11, 1, 7, 0.5, 1.0, 11500, 1200), kOff, kOff, air(-34, kPink, 5000, 13000, 0.01, 0.1, 1.0, 0.3)},
     .fx = smooth({.driveDb = 0.5, .chorus = 0.3, .ott = 0.3, .width = 0.5, .delay = 0.25, .delayFeedback = 0.4, .delayBeats = 0.75,
                   .reverb = 0.45, .reverbDecay = 3.0, .reverbSize = 0.8, .sidechain = 0.85, .sidechainBeats = 0.25,
                   .sidechainCurve = kGate, .trimDb = 0.9},
                  12000),
     .attack = 0.003,
     .release = 0.3},
    {.key = "synthwave-chords",
     .name = "Synthwave Chords",
     .family = "CHORDS",
     .layers = {kOff, low(-14, 500), breathe(resonant(pwm(saw(-5, 0, 3, 0.25, 0.6, 3000, 150), 0.7, 0.45), 0.2), 0.4, 0.15),
                breathe(saw(-9, 0, 3, 0.3, 0.8, 3500, 250), 0.4, 0.15), saw(-15, 1, 3, 0.25, 0.8, 7000, 1000, kTriangle), kOff,
                kOff, kOff},
     .fx = smooth({.chorus = 0.7, .ott = 0.15, .width = 0.35, .delay = 0.25, .delayFeedback = 0.35, .delayBeats = 0.75, .reverb = 0.45,
                   .reverbDecay = 3.5, .reverbSize = 0.8, .sidechain = 0.15, .trimDb = -1.9},
                  9000),
     .attack = 0.01,
     .release = 0.5},
    {.key = "uplifting-chords",
     .name = "Uplifting Chords",
     .family = "CHORDS",
     .layers = {sub(-11), low(-13, 700), breathe(saw(-5, 0, 9, 0.5, 0.6, 5500, 200), 0.3, 0.1), saw(-8, 1, 9, 0.55, 1.0, 10000, 900),
                saw(-17, 2, 5, 0.45, 1.0, 12000, 2500), kOff, kOff, air(-32, kPink, 5000, 13000, 0.3, 0.1, 1.0, 1.0)},
     .fx = smooth({.driveDb = 0.5, .chorus = 0.4, .ott = 0.3, .width = 0.6, .delay = 0.15, .delayFeedback = 0.35, .delayBeats = 0.75,
                   .reverb = 0.5, .reverbDecay = 4.5, .reverbSize = 0.9, .sidechain = 0.5, .trimDb = 2.0},
                  12500),
     .attack = 0.05,
     .release = 0.6},

    // ================= PLUCK
    {.key = "supersaw-pluck",
     .name = "Supersaw Pluck",
     .family = "PLUCK",
     .layers = {kOff, kOff, plucked(saw(-4, 0, 9, 0.45, 0.7, 700, 150), 0.8, 0.25, 0.6),
                plucked(saw(-8, 0, 9, 0.5, 1.0, 1200, 400), 0.7, 0.2, 0.5), plucked(saw(-11, 1, 7, 0.45, 1.0, 1500, 900), 0.6, 0.2, 0.4),
                kOff, kOff, air(-30, kPink, 5000, 12000, 0.001, 0.12, 0.0, 0.1)},
     .fx = smooth({.driveDb = 0.8, .chorus = 0.25, .ott = 0.35, .width = 0.5, .delay = 0.3, .delayFeedback = 0.4, .delayBeats = 0.75,
                   .reverb = 0.35, .reverbDecay = 2.5, .reverbSize = 0.65, .sidechain = 0.3, .trimDb = 6.1},
                  11000),
     .attack = 0.001,
     .release = 0.3},
    {.key = "future-pluck",
     .name = "Future Pluck",
     .family = "PLUCK",
     .layers = {kOff, kOff, glide(plucked(saw(-4, 0, 5, 0.35, 0.6, 900, 200, kSquare), 0.75, 0.15, 0.25), 12, 0.015),
                glide(plucked(saw(-8, 0, 7, 0.5, 1.0, 1400, 400), 0.7, 0.15, 0.25), 12, 0.015),
                glide(plucked(saw(-12, 1, 5, 0.4, 1.0, 2000, 1000), 0.6, 0.12, 0.2), 12, 0.015), kOff, kOff, kOff},
     .fx = smooth({.driveDb = 1.0, .chorus = 0.2, .ott = 0.4, .width = 0.55, .delay = 0.35, .delayFeedback = 0.35, .delayBeats = 0.75,
                   .reverb = 0.35, .reverbDecay = 2.5, .reverbSize = 0.65, .sidechain = 0.3, .trimDb = 5.2},
                  11000),
     .attack = 0.001,
     .release = 0.2},
    {.key = "trance-pluck",
     .name = "Trance Pluck",
     .family = "PLUCK",
     .layers = {kOff, kOff, resonant(plucked(saw(-4, 0, 7, 0.45, 0.6, 500, 150), 0.8, 0.18, 0.35), 0.3),
                resonant(plucked(saw(-9, 0, 7, 0.5, 1.0, 700, 400), 0.75, 0.16, 0.3), 0.25), kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 0.5, .chorus = 0.2, .ott = 0.35, .width = 0.5, .delay = 0.4, .delayFeedback = 0.45, .delayBeats = 0.75,
                   .reverb = 0.4, .reverbDecay = 3.0, .reverbSize = 0.75, .sidechain = 0.25, .trimDb = 4.7},
                  11500),
     .attack = 0.001,
     .release = 0.25},
    {.key = "tropical-pluck",
     .name = "Tropical Pluck",
     .family = "PLUCK",
     .layers = {kOff, kOff, plucked(saw(-4, 0, 2, 0.15, 0.3, 6000, 150, kTriangle), 0.0, 0.1, 0.3),
                plucked(saw(-10, 1, 1, 0.0, 0.0, 8000, 400, kSine), 0.0, 0.1, 0.2),
                above(plucked(saw(-16, 1, 1, 0.0, 0.0, 9000, 800, kTriangle), 0.0, 0.1, 0.12), 7), kOff, kOff, kOff},
     .fx = smooth({.chorus = 0.2, .ott = 0.2, .width = 0.4, .delay = 0.3, .delayFeedback = 0.3, .delayBeats = 0.5, .reverb = 0.35,
                   .reverbDecay = 2.0, .reverbSize = 0.6, .sidechain = 0.2, .trimDb = 7.9},
                  9000),
     .attack = 0.001,
     .release = 0.2},
    {.key = "rave-stab",
     .name = "Rave Stab",
     .family = "PLUCK",
     .layers = {kOff, kOff, plucked(saw(-5, 0, 7, 0.5, 0.6, 1500, 200), 0.6, 0.2, 0.25),
                above(plucked(saw(-8, 0, 5, 0.5, 0.8, 1800, 400), 0.6, 0.2, 0.25), 7),
                plucked(saw(-10, 1, 5, 0.45, 0.8, 2500, 900), 0.5, 0.2, 0.2), kOff, kOff, kOff},
     .fx = smooth({.driveDb = 2.0, .chorus = 0.2, .ott = 0.35, .width = 0.45, .delay = 0.15, .delayFeedback = 0.3, .delayBeats = 0.75,
                   .reverb = 0.3, .reverbDecay = 2.0, .reverbSize = 0.6, .sidechain = 0.3, .trimDb = 7.3},
                  10500),
     .attack = 0.001,
     .release = 0.2},

    // ================= PAD
    {.key = "supersaw-pad",
     .name = "Supersaw Pad",
     .family = "PAD",
     .layers = {sub(-14), kOff, breathe(saw(-6, 0, 9, 0.5, 0.8, 4000, 150), 0.5, 0.12), saw(-8, 0, 9, 0.55, 1.0, 6000, 400),
                saw(-12, 1, 7, 0.5, 1.0, 8000, 1200), kOff, kOff, air(-28, kPink, 3000, 9000, 0.8, 0.1, 1.0, 2.5)},
     .fx = smooth({.chorus = 0.5, .ott = 0.2, .width = 0.7, .delay = 0.15, .delayFeedback = 0.4, .delayBeats = 1.5, .reverb = 0.6,
                   .reverbDecay = 6.0, .reverbSize = 0.95, .sidechain = 0.2, .trimDb = 3.6},
                  9000),
     .attack = 0.6,
     .release = 2.5},
    {.key = "dream-pad",
     .name = "Dream Pad",
     .family = "PAD",
     .layers = {kOff, kOff, vibrato(saw(-5, 0, 7, 0.4, 0.8, 4500, 150, kTriangle), 6, 0.3),
                breathe(saw(-11, 0, 7, 0.5, 1.0, 2500, 300), 0.6, 0.08), vibrato(saw(-11, 1, 5, 0.4, 1.0, 7000, 900, kTriangle), 6, 0.3),
                kOff, kOff, air(-32, kPink, 4000, 9000, 1.0, 0.1, 1.0, 3.0)},
     .fx = smooth({.chorus = 0.5, .ott = 0.1, .width = 0.7, .delay = 0.2, .delayFeedback = 0.45, .delayBeats = 1.5, .reverb = 0.65,
                   .reverbDecay = 7.0, .reverbSize = 1.0, .sidechain = 0.0, .trimDb = 0.2},
                  8000),
     .attack = 0.8,
     .release = 3.0},
    {.key = "analog-pad",
     .name = "Analog Warm Pad",
     .family = "PAD",
     .layers = {kOff, kOff, breathe(pwm(saw(-5, 0, 3, 0.3, 0.6, 2500, 120), 0.7, 0.35), 0.35, 0.2),
                breathe(pwm(saw(-9, 1, 3, 0.3, 0.8, 4000, 600), 0.6, 0.27), 0.35, 0.2), saw(-12, 0, 3, 0.3, 0.8, 2000, 200), kOff, kOff,
                kOff},
     .fx = smooth({.chorus = 0.7, .ott = 0.1, .width = 0.4, .delay = 0.15, .delayFeedback = 0.4, .delayBeats = 1.5, .reverb = 0.5,
                   .reverbDecay = 4.0, .reverbSize = 0.85, .sidechain = 0.0, .trimDb = -0.1},
                  8500),
     .attack = 0.5,
     .release = 2.0},
    {.key = "swell-pad",
     .name = "Swell Pad",
     .family = "PAD",
     .layers = {kOff, low(-15, 500), saw(-5, 0, 9, 0.5, 0.7, 5000, 200), saw(-9, 0, 9, 0.55, 1.0, 7000, 450),
                saw(-12, 1, 7, 0.5, 1.0, 9500, 1200), kOff, kOff, air(-32, kPink, 4000, 11000, 0.3, 0.1, 1.0, 1.5)},
     .fx = smooth({.chorus = 0.4, .ott = 0.25, .width = 0.6, .delay = 0.1, .delayFeedback = 0.3, .delayBeats = 0.75, .reverb = 0.5,
                   .reverbDecay = 4.5, .reverbSize = 0.9, .sidechain = 0.8, .sidechainBeats = 2.0, .sidechainCurve = kSwell,
                   .trimDb = 2.8},
                  9500),
     .attack = 0.3,
     .release = 1.5},
    {.key = "air-pad",
     .name = "Airy Saw Pad",
     .family = "PAD",
     .layers = {kOff, kOff, saw(-6, 0, 9, 0.55, 1.0, 7000, 900), saw(-9, 1, 7, 0.5, 1.0, 10000, 1500), kOff, kOff, kOff,
                air(-24, kPink, 3000, 11000, 1.2, 0.1, 1.0, 3.5)},
     .fx = smooth({.chorus = 0.5, .ott = 0.15, .width = 0.8, .delay = 0.2, .delayFeedback = 0.45, .delayBeats = 1.5, .reverb = 0.65,
                   .reverbDecay = 7.0, .reverbSize = 1.0, .sidechain = 0.0, .trimDb = 8.8},
                  11000),
     .attack = 0.9,
     .release = 3.5},

    // ================= BASS (one note under the chords: every layer mono, the lowest note)
    {.key = "reese-bass",
     .name = "Reese Bass",
     .family = "BASS",
     .layers = {sub(-6, -1), mono(breathe(saw(-7, 0, 2, 0.6, 0.0, 900, 40), 0.4, 0.25)),
                {.on = true, .levelDb = -10, .voices = 3, .detune = 0.35, .mix = 0.6, .width = 0.3, .wave = kSaw, .cutoffHz = 1800,
                 .resonance = 0.1, .lowCutHz = 150, .lowestOnly = true},
                kOff, kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 3, .cutMud = false, .ott = 0.45, .sidechain = 0.5, .trimDb = 2.3}, 7000),
     .attack = 0.003,
     .release = 0.12},
    {.key = "saw-bass",
     .name = "Saw Bass",
     .family = "BASS",
     .layers = {sub(-8, -1), mono(plucked(saw(-6, 0, 3, 0.3, 0.0, 600, 50), 0.5, 0.25, 0.4, 0.6)), kOff, kOff, kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 2.0, .cutMud = false, .ott = 0.35, .sidechain = 0.55, .trimDb = 5.5}, 8000),
     .attack = 0.002,
     .release = 0.12},
    {.key = "octave-bass",
     .name = "Octave Bass",
     .family = "BASS",
     .layers = {sub(-7, -1), mono(saw(-8, 0, 1, 0.0, 0.0, 1200, 120)), mono(saw(-12, 1, 3, 0.3, 0.3, 2500, 300)), kOff, kOff, kOff,
                kOff, kOff},
     .fx = smooth({.driveDb = 1.5, .cutMud = false, .chorus = 0.15, .ott = 0.35, .sidechain = 0.6, .trimDb = 4.6}, 8000),
     .attack = 0.002,
     .release = 0.12},
    {.key = "hoover-bass",
     .name = "Hoover Bass",
     .family = "BASS",
     .layers = {sub(-8, -1), mono(glide(pwm(saw(-6, 0, 5, 0.5, 0.3, 1800, 60), 0.7, 0.5), -12, 0.15)),
                mono(glide(saw(-12, 1, 3, 0.4, 0.5, 3000, 400), -12, 0.15)), kOff, kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 2.0, .cutMud = false, .chorus = 0.3, .ott = 0.35, .sidechain = 0.5, .trimDb = 2.9}, 7500),
     .attack = 0.003,
     .release = 0.15},
    {.key = "pluck-bass",
     .name = "Pluck Bass",
     .family = "BASS",
     .layers = {own(sub(-8, -1), 0.001, 0.35, 0.0, 0.1), mono(plucked(saw(-6, 0, 3, 0.25, 0.0, 400, 50), 0.7, 0.12, 0.3)),
                mono(plucked(saw(-14, 1, 3, 0.3, 0.3, 800, 300), 0.6, 0.1, 0.2)), kOff, kOff, kOff, kOff, kOff},
     .fx = smooth({.driveDb = 1.5, .cutMud = false, .ott = 0.3, .delay = 0.1, .delayFeedback = 0.2, .delayBeats = 0.75, .sidechain = 0.3,
                   .trimDb = 11.1},
                  8000),
     .attack = 0.001,
     .release = 0.1},
}};

// A bass plays one note under the chords: every layer under the lowest held note.
consteval bool bassesAreMono()
{
    return std::ranges::all_of(kTypes, [](const StackType& t) {
        return std::string_view(t.family) != "BASS" || std::ranges::none_of(t.layers, [](const LayerDesign& l) { return l.on && !l.lowestOnly; });
    });
}
static_assert(bassesAreMono());

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
