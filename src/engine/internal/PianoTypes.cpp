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
// The piano alone (the other layers there to switch on).
constexpr std::array<LayerDesign, kLayers> kAlone{on(-6), off(-14), off(-18), off(-12)};

// Layers: piano, octave up, strings, sub. (Keys never change once released:
// "electric-piano" is the Rhodes, as it was when it was an FM one.)
constexpr std::array<PianoType, 19> kTypes{{
    // ================= ACOUSTIC: the real pianos, as they sound in a room.
    // A concert grand, every one of its loudness layers there: a little hall.
    {.key = "grand-piano",
     .name = "Concert Grand",
     .family = "ACOUSTIC",
     .source = Source::Grand,
     .layers = kAlone,
     .fx = {.cutMud = false, .width = 0.15, .reverb = 0.2, .reverbDecay = 2.4, .reverbSize = 0.75, .trimDb = 0.0},
     .release = 0.45,
     .sidechainOn = false},
    // Pop and rock: played into its harder layers, its top lifted.
    {.key = "bright-grand",
     .name = "Bright Grand",
     .family = "ACOUSTIC",
     .source = Source::Grand,
     .softness = -0.35,
     .layers = kAlone,
     .fx = {.driveDb = 1, .width = 0.2, .reverb = 0.16, .reverbDecay = 1.8, .reverbSize = 0.6, .trimDb = -3.1, .mudDb = -2.0, .harshDb = 0.0,
            .airDb = 3.0},
     .release = 0.35,
     .sidechainOn = false},
    // Quiet songs: its softer layers, the top rounded, a long room.
    {.key = "ballad-grand",
     .name = "Ballad Grand",
     .family = "ACOUSTIC",
     .source = Source::Grand,
     .softness = 0.2,
     .layers = kAlone,
     .fx = {.cutMud = false, .width = 0.3, .reverb = 0.36, .reverbDecay = 3.6, .reverbSize = 0.9, .trimDb = 3.4},
     .toneHz = 9000,
     .release = 0.7,
     .sidechainOn = false},
    {.key = "upright-piano",
     .name = "Upright Piano",
     .family = "ACOUSTIC",
     .source = Source::Upright,
     .layers = kAlone,
     .fx = {.cutMud = false, .width = 0.15, .reverb = 0.15, .reverbDecay = 1.5, .reverbSize = 0.45, .trimDb = 0.0},
     .release = 0.35,
     .sidechainOn = false},
    // Soft hammers, dark, on tape, a long room and a half-note echo.
    {.key = "felt-piano",
     .name = "Felt Piano",
     .family = "ACOUSTIC",
     .source = Source::Upright,
     .softness = 0.6,
     .layers = {on(-4), off(-14), off(-18), off(-12)},
     .fx = {.width = 0.3, .delay = 0.2, .delayFeedback = 0.35, .delayBeats = 2.0, .reverb = 0.5, .reverbDecay = 5.0, .reverbSize = 0.9,
            .sidechain = 0.15, .sidechainCurve = kSmooth, .trimDb = 10.9},
     .toneHz = 3200,
     .lowCutHz = 60,
     .attack = 0.004,
     .release = 0.8,
     .tape = 0.45,
     .sidechainOn = false},

    // ================= ELECTRIC: the real electric pianos.
    {.key = "electric-piano",
     .name = "Rhodes",
     .family = "ELECTRIC",
     .source = Source::Rhodes,
     .layers = kAlone,
     .fx = {.cutMud = false, .width = 0.2, .reverb = 0.16, .reverbDecay = 1.8, .reverbSize = 0.55, .trimDb = 0.0},
     .release = 0.3,
     .sidechainOn = false},
    // Its amp's tremolo: left to right and back, as the suitcase Rhodes does.
    {.key = "suitcase-rhodes",
     .name = "Suitcase Rhodes",
     .family = "ELECTRIC",
     .source = Source::Rhodes,
     .layers = kAlone,
     .fx = {.cutMud = false, .chorus = 0.12, .width = 0.3, .reverb = 0.22, .reverbDecay = 2.2, .reverbSize = 0.6, .trimDb = 2.8},
     .release = 0.35,
     .tremolo = 0.6,
     .tremoloHz = 4.8,
     .sidechainOn = false},
    {.key = "wurlitzer",
     .name = "Wurlitzer",
     .family = "ELECTRIC",
     .source = Source::Wurlitzer,
     .layers = kAlone,
     .fx = {.driveDb = 3, .cutMud = false, .width = 0.15, .reverb = 0.15, .reverbDecay = 1.6, .reverbSize = 0.5, .trimDb = 0.0},
     .release = 0.25,
     .sidechainOn = false},
    // The 80s ballad keyboard: its strings' bite, chorused wide.
    {.key = "electric-grand",
     .name = "Electric Grand",
     .family = "ELECTRIC",
     .source = Source::ElectricGrand,
     .layers = kAlone,
     .fx = {.cutMud = false, .chorus = 0.35, .width = 0.4, .reverb = 0.22, .reverbDecay = 2.4, .reverbSize = 0.65, .trimDb = 0.0},
     .release = 0.4,
     .sidechainOn = false},
    {.key = "pianet",
     .name = "Pianet",
     .family = "ELECTRIC",
     .source = Source::Pianet,
     .layers = kAlone,
     .fx = {.cutMud = false, .width = 0.15, .reverb = 0.15, .reverbDecay = 1.5, .reverbSize = 0.45, .trimDb = 0.0},
     .release = 0.2,
     .tape = 0.1,
     .sidechainOn = false},
    // The DX7 electric piano: FM, bright and glassy.
    {.key = "dx-piano",
     .name = "DX Piano",
     .family = "ELECTRIC",
     .source = Source::Dx,
     .layers = kAlone,
     .fx = {.chorus = 0.35, .width = 0.45, .delay = 0.2, .delayFeedback = 0.3, .delayBeats = 0.75, .reverb = 0.3, .reverbDecay = 3.0,
            .reverbSize = 0.7, .sidechain = 0.3, .trimDb = 0.0},
     .release = 0.5,
     .sidechainOn = false},
    // Lo-fi house: a soft, dark, wobbly Rhodes.
    {.key = "lofi-keys",
     .name = "Lo-fi Keys",
     .family = "ELECTRIC",
     .source = Source::Rhodes,
     .softness = 0.3,
     .layers = kAlone,
     .fx = {.chorus = 0.3, .width = 0.3, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 1.5, .reverb = 0.3, .reverbDecay = 2.5,
            .reverbSize = 0.6, .sidechain = 0.3, .sidechainCurve = kSmooth, .trimDb = 5.5},
     .toneHz = 4200,
     .release = 0.4,
     .tape = 0.7,
     .sidechainOn = false},

    // ================= SYNTH: an 80s polysynth (the Rolands' way: saw,
    // pulse and sub into a 24 dB low-pass, its own envelope, the chorus).
    {.key = "poly-keys",
     .name = "Poly Keys",
     .family = "SYNTH",
     .source = Source::Poly,
     .layers = kAlone,
     .fx = {.chorus = 0.5, .width = 0.3, .delay = 0.12, .delayFeedback = 0.25, .delayBeats = 0.75, .reverb = 0.2, .reverbDecay = 2.2,
            .reverbSize = 0.6, .trimDb = 6.1},
     .release = 0.35,
     .poly = {.saw = 0.8, .pulse = 0.5, .sub = 0.3, .cutoffHz = 700, .resonance = 0.15, .envOctaves = 3.5, .keyTrack = 0.6,
              .fAttack = 0.002, .fDecay = 0.5, .fSustain = 0.15, .fRelease = 0.4, .decay = 1.8, .sustain = 0.35},
     .sidechainOn = false},
    {.key = "poly-strings",
     .name = "Poly Strings",
     .family = "SYNTH",
     .source = Source::Poly,
     .layers = kAlone,
     .fx = {.chorus = 0.7, .width = 0.5, .reverb = 0.35, .reverbDecay = 3.0, .reverbSize = 0.8, .trimDb = 0.0},
     .attack = 0.35,
     .release = 1.0,
     .poly = {.saw = 1.0, .pulse = 0.4, .pwm = 0.3, .pwmHz = 0.5, .cutoffHz = 2600, .envOctaves = 0.6, .keyTrack = 0.5,
              .fAttack = 0.6, .fDecay = 1.5, .fSustain = 0.7, .fRelease = 1.0},
     .sidechainOn = false},
    {.key = "poly-brass",
     .name = "Poly Brass",
     .family = "SYNTH",
     .source = Source::Poly,
     .layers = kAlone,
     .fx = {.chorus = 0.45, .width = 0.35, .reverb = 0.2, .reverbDecay = 2.0, .reverbSize = 0.6, .trimDb = -5.1},
     .attack = 0.03,
     .release = 0.3,
     .poly = {.saw = 1.0, .sub = 0.2, .cutoffHz = 500, .resonance = 0.1, .envOctaves = 3.0, .keyTrack = 0.5, .fAttack = 0.08,
              .fDecay = 0.5, .fSustain = 0.45, .fRelease = 0.3},
     .sidechainOn = false},

    // ================= HOUSE: the grand as dance records use it.
    // Korg M1 "Piano 16'": bright and hard, a quiet octave up in it, short room.
    {.key = "house-piano",
     .name = "House Piano",
     .family = "HOUSE",
     .source = Source::Grand,
     .softness = -0.3,
     .layers = {on(-6), on(-16), off(-18), off(-12)},
     .fx = {.driveDb = 3, .chorus = 0.1, .width = 0.3, .delay = 0.1, .delayFeedback = 0.25, .delayBeats = 0.75, .reverb = 0.22,
            .reverbDecay = 1.8, .reverbSize = 0.5, .sidechain = 0.35, .trimDb = -2.3},
     .lowCutHz = 120,
     .release = 0.3},
    // Piano for the body, an octave up for the top, strings two octaves up.
    {.key = "big-house-piano",
     .name = "Big House Piano",
     .family = "HOUSE",
     .source = Source::Grand,
     .softness = -0.2,
     .layers = {on(-6), on(-9), on(-12), off(-12)},
     .fx = {.driveDb = 5, .chorus = 0.15, .width = 0.6, .delay = 0.12, .delayFeedback = 0.3, .delayBeats = 0.75, .reverb = 0.3,
            .reverbDecay = 2.5, .reverbSize = 0.7, .sidechain = 0.6, .sidechainCurve = kPunchy, .trimDb = 0.4},
     .lowCutHz = 150,
     .release = 0.4},
    // Avicii's: bass and treble cut hard; a darker room.
    {.key = "prog-house-piano",
     .name = "Prog House Piano",
     .family = "HOUSE",
     .source = Source::Grand,
     .layers = kAlone,
     .fx = {.driveDb = 2, .width = 0.35, .delay = 0.2, .delayFeedback = 0.35, .delayBeats = 0.75, .reverb = 0.35, .reverbDecay = 3.0,
            .reverbSize = 0.75, .sidechain = 0.45, .trimDb = 3.1},
     .toneHz = 6500,
     .lowCutHz = 300,
     .release = 0.5},
    // Future bass breakdowns: long tails, strings under it.
    {.key = "emotional-piano",
     .name = "Emotional Piano",
     .family = "HOUSE",
     .source = Source::Grand,
     .layers = {on(-6), off(-14), on(-17), off(-12)},
     .fx = {.chorus = 0.1, .width = 0.5, .delay = 0.22, .delayFeedback = 0.4, .delayBeats = 1.0, .reverb = 0.55, .reverbDecay = 6.0,
            .reverbSize = 0.95, .sidechain = 0.3, .sidechainCurve = kSmooth, .trimDb = 0.6},
     .lowCutHz = 60,
     .release = 1.2},
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
