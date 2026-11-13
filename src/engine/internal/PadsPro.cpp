#include "PadsPro.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace gigchain::engine::pads {

namespace {

// Angelic's macros (its Timbre: the strings above the choir).
constexpr Macro kAngelicTimbre{{{.target = Target::LayerB, .amount = 9.0}}};
constexpr Macro kAngelicMovement{
    {{.target = Target::Motion, .amount = 1.0}, {.target = Target::Chorus, .amount = 0.4}, {.target = Target::Breath, .amount = 0.3}}};

// The pads: each its own instrument, its sound made by its own engine
// (PadsProEngines.h), as the collections players love are built (each
// preset a patch on a whole instrument: a piano, a guitar, a section of
// violins, an organ, a Moog, a Juno, a DX7...). The first eight keep the
// keys songs and Sustain Pads saved before this ask for. Levels in dB,
// balanced by measurement (tst_pads_pro: theTypesAreEquallyLoud).
constexpr std::array<PadType, 23> kTypes{{
    // ---- Synth.
    // A Juno: one oscillator (its pulse moving, a saw, the square an octave
    // below), the 4-pole opening as it swells, its chorus I.
    {.key = "warm", .name = "Juno Warmth", .family = "SYNTH", .about = "A Juno's pulse, saw and sub through its 4-pole and chorus",
     .chorus = 0.55, .echo = 0.1, .reverbSeconds = 5.0, .reverbSize = 0.8, .reverbDampHz = 6000.0, .lowCutHz = 40.0, .airDb = 0.0, .topHz = 9000.0,
     .attack = 0.9, .release = 3.5, .width = 0.2, .reverb = 0.35, .levelDb = 0.9, .engine = Engine::Juno},
    // A JP-8000's supersaw: seven saws spread as measured, the high-pass on the note.
    {.key = "lush", .name = "Supersaw Haze", .family = "SYNTH", .about = "Seven spread saws, the JP way, opening slowly; echo and hall",
     .chorus = 0.2, .echo = 0.2, .echoSeconds = 0.5, .reverbSeconds = 8.0, .reverbSize = 0.9, .reverbDampHz = 7000.0, .lowCutHz = 40.0,
     .airDb = 0.0, .topHz = 8000.0, .attack = 1.4, .release = 5.0, .width = 0.3, .reverb = 0.45, .levelDb = 3.4, .engine = Engine::Supersaw,
     .patch = {.supersaw = {.cutoffHz = 2500.0, .envAttack = 1.8}}},
    // A Moog: two saws and a triangle an octave below into the driven ladder.
    {.key = "dark", .name = "Moog Low Glow", .family = "SYNTH", .about = "Three oscillators into a warm, driven Moog ladder",
     .chorus = 0.0, .echo = 0.1, .reverbSeconds = 6.0, .reverbSize = 0.8, .reverbDampHz = 5000.0, .lowCutHz = 30.0, .airDb = 0.0, .topHz = 7000.0,
     .attack = 1.2, .release = 4.0, .width = 0.1, .reverb = 0.35, .levelDb = -2.8, .engine = Engine::Moog},
    // ---- Air.
    // A blown pipe: the breath through the jet into the bore, its noise and vibrato.
    {.key = "air", .name = "Breath Flute", .family = "AIR", .about = "A blown pipe: breath, jet and bore, a gentle vibrato",
     .chorus = 0.15, .echo = 0.25, .echoSeconds = 0.45, .reverbSeconds = 7.0, .reverbSize = 0.9, .reverbDampHz = 8000.0, .lowCutHz = 60.0,
     .airDb = 0.0, .topHz = 10000.0, .attack = 0.4, .release = 3.0, .width = 0.2, .reverb = 0.5, .levelDb = -8.2, .engine = Engine::Flute},
    // ---- Synth.
    // A DX7's bells: a bell pair (3.5) and a soft pair an octave up, a sub pair below.
    {.key = "shimmer", .name = "FM Crystal Bells", .family = "SYNTH", .about = "Six operators: struck bells over a soft, slowly breathing body",
     .chorus = 0.25, .echo = 0.3, .echoSeconds = 0.42, .reverbSeconds = 9.0, .reverbSize = 0.95, .reverbDampHz = 8000.0, .lowCutHz = 40.0,
     .airDb = 0.0, .topHz = 10000.0, .attack = 0.05, .release = 6.0, .width = 0.3, .reverb = 0.55, .levelDb = -3.4, .engine = Engine::Fm,
     .patch = {.fm = {.ops = {{{.ratio = 1.0, .level = 0.8, .attack = 0.02, .decay = 4.0, .sustain = 0.6, .release = 4.0, .pan = -0.3},
                               {.ratio = 3.5, .level = 0.35, .attack = 0.005, .decay = 2.5, .sustain = 0.15, .release = 3.0, .into = 1},
                               {.ratio = 2.0, .detuneHz = 0.7, .level = 0.4, .attack = 0.5, .decay = 3.0, .sustain = 0.8, .release = 4.0, .pan = 0.3},
                               {.ratio = 1.0, .level = 0.25, .attack = 1.5, .decay = 5.0, .sustain = 0.5, .release = 4.0, .into = 3},
                               {.ratio = 0.5, .level = 0.3, .attack = 1.2, .decay = 4.0, .sustain = 0.9, .release = 4.0},
                               {.ratio = 1.0, .level = 0.15, .attack = 1.0, .decay = 4.0, .sustain = 0.5, .release = 4.0, .feedback = 0.3, .into = 5}}},
                      .lfoHz = 0.15, .lfoDepth = 0.3}}},
    // ---- Air.
    // A tape-replay choir: one tape a note, its wow and flutter, the dull top.
    {.key = "choir", .name = "Tape Choir", .family = "AIR", .about = "A tape-replay choir: wow, flutter and a soft dull top",
     .chorus = 0.1, .echo = 0.1, .reverbSeconds = 6.0, .reverbSize = 0.85, .reverbDampHz = 6000.0, .lowCutHz = 60.0, .airDb = 0.0, .topHz = 6000.0,
     .attack = 0.25, .release = 2.5, .width = 0.3, .reverb = 0.45, .levelDb = -6.2, .engine = Engine::Mellotron},
    // ---- Strings.
    // A real string section, recorded (VSCO 2 CE, CC0): the violins, the
    // cellos below them, sustained with vibrato, held as long as the key is.
    {.key = "strings", .name = "Orchestra Strings", .family = "STRINGS", .about = "A recorded violin section, cellos below, held with vibrato",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 5.0, .reverbSize = 0.85, .reverbDampHz = 7000.0, .lowCutHz = 40.0, .airDb = 0.0,
     .topHz = 14000.0, .attack = 0.3, .release = 2.5, .width = 0.2, .reverb = 0.45, .levelDb = -2.8, .engine = Engine::Sampled,
     .patch = {.sampled = {.set = "strings", .fallback = Engine::Bowed, .toneHz = 14000.0, .tuneCents = -4.5}}},
    // ---- Organ.
    // Tonewheels at 888000000, the key click, through a rotary speaker turning slowly.
    {.key = "organ", .name = "Tonewheel Organ", .family = "ORGAN", .about = "Drawbars out to 888, the key click, a slow rotary speaker",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 3.0, .reverbSize = 0.7, .reverbDampHz = 6000.0, .lowCutHz = 40.0, .airDb = 0.0, .topHz = 9000.0,
     .attack = 0.02, .release = 1.0, .width = 0.2, .reverb = 0.3, .levelDb = -6.7, .engine = Engine::Tonewheel, .rotary = 0.8},
    // ---- Air.
    // A soft "aah" choir of countless voices (spectral), airy strings an
    // octave above it, a little apart; the Juno's chorus (II); a big hall.
    {.key = "angelic", .name = "Angelic", .family = "AIR", .about = "A soft choir of countless voices, airy strings above; a big hall",
     .a = {.levelDb = 0.0, .pan = -0.15, .spectral = true, .spectrum = Spectrum::Choir, .bandwidthCents = 50.0},
     .b = {.levelDb = -8.0, .octave = 1, .pan = 0.15, .attackScale = 1.4, .releaseScale = 1.1, .spectral = true,
           .spectrum = Spectrum::Strings, .bandwidthCents = 25.0},
     .cutoffHz = 9000.0, .keyTrack = 0.0, .breathHz = 0.04, .breathOctaves = 0.1, .randomOctaves = 0.05,
     .chorus = 0.4, .echo = 0.0, .reverbSeconds = 9.0, .reverbSize = 1.0, .reverbDampHz = 8000.0, .lowCutHz = 140.0, .topHz = 8000.0,
     .attack = 1.8, .release = 6.0, .width = 0.4, .reverb = 0.5, .dispersion = 0.3, .drive = 0.1, .levelDb = -6.2,
     .macros = {.timbre = kAngelicTimbre, .movement = kAngelicMovement}, .chorusDeep = true},

    // ---- Piano.
    // A real grand piano, recorded (AKAI's Splendid Grand, public domain),
    // ringing out into a big room.
    {.key = "pianobloom", .name = "Grand Piano", .family = "PIANO", .about = "A recorded grand piano ringing into a big room",
     .chorus = 0.0, .echo = 0.15, .echoSeconds = 0.5, .reverbSeconds = 6.0, .reverbSize = 0.9, .reverbDampHz = 8000.0, .lowCutHz = 30.0,
     .airDb = 0.0, .topHz = 16000.0, .attack = 0.002, .release = 4.0, .width = 0.2, .reverb = 0.4, .levelDb = 4.0, .engine = Engine::Sampled,
     .patch = {.sampled = {.set = "piano", .fallback = Engine::Piano, .toneHz = 16000.0}}},
    // An electric piano's tine: bright at the strike, mellow held, its stereo tremolo.
    {.key = "electricdream", .name = "Electric Dream", .family = "PIANO", .about = "An electric piano's tine and pickup, its stereo tremolo",
     .chorus = 0.35, .echo = 0.25, .echoSeconds = 0.45, .reverbSeconds = 6.0, .reverbSize = 0.85, .reverbDampHz = 7000.0, .lowCutHz = 40.0,
     .airDb = 0.0, .topHz = 10000.0, .attack = 0.01, .release = 3.0, .width = 0.2, .reverb = 0.4, .levelDb = -7.9, .engine = Engine::Tine},
    // ---- Guitar.
    // A guitar string driven for ever, swelling in, echoes and a big room.
    {.key = "ebowsky", .name = "E-Bow Sky", .family = "GUITAR", .about = "A guitar string driven for ever: it swells and sings",
     .chorus = 0.3, .echo = 0.35, .echoSeconds = 0.55, .echoFeedback = 0.45, .reverbSeconds = 9.0, .reverbSize = 0.95, .reverbDampHz = 6000.0,
     .lowCutHz = 60.0, .airDb = 0.0, .topHz = 8000.0, .attack = 0.3, .release = 4.0, .width = 0.3, .reverb = 0.5, .levelDb = -8.9,
     .engine = Engine::Ebow},
    // ---- Strings.
    // A string machine: every footage from one divided-down saw, the triple ensemble.
    {.key = "stringmachine", .name = "String Machine", .family = "STRINGS", .about = "A 70s string machine: locked saws through the ensemble",
     .chorus = 0.2, .ensemble = 0.8, .echo = 0.1, .reverbSeconds = 5.0, .reverbSize = 0.8, .reverbDampHz = 7000.0, .lowCutHz = 50.0, .airDb = 0.0,
     .topHz = 8000.0, .attack = 0.6, .release = 3.0, .width = 0.3, .reverb = 0.4, .levelDb = -7.4, .engine = Engine::StringMachine},
    // ---- Organ.
    // Flue pipes: 16', 8' and 2 2/3', the air's chiff at each start, a long stone hall.
    {.key = "pipeorgan", .name = "Pipe Organ", .family = "ORGAN", .about = "Ranks of flue pipes, the air at each start, a long stone hall",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 10.0, .reverbSize = 1.0, .reverbDampHz = 5000.0, .lowCutHz = 30.0, .airDb = 0.0, .topHz = 9000.0,
     .attack = 0.08, .release = 2.5, .width = 0.3, .reverb = 0.6, .levelDb = -4.4, .engine = Engine::PipeOrgan},
    // Two ranks of free reeds beating gently, the bellows breathing.
    {.key = "harmonium", .name = "Harmonium", .family = "ORGAN", .about = "Free reeds under a bellows: two ranks beating, breathing",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 4.0, .reverbSize = 0.75, .reverbDampHz = 6000.0, .lowCutHz = 40.0, .airDb = 0.0, .topHz = 7000.0,
     .attack = 0.25, .release = 1.5, .width = 0.2, .reverb = 0.35, .levelDb = -5.2, .engine = Engine::Harmonium},
    // ---- Synth.
    // Two whole synths side by side (a saw and a sine; a moving square through
    // a band), each with its own high- and low-pass and envelope.
    {.key = "twinhorizon", .name = "Twin Horizon", .family = "SYNTH", .about = "Two whole synths side by side, a CS-80's two channels",
     .chorus = 0.45, .echo = 0.15, .reverbSeconds = 8.0, .reverbSize = 0.9, .reverbDampHz = 7000.0, .lowCutHz = 40.0, .airDb = 0.0,
     .topHz = 9000.0, .attack = 1.0, .release = 5.0, .width = 0.3, .reverb = 0.5, .levelDb = 2.1, .engine = Engine::TwinAnalog,
     .patch = {.twin = {.one = {.saw = 0.8, .sine = 0.3, .detuneCents = -2.0, .pan = -0.3, .hpHz = 80.0, .lpHz = 1600.0, .resonance = 0.2, .envOctaves = 0.8,
                                .envAttack = 1.5, .envDecay = 3.0, .envSustain = 0.5},
                        .two = {.saw = 0.0, .square = 0.7, .pwmDepth = 0.2, .pwmHz = 0.4, .detuneCents = 2.0, .pan = 0.3, .hpHz = 300.0,
                                .lpHz = 2500.0, .resonance = 0.3, .envOctaves = 0.6, .envAttack = 2.5, .envDecay = 4.0, .envSustain = 0.4},
                        .vibratoHz = 5.5, .vibratoCents = 4.0}}},
    // Four waves (a sine, a voice, glass, a hollow) crossfaded along a slowly looping path.
    {.key = "vectorsky", .name = "Vector Sky", .family = "SYNTH", .about = "Four waves crossfaded along a slow, looping joystick path",
     .chorus = 0.3, .echo = 0.25, .echoSeconds = 0.6, .echoFeedback = 0.4, .reverbSeconds = 9.0, .reverbSize = 0.95, .reverbDampHz = 7000.0,
     .lowCutHz = 40.0, .airDb = 0.0, .topHz = 9000.0, .attack = 1.2, .release = 5.0, .width = 0.3, .reverb = 0.5, .levelDb = -6.9,
     .engine = Engine::Vector},
    // ---- Air.
    // A singing bowl: its modes rubbed while held, struck now and then.
    {.key = "singingbowl", .name = "Singing Bowl", .family = "AIR", .about = "A singing bowl's modes, rubbed while held and struck now and then",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 10.0, .reverbSize = 1.0, .reverbDampHz = 8000.0, .lowCutHz = 40.0, .airDb = 0.0,
     .topHz = 10000.0, .attack = 0.5, .release = 6.0, .width = 0.2, .reverb = 0.5, .levelDb = 0.4, .engine = Engine::Modal},
    // A cloud of short grains on the note's partials, each its own place and pitch.
    {.key = "nebula", .name = "Nebula", .family = "AIR", .about = "A cloud of tiny grains on the note's partials",
     .chorus = 0.2, .echo = 0.3, .echoSeconds = 0.5, .echoFeedback = 0.4, .reverbSeconds = 12.0, .reverbSize = 1.0, .reverbDampHz = 7000.0,
     .lowCutHz = 40.0, .airDb = 0.0, .topHz = 9000.0, .attack = 1.0, .release = 6.0, .width = 0.3, .reverb = 0.6, .levelDb = 0.7,
     .engine = Engine::Granular},
    // ---- Atmos: places, each tuned to the note played.
    // Gusts over a mountain, the wind howling on the note's harmonics.
    {.key = "mountainwind", .name = "Mountain Wind", .family = "ATMOS", .about = "Gusts of wind, howling softly on the note",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 6.0, .reverbSize = 0.9, .reverbDampHz = 6000.0, .lowCutHz = 40.0, .airDb = 0.0,
     .topHz = 9000.0, .attack = 2.5, .release = 5.0, .width = 0.3, .reverb = 0.4, .levelDb = -14.5, .engine = Engine::Wind},
    // Waves rolling in and breaking, the sea singing the note under them.
    {.key = "oceanshore", .name = "Ocean Shore", .family = "ATMOS", .about = "Waves rolling in and breaking, the sea singing the note",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 5.0, .reverbSize = 0.85, .reverbDampHz = 6000.0, .lowCutHz = 30.0, .airDb = 0.0,
     .topHz = 9000.0, .attack = 2.0, .release = 6.0, .width = 0.3, .reverb = 0.35, .levelDb = -11.7, .engine = Engine::Ocean},
    // Wind in leaves over a soft bed.
    {.key = "forestdawn", .name = "Forest Dawn", .family = "ATMOS", .about = "Wind in the leaves over a soft, warm bed",
     .chorus = 0.0, .echo = 0.15, .echoSeconds = 0.4, .reverbSeconds = 4.0, .reverbSize = 0.8, .reverbDampHz = 7000.0, .lowCutHz = 50.0,
     .airDb = 0.0, .topHz = 10000.0, .attack = 1.5, .release = 4.0, .width = 0.3, .reverb = 0.35, .levelDb = 0.0, .engine = Engine::Forest},
    // A cave: a deep, dark drone and rumble in a long dark space.
    {.key = "cavedwelling", .name = "Cave Dwelling", .family = "ATMOS", .about = "A deep, dark drone and rumble in a long dark space",
     .chorus = 0.0, .echo = 0.0, .reverbSeconds = 14.0, .reverbSize = 1.0, .reverbDampHz = 2500.0,
     .lowCutHz = 30.0, .airDb = 0.0, .topHz = 8000.0, .attack = 2.0, .release = 7.0, .width = 0.3, .reverb = 0.7, .levelDb = 2.8,
     .engine = Engine::Cave},
}};

constexpr std::array<double, kDrawbars> kDrawbarRatios{0.5, 1.0, 2.0, 3.0};
// The "aah" of a voice: its three strongest resonances (Hz), how sharp, how loud.
constexpr std::array<double, 3> kVowelHz{800.0, 1150.0, 2900.0};
constexpr std::array<double, 3> kVowelQ{3.0, 3.5, 4.5}; // (broad, as voices' are: sharper, the vibrato flutters the level through them)
constexpr std::array<float, 3> kVowelGain{1.0F, 0.5F, 0.25F};
constexpr float kVowelLevel = 0.7F; // the vowel as loud as the pad going in (it rings: more, and the top limits it)
constexpr float kVowelBody = 0.5F; // the voice between the resonances
// The texture's level against the pad (the Texture knob at its middle-high).
constexpr float kBellLevel = 0.3F;
constexpr float kAirLevel = 0.5F;
constexpr double kAirHz = 6500.0; // the breath's band: about 4 to 9 kHz
constexpr double kEchoLevel = 0.6; // a pad's echo against its `echo`
// White noise made at four times the rate spreads over four times the
// band: twice as loud keeps what we hear of it.
constexpr float kNoiseAtVoiceRate = 2.0F;
// A spectral loop at `at` (between samples: Catmull-Rom, wrapping round).
float spectralRead(const std::vector<float>& loop, double at)
{
    constexpr std::size_t kMask = SpectralTables::kLength - 1;
    const auto whole = static_cast<std::size_t>(at);
    const auto t = static_cast<float>(at - static_cast<double>(whole));
    const float y0 = loop.at((whole - 1) & kMask);
    const float y1 = loop.at(whole & kMask);
    const float y2 = loop.at((whole + 1) & kMask);
    const float y3 = loop.at((whole + 2) & kMask);
    return y1 + 0.5F * t * (y2 - y0 + t * (2.0F * y0 - 5.0F * y1 + 4.0F * y2 - y3 + t * (3.0F * (y1 - y2) + y3 - y0)));
}

// The top: clean up to the knee, then curving towards full scale (the old
// tanh on everything bent every chord a little, all the time).
constexpr float kKnee = 0.8F;
// Room under it: a five-note chord at the Master's 0 dB stays clean
// (measured: without it, chords sat in the knee 2-12 % of the time,
// Strings the most: heard as harshness on every chord).
constexpr double kHeadroomDb = -8.0;
float softTop(float x)
{
    const float size = std::abs(x);
    if (size <= kKnee) return x;
    return std::copysign(kKnee + 0.199F * fastTanh((size - kKnee) / 0.2F), x);
}

constexpr std::array<const char*, kParamCount> kParamNames{"Type",   "Brightness", "Chorus", "Movement", "Phaser", "Delay",
                                                           "Timbre", "Reverb",     "Master", "Touch",    "Time"};
// (Saved settings' keys: those kept from 0.9 as they were first released.)
constexpr std::array<const char*, kParamCount> kParamKeys{"type",   "tone",   "chorus", "motion",   "phaser", "delay",
                                                          "timbre", "reverb", "level",  "velocity", "time"};

float dbGain(double db)
{
    return db <= -60.0 ? 0.0F : static_cast<float>(std::pow(10.0, db / 20.0));
}

std::size_t at(Param p)
{
    return static_cast<std::size_t>(p);
}

} // namespace

std::span<const PadType> padTypes()
{
    return kTypes;
}
const PadType& padType(int index)
{
    return kTypes.at(static_cast<std::size_t>(std::clamp(index, 0, padTypeCount() - 1)));
}
int padTypeCount()
{
    return static_cast<int>(kTypes.size());
}
double typeValue(int index)
{
    return Ranges::fromChoice(index, padTypeCount());
}
int typeIndex(double value)
{
    return Ranges::choice(value, padTypeCount());
}

QString paramName(uint32_t id)
{
    if (id < kParamNames.size()) return QString::fromLatin1(kParamNames.at(id));
    return u"Parameter %1"_s.arg(id);
}

QString paramKey(uint32_t id)
{
    if (id < kParamKeys.size()) return QString::fromLatin1(kParamKeys.at(id));
    return u"parameter%1"_s.arg(id);
}

std::array<float, kParamCount> typeParams(int index)
{
    const PadType& t = padType(index);
    std::array<float, kParamCount> p{};
    // The macros in the middle (the pad as designed), the effects where the pad has them.
    p.at(at(Param::Type)) = static_cast<float>(typeValue(index));
    p.at(at(Param::Tone)) = 0.5F;
    p.at(at(Param::Timbre)) = 0.5F;
    p.at(at(Param::Time)) = 0.5F;
    p.at(at(Param::Motion)) = 0.5F;
    p.at(at(Param::Chorus)) = static_cast<float>(t.chorus);
    p.at(at(Param::Phaser)) = static_cast<float>(t.phaser);
    p.at(at(Param::Delay)) = static_cast<float>(t.echo);
    p.at(at(Param::Reverb)) = static_cast<float>(t.reverb);
    p.at(at(Param::Level)) = static_cast<float>(Ranges::fromLevelDb(0.0));
    p.at(at(Param::Velocity)) = 0.0F;
    return p;
}

std::array<float, kParamCount> defaultParams()
{
    return typeParams(0);
}

Synth::Synth()
{
    const auto start = defaultParams();
    for (std::size_t i = 0; i < m_params.size(); ++i) m_params.at(i).store(start.at(i), std::memory_order_relaxed);
    m_seen.fill(-1.0F);
}

void Synth::prepare(double sampleRate, int maxBlock)
{
    m_rate = sampleRate;
    m_voiceRate = sampleRate * kOversample;
    m_maxBlock = std::max(maxBlock, 64);
    m_voiceLeft.assign(static_cast<std::size_t>(m_maxBlock) * kOversample, 0.0F);
    m_voiceRight.assign(static_cast<std::size_t>(m_maxBlock) * kOversample, 0.0F);
    m_downLeft.prepare();
    m_downRight.prepare();
    m_tape.prepare(m_voiceRate);
    m_lofi.prepare(sampleRate);
    // Every pad's spectral loops, built now (allocating; shared with every other Pads Pro).
    m_spectralKeep.clear();
    m_spectralFor.assign(kTypes.size(), {nullptr, nullptr});
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
        for (std::size_t k = 0; k < kLayers; ++k) {
            const LayerType& lt = k == 0 ? kTypes.at(i).a : kTypes.at(i).b;
            if (!lt.spectral || lt.levelDb <= -60.0) continue;
            m_spectralKeep.push_back(spectralTables(lt.spectrum, lt.bandwidthCents, sampleRate));
            m_spectralFor.at(i).at(k) = m_spectralKeep.back().get();
        }
    }
    // Every pad's engine: its waves, its spectral tone, and each voice's strings and bores.
    m_wavesFor.assign(kTypes.size(), nullptr);
    m_engineSpectralFor.assign(kTypes.size(), nullptr);
    m_samplesFor.assign(kTypes.size(), nullptr);
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
        const PadType& t = kTypes.at(i);
        // A sampled pad: its recordings (or, when they are not there, its synth).
        if (t.engine == Engine::Sampled) m_samplesFor.at(i) = loadSampleSet(QString::fromLatin1(t.patch.sampled.set));
        const Engine engine = engineFor(i);
        m_wavesFor.at(i) = buildWaves(engine, t.patch);
        Spectrum spectrum = Spectrum::Choir;
        double bandwidth = 0.0;
        if (engineSpectrum(engine, spectrum, bandwidth)) {
            m_spectralKeep.push_back(spectralTables(spectrum, bandwidth, sampleRate));
            m_engineSpectralFor.at(i) = m_spectralKeep.back().get();
        }
    }
    for (Voice& v : m_voices) v.engine.allocate(m_voiceRate);
    m_engineLeft.assign(static_cast<std::size_t>(m_maxBlock) * kOversample, 0.0F);
    m_engineRight.assign(static_cast<std::size_t>(m_maxBlock) * kOversample, 0.0F);
    m_rotary.prepare(sampleRate);
    m_chorus.prepare(sampleRate);
    m_phaser.prepare(sampleRate);
    m_width.prepare(sampleRate);
    m_echo.prepare(sampleRate);
    m_reverb.prepare(sampleRate);
    m_ensembleL.allocate(static_cast<std::size_t>(0.02 * sampleRate) + 8);
    const double tick = kControl / m_voiceRate;
    m_driftGlide = static_cast<float>(1.0 - std::exp(-tick / 0.35));
    m_randomGlide = static_cast<float>(1.0 - std::exp(-tick / 0.9));
    m_seen.fill(-1.0F); // everything worked out again for this rate
    m_type = -1;
    applyParams();
}

void Synth::setParam(uint32_t id, double value) noexcept
{
    if (id >= static_cast<uint32_t>(kParamCount) || !std::isfinite(value)) return;
    const auto v = static_cast<float>(std::clamp(value, 0.0, 1.0));
    if (id == paramId(Param::Type)) {
        // A new pad brings its own knobs (as a preset does); Level and Velocity are the player's.
        const auto start = typeParams(typeIndex(v));
        for (std::size_t i = 0; i < start.size(); ++i) {
            const auto p = static_cast<Param>(i);
            if (p == Param::Type || p == Param::Level || p == Param::Velocity) continue;
            m_params.at(i).store(start.at(i), std::memory_order_relaxed);
        }
    }
    m_params.at(id).store(v, std::memory_order_relaxed);
}

double Synth::param(uint32_t id) const noexcept
{
    return id < static_cast<uint32_t>(kParamCount) ? m_params.at(id).load(std::memory_order_relaxed) : -1.0;
}

void Synth::applyParams() noexcept
{
    bool changed = false;
    std::array<float, kParamCount> now{};
    for (std::size_t i = 0; i < now.size(); ++i) {
        now.at(i) = m_params.at(i).load(std::memory_order_relaxed);
        changed = changed || now.at(i) != m_seen.at(i);
    }
    const float extraShimmer = m_extraShimmer.load(std::memory_order_relaxed);
    const bool quickAttack = m_quickAttack.load(std::memory_order_relaxed);
    changed = changed || extraShimmer != m_extraShimmerSeen || quickAttack != m_quickAttackSeen;
    if (!changed) return;
    m_seen = now;
    m_extraShimmerSeen = extraShimmer;
    m_quickAttackSeen = quickAttack;
    const auto knob = [&now](Param p) { return static_cast<double>(now.at(at(p))); };
    const int index = typeIndex(knob(Param::Type));
    const PadType& t = padType(index);
    m_type = index;
    m_d.type = &t;
    m_d.engine = engineFor(static_cast<std::size_t>(index));

    // The macros: each turned from its middle moves what the pad wired it to.
    std::array<double, static_cast<std::size_t>(Target::Count)> moved{};
    const auto turn = [&moved](const Macro& macro, double knobValue) {
        const double d = (std::clamp(knobValue, 0.0, 1.0) - 0.5) * 2.0;
        for (const MacroTarget& m : macro) {
            if (m.target != Target::None) moved.at(static_cast<std::size_t>(m.target)) += m.amount * d;
        }
    };
    turn(t.macros.brightness, knob(Param::Tone));
    turn(t.macros.timbre, knob(Param::Timbre));
    turn(t.macros.time, knob(Param::Time));
    turn(t.macros.movement, knob(Param::Motion));
    const auto by = [&moved](Target target) { return moved.at(static_cast<std::size_t>(target)); };

    const double pulseDb = by(Target::Pulse);
    const double bedDb = by(Target::Bed);
    const auto level = [](double db, double add) { return db <= -60.0 ? 0.0F : static_cast<float>(std::pow(10.0, (db + add) / 20.0)); };
    // Time stretches the swell and the tail on any pad (a design whose Time
    // moves other things still gets these).
    const double timeTurn = (std::clamp(knob(Param::Time), 0.0, 1.0) - 0.5) * 2.0;
    const bool timeShapes = std::ranges::any_of(t.macros.time, [](const MacroTarget& m) {
        return m.target == Target::Attack || m.target == Target::Release;
    });
    const double ownAttack = timeShapes ? 0.0 : 2.0 * timeTurn;
    const double ownRelease = timeShapes ? 0.0 : 1.5 * timeTurn;
    // A struck pad (bells, keys, an organ: its start all but instant) swells in
    // as Time is turned up, the way a slow pad does.
    const double swell = t.attack < 0.3 ? 1.2 * std::max(timeTurn, 0.0) : 0.0;
    m_d.attack = quickAttack ? 0.05 : (t.attack * std::exp2(by(Target::Attack) + ownAttack)) + swell;
    m_d.release = t.release * std::exp2(by(Target::Release) + ownRelease);
    for (std::size_t k = 0; k < kLayers; ++k) {
        const LayerType& lt = k == 0 ? t.a : t.b;
        LayerDerived& ld = m_d.layer.at(k);
        // (An engine's pad: its note is the first layer's, whole.)
        const bool engineNote = k == 0 && m_d.engine != Engine::Classic;
        ld.on = lt.levelDb > -60.0 || engineNote;
        ld.gain = engineNote ? level(0.0, bedDb) : level(lt.levelDb, bedDb + (k == 1 ? by(Target::LayerB) : 0.0));
        ld.saw = dbGain(lt.sawDb);
        ld.pulse = level(lt.pulseDb, pulseDb);
        ld.triangle = level(lt.triangleDb, pulseDb);
        ld.sub = dbGain(lt.subDb);
        ld.noise = dbGain(lt.noiseDb);
        ld.tables = static_cast<std::size_t>(index) < m_spectralFor.size() ? m_spectralFor.at(static_cast<std::size_t>(index)).at(k) : nullptr;
        ld.unison = std::clamp(lt.unison, 1, kUnison);
        ld.partner = static_cast<float>(std::pow(10.0, lt.partnerDb / 20.0));
        ld.norm = 1.0F / std::sqrt(1.0F + static_cast<float>(ld.unison - 1) * ld.partner * ld.partner);
        ld.stereo = static_cast<float>(std::clamp(lt.stereo, 0.0, 1.0));
        ld.stereoNorm = 1.0F / std::sqrt(((1.0F - ld.stereo) * (1.0F - ld.stereo)) + (ld.stereo * ld.stereo));
        // (A layer that is not there follows A's envelope: the note ends with A.)
        const LayerType& timing = ld.on ? lt : t.a;
        ld.attack = m_d.attack * timing.attackScale;
        ld.release = m_d.release * timing.releaseScale;
    }
    for (std::size_t b = 0; b < kDrawbars; ++b) m_d.drawbarGain.at(b) = level(t.drawbarsDb.at(b), bedDb);
    m_d.cutoffHz = t.cutoffHz * std::exp2(by(Target::Cutoff));
    m_d.resonance = std::clamp(t.resonance + by(Target::Resonance), 0.0, 0.7);
    m_d.breathOctaves = std::max(t.breathOctaves + by(Target::Breath), 0.0);
    m_d.motion = std::clamp(1.0 + by(Target::Motion), 0.0, 2.0);
    m_d.sweepOctaves = std::max(t.sweepOctaves + by(Target::Sweep), 0.0);
    m_d.brightness = by(Target::Cutoff);
    m_d.character = (std::clamp(knob(Param::Timbre), 0.0, 1.0) - 0.5) * 2.0;
    // The pad's own vibrato as it moves; moving more than designed, a gentle one comes in on any pad.
    m_d.vibratoCents = t.vibratoCents * std::min(m_d.motion, 1.0) + std::max(0.0, m_d.motion - 1.0) * 6.0;
    m_d.detuneScale = static_cast<float>(std::exp2(by(Target::Detune)));
    m_d.strikeScale = static_cast<float>(std::exp2(by(Target::Strike)));
    m_d.dispersion = t.dispersion * 2.0;
    m_d.width = 1.0;
    const double drive = std::clamp(t.drive + by(Target::Drive), 0.0, 1.0);
    m_d.drive = static_cast<float>(1.0 + 5.0 * drive * drive);
    const double texture = std::clamp(t.textureLevel + by(Target::Texture), 0.0, 1.0);
    m_d.textureGain = static_cast<float>(2.0 * texture * texture);
    m_d.velocity = static_cast<float>(knob(Param::Velocity));
    m_d.masterGain = static_cast<float>(std::pow(10.0, (Ranges::levelDb(knob(Param::Level)) + t.levelDb + kHeadroomDb) / 20.0));
    for (Voice& v : m_voices) {
        if (v.active() && !v.stolen) setEnvelopes(v);
    }

    const double lofi = std::clamp(t.lofi + by(Target::LoFi), 0.0, 1.0);
    m_tape.set(lofi, 0.0);
    m_lofi.set(0.0, lofi * t.crush);
    m_lowCutL.set(t.lowCutHz, 0.7071, m_rate);
    m_lowCutR.set(t.lowCutHz, 0.7071, m_rate);
    m_chorus.setMix(std::clamp(knob(Param::Chorus) + by(Target::Chorus), 0.0, 1.0));
    m_ensembleMix = static_cast<float>(t.ensemble);
    m_phaser.setMix(std::clamp(knob(Param::Phaser) + by(Target::Phaser), 0.0, 1.0));
    m_width.setAmount(t.width);
    // The echo sits under the pad (louder, a held chord pulses against its own repeats).
    const double echo = kEchoLevel * std::clamp(knob(Param::Delay) + by(Target::Echo), 0.0, 1.0);
    m_echo.set(echo, t.echoFeedback, t.echoSeconds); // (at 60 beats a minute: beats are seconds)
    Hall::Settings hall;
    hall.decaySeconds = t.reverbSeconds;
    hall.size = t.reverbSize;
    hall.dampHz = t.reverbDampHz;
    const double shimmer = std::clamp(t.shimmer + by(Target::Shimmer) + static_cast<double>(extraShimmer), 0.0, 1.0);
    m_reverb.set(hall, knob(Param::Reverb), shimmer);
    m_chorus.setMode(t.chorusDeep ? BbdChorus::Mode::Two : BbdChorus::Mode::One);
    m_air.set(sawstack::EqBand::Kind::HighShelf, 8000.0, t.airDb + by(Target::Air), 0.7, m_rate);
    // Brightness and Timbre on an engine's pad (a classic pad's filter and
    // layers answer them already); Movement past its design on any pad.
    const bool enginePad = m_d.engine != Engine::Classic;
    const double toneTurn = enginePad ? (std::clamp(knob(Param::Tone), 0.0, 1.0) - 0.5) * 2.0 : 0.0;
    const double timbreTurn = enginePad ? (std::clamp(knob(Param::Timbre), 0.0, 1.0) - 0.5) * 2.0 : 0.0;
    const double moveTurn = (std::clamp(knob(Param::Motion), 0.0, 1.0) - 0.5) * 2.0;
    m_moveOctaves = 4.5 * std::max(moveTurn, 0.0);
    m_toneHz = toneTurn < 0.0 ? 20000.0 * std::exp2(3.5 * toneTurn) : (m_moveOctaves > 0.0 ? 20000.0 : 0.0);
    m_shelfOn = toneTurn > 0.0;
    m_toneShelf.set(sawstack::EqBand::Kind::HighShelf, 3000.0, 7.0 * std::max(toneTurn, 0.0), 0.7, m_rate);
    m_timbreOn = timbreTurn != 0.0;
    // Down: warmer (the low mids up, the presence down); up: more forward and saturated.
    m_timbreLow.set(sawstack::EqBand::Kind::Peak, 250.0, 5.0 * std::max(-timbreTurn, 0.0), 0.7, m_rate);
    m_timbrePresence.set(sawstack::EqBand::Kind::Peak, 2500.0, 6.0 * timbreTurn, 0.8, m_rate);
    m_timbreDrive = static_cast<float>(1.0 + 3.0 * std::max(timbreTurn, 0.0));
    m_toneCountdown = 0;
    m_rotary.setMix(t.rotary);
    m_sideLow.set(150.0, 0.7071, m_rate);
    // (A 4th-order Butterworth: its two 2-poles' Qs.)
    m_topL1.set(t.topHz, 0.5412, m_rate);
    m_topR1.set(t.topHz, 0.5412, m_rate);
    m_topL2.set(t.topHz, 1.3066, m_rate);
    m_topR2.set(t.topHz, 1.3066, m_rate);
}

void Synth::setEnvelopes(Voice& v) noexcept
{
    const float spread = 0.08F * static_cast<float>(m_d.dispersion) * v.releaseOffset; // each note's tail its own
    for (std::size_t k = 0; k < kLayers; ++k) {
        const LayerDerived& ld = m_d.layer.at(k);
        v.layer.at(k).amp.set(ld.attack, 0.2, 1.0, ld.release * (1.0 + spread), m_voiceRate);
    }
}

void Synth::process(std::span<const MidiEvent> events, const AudioBlock& io) noexcept
{
    applyParams();
    if (m_releaseAll.exchange(false, std::memory_order_acquire)) releaseAll(false);
    const auto total = static_cast<std::size_t>(std::max(io.frames, 0));
    const std::span<float> left(io.left, total);
    const std::span<float> right(io.right, total);
    std::ranges::fill(left, 0.0F);
    std::ranges::fill(right, 0.0F);
    std::size_t done = 0;
    for (const MidiEvent& event : events) {
        if (event.controlsOnly) continue;
        const auto where = std::clamp(static_cast<std::size_t>(std::max(event.sampleOffset, 0)), done, total);
        if (where > done) render(left.subspan(done, where - done), right.subspan(done, where - done));
        done = where;
        handle(event);
    }
    if (total > done) render(left.subspan(done, total - done), right.subspan(done, total - done));
}

void Synth::handle(const MidiEvent& event) noexcept
{
    const int kind = event.status & 0xF0;
    if (kind == 0x90 && event.data2 > 0) {
        noteOn(event.data1 & 0x7F, event.data2);
    } else if (kind == 0x80 || kind == 0x90) {
        noteOff(event.data1 & 0x7F);
    } else if (kind == 0xB0) {
        switch (event.data1) {
        case 64: setSustain(event.data2 >= 64); break;
        case 120: releaseAll(true); break; // all sound off
        case 121: setSustain(false); break;
        case 123: releaseAll(false); break; // all notes off
        default: break;
        }
    }
}

void Synth::noteOn(int note, int velocity) noexcept
{
    if (m_d.type == nullptr) return;
    const float vel = 1.0F - (m_d.velocity * (1.0F - (static_cast<float>(velocity) / 127.0F)));
    // The same note again: its voice plays it again from where it is (no click).
    auto same = std::ranges::find_if(m_voices, [&](const Voice& v) { return v.note == note && v.active() && !v.stolen; });
    Voice* target = same != m_voices.end() ? &*same : nullptr;
    if (target == nullptr) {
        const auto sounding = [](const Voice& v) { return v.active() && !v.stolen; };
        if (std::ranges::count_if(m_voices, sounding) >= kMaxNotes) {
            // Too many: the oldest (one let go first) fades out fast.
            Voice* oldest = nullptr;
            for (Voice& v : m_voices) {
                if (!sounding(v)) continue;
                const bool better = oldest == nullptr || (oldest->keyDown && !v.keyDown) || (oldest->keyDown == v.keyDown && v.order < oldest->order);
                if (better) oldest = &v;
            }
            if (oldest != nullptr) {
                oldest->stolen = true;
                for (LayerVoice& lv : oldest->layer) lv.amp.fastRelease(m_voiceRate);
            }
        }
        const auto free = std::ranges::find_if(m_voices, [](const Voice& v) { return !v.active(); });
        if (free == m_voices.end()) return; // every slot fading out: this note waits for none
        target = &*free;
        startVoice(*target, note);
    } else if (m_d.engine != Engine::Classic) {
        // (Its engine let go when the key did: started again, or the note
        // would stay silent until its old tail had died away.)
        target->engine.retrigger();
    }
    target->velocity = vel;
    target->keyDown = true;
    target->sustained = false;
    target->order = ++m_order;
    for (LayerVoice& lv : target->layer) lv.amp.gateOn();
}

void Synth::startVoice(Voice& v, int note) noexcept
{
    const PadType& t = *m_d.type;
    // (Its engine's strings and bores kept: moved, never allocated again here.)
    EngineVoice engine = std::move(v.engine);
    v = Voice{};
    v.engine = std::move(engine);
    v.note = note;
    if (m_d.engine != Engine::Classic) {
        v.engine.start(m_d.engine, t.patch, engineContext(v), m_random);
        setEnvelopes(v);
        v.controlCountdown = 0;
        return;
    }
    // Its own small differences, as a polysynth's voice cards have.
    v.cutoffOffset = m_random.bipolar();
    v.driveOffset = m_random.bipolar();
    v.levelOffset = m_random.bipolar();
    v.panOffset = m_random.bipolar();
    v.widthOffset = m_random.bipolar();
    v.releaseOffset = m_random.bipolar();
    v.pitchOffset = m_random.bipolar();
    v.pwmRate = 1.0F + 0.15F * m_random.bipolar();
    const auto tick = static_cast<float>(kControl / m_voiceRate);
    for (std::size_t k = 0; k < kLayers; ++k) {
        const LayerType& lt = k == 0 ? t.a : t.b;
        LayerVoice& lv = v.layer.at(k);
        const int copies = std::clamp(lt.unison, 1, kUnison);
        // The first copy on the note, in the layer's place; the others
        // quieter around it, the second and third a pair the same distance
        // above and below (not an even grid, yet in tune on average), a
        // fourth its own way; each of them upside down in one ear or the
        // other (`place` ±1, a little of its own; see control()).
        for (int i = 0; i < copies; ++i) {
            Osc& o = lv.osc.at(static_cast<std::size_t>(i));
            o.phase = m_random.unit();
            o.partner = i > 0;
            const float side = m_random.unit() < 0.5F ? -1.0F : 1.0F;
            const float own = 0.8F + 0.2F * m_random.unit();
            if (i == 0) {
                o.detune = 0.0F;
                o.place = 0.0F;
            } else if (i == 2) {
                o.detune = -lv.osc.at(1).detune;
                o.place = -lv.osc.at(1).place / std::abs(lv.osc.at(1).place) * own;
            } else {
                o.detune = side * static_cast<float>(lt.detuneCents) * (0.4F + 0.6F * m_random.unit());
                o.place = side * own;
            }
            o.drift = m_random.bipolar() * static_cast<float>(t.driftCents);
            o.driftTarget = o.drift;
            o.driftCountdown = static_cast<int>((0.3F + 0.9F * m_random.unit()) / tick);
        }
        lv.subPhase = m_random.unit();
        lv.pwmPhase = m_random.unit();
        if (const SpectralTables* tables = m_d.layer.at(k).tables) {
            // Its loop, from a place of its own; the other ear half a loop on (wide, never alike).
            lv.table = tables->pick(sawstack::midiHz(note) * std::exp2(static_cast<double>(lt.octave)));
            lv.readLeft = static_cast<double>(m_random.unit()) * SpectralTables::kLength;
            lv.readRight = std::fmod(lv.readLeft + SpectralTables::kLength / 2.0, static_cast<double>(SpectralTables::kLength));
        }
    }
    // The second layer's first copy starts where the first's is, an octave
    // or two on: as they wander together, the harmonics they share keep one
    // relation, the same on every note (from random places, each note's
    // shared harmonics added up differently and for good: notes differed by
    // chance, not by Dispersion).
    const double octaves = std::exp2(static_cast<double>(t.b.octave - t.a.octave));
    const double startB = static_cast<double>(v.layer.at(0).osc.at(0).phase) * octaves;
    v.layer.at(1).osc.at(0).phase = static_cast<float>(startB - std::floor(startB));
    v.randomNow = m_random.bipolar();
    v.randomTarget = v.randomNow;
    v.vibratoPhase = m_random.unit();
    v.lfoPhase = static_cast<float>(m_lfoPhase);
    v.stepPhase = static_cast<float>(m_stepPhase);
    v.strikeCountdown = static_cast<int>((0.05F + 0.35F * m_random.unit()) / tick);
    const double hz = sawstack::midiHz(note);
    if (t.texture != Texture::Air) {
        const double textureHz = hz * std::exp2(t.textureOctave);
        v.bellsL.tune(modeSet(t.texture), textureHz, t.textureDecay, m_voiceRate);
        v.bellsR.tune(modeSet(t.texture), textureHz, t.textureDecay, m_voiceRate);
    }
    v.airL.set(kAirHz, 1.0, m_voiceRate);
    v.airR.set(kAirHz, 1.0, m_voiceRate);
    if (t.choir) {
        for (std::size_t f = 0; f < 3; ++f) {
            v.vowelL.at(f).set(kVowelHz.at(f), kVowelQ.at(f), m_voiceRate);
            v.vowelR.at(f).set(kVowelHz.at(f), kVowelQ.at(f), m_voiceRate);
        }
    }
    setEnvelopes(v);
    v.controlCountdown = 0; // (worked out before its first sample)
}

void Synth::noteOff(int note) noexcept
{
    for (Voice& v : m_voices) {
        if (v.note != note || !v.keyDown || v.stolen) continue;
        v.keyDown = false;
        if (m_sustain) {
            v.sustained = true;
        } else {
            for (LayerVoice& lv : v.layer) lv.amp.gateOff();
        }
    }
}

void Synth::setSustain(bool down) noexcept
{
    m_sustain = down;
    if (down) return;
    for (Voice& v : m_voices) {
        if (v.sustained) {
            v.sustained = false;
            for (LayerVoice& lv : v.layer) lv.amp.gateOff();
        }
    }
}

void Synth::releaseAll(bool fast) noexcept
{
    m_sustain = false;
    for (Voice& v : m_voices) {
        if (!v.active()) continue;
        v.keyDown = false;
        v.sustained = false;
        for (LayerVoice& lv : v.layer) {
            if (fast) lv.amp.fastRelease(m_voiceRate);
            else lv.amp.gateOff();
        }
    }
}

bool Synth::holdsNotes() const noexcept
{
    return std::ranges::any_of(m_voices, [](const Voice& v) { return v.active(); });
}

int Synth::soundingNotes() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_voices, [](const Voice& v) { return v.active(); }));
}

// A few times a millisecond: the voice's pitches (its own drifts, the late
// vibrato), its pulse widths, its filter (the pad's cutoff, the breath, its
// own random movement, the bloom) and its texture's strikes.
void Synth::control(Voice& v) noexcept
{
    const PadType& t = *m_d.type;
    const auto tick = static_cast<float>(kControl / m_voiceRate);
    const auto motion = static_cast<float>(m_d.motion);
    const auto disp = static_cast<float>(m_d.dispersion);
    v.age += tick;

    // Its smooth random movement: a new place to glide to every two seconds or so.
    if (--v.randomCountdown <= 0) {
        v.randomTarget = m_random.bipolar();
        v.randomCountdown = static_cast<int>((1.2F + 1.8F * m_random.unit()) / tick);
    }
    v.randomNow += (v.randomTarget - v.randomNow) * m_randomGlide;

    v.vibratoPhase += static_cast<float>(t.vibratoHz) * tick;
    if (v.vibratoPhase >= 1.0F) v.vibratoPhase -= 1.0F;
    const float vibratoIn = std::clamp((v.age - static_cast<float>(t.vibratoDelay)) / 1.5F, 0.0F, 1.0F);
    const float vibrato = static_cast<float>(m_d.vibratoCents) * vibratoIn * std::sin(kTwoPi * v.vibratoPhase);
    const float pitch = 0.5F * disp * v.pitchOffset + vibrato; // (±half a cent of its own: in tune)

    v.gain = std::pow(10.0F, disp * v.levelOffset / 20.0F); // ±1 dB
    v.pan = 0.1F * disp * v.panOffset;
    const double hz = sawstack::midiHz(v.note);
    v.organStep = static_cast<float>(hz * std::exp2(static_cast<double>(pitch) / 1200.0) / m_voiceRate);
    for (std::size_t k = 0; k < kLayers; ++k) {
        const LayerDerived& ld = m_d.layer.at(k);
        if (!ld.on) continue;
        const LayerType& lt = k == 0 ? t.a : t.b;
        LayerVoice& lv = v.layer.at(k);
        const double layerHz = hz * std::exp2(static_cast<double>(lt.octave));
        for (int i = 0; i < ld.unison; ++i) {
            Osc& o = lv.osc.at(static_cast<std::size_t>(i));
            if (--o.driftCountdown <= 0) {
                o.driftTarget = m_random.bipolar() * static_cast<float>(t.driftCents);
                o.driftCountdown = static_cast<int>((0.5F + 1.0F * m_random.unit()) / tick);
            }
            o.drift += (o.driftTarget - o.drift) * m_driftGlide;
            // Every note's first copies wander together, as a whole analog
            // synth's tuning drifts with its warmth: the chord's octaves and
            // fifths stay locked (apart, the harmonics they share beat
            // slowly and deeply against each other: measured, a held chord
            // swung 4 dB). The quieter copies around them wander on their own.
            const float drift = i == 0 ? m_drift : o.drift;
            const auto cents = static_cast<double>(o.detune * m_d.detuneScale + drift * motion + pitch);
            o.step = static_cast<float>(layerHz * std::exp2(cents / 1200.0) / m_voiceRate);
            if (o.partner) {
                // The copies around the first: in both ears, one of them
                // upside down (as the Juno's chorus is). Each ear hears them
                // beat against the first, one ear's beat at its loudest
                // where the other's is quietest: the pad moves across the
                // room and never pumps (measured, a held chord swung 3-4 dB
                // with them to one side). In mono they fold away.
                const float side = ld.norm * ld.partner * o.place * static_cast<float>(std::min(m_d.width, 1.5));
                o.left = side;
                o.right = -side;
            } else {
                const float place = std::clamp(static_cast<float>(lt.pan) * static_cast<float>(m_d.width) + v.pan, -1.0F, 1.0F);
                const float angle = (place + 1.0F) * std::numbers::pi_v<float> / 4.0F;
                const float gain = ld.norm * std::numbers::sqrt2_v<float>;
                o.left = gain * std::cos(angle);
                o.right = gain * std::sin(angle);
            }
        }
        lv.subStep = static_cast<float>(layerHz * 0.5 * std::exp2(static_cast<double>(pitch) / 1200.0) / m_voiceRate);
        if (const SpectralTables* tables = ld.tables) {
            // The loop at this note: its own note's speed, the synth's wander and the voice's own, at the voices' rate.
            const double hzNow = layerHz * std::exp2(static_cast<double>(m_drift * motion + pitch) / 1200.0);
            lv.tableStep = hzNow / tables->baseHz.at(static_cast<std::size_t>(lv.table)) * (tables->sampleRate / m_voiceRate);
            const float place = std::clamp(static_cast<float>(lt.pan) + v.pan, -1.0F, 1.0F);
            const float angle = (place + 1.0F) * std::numbers::pi_v<float> / 4.0F;
            lv.spectralLeft = std::cos(angle) * std::numbers::sqrt2_v<float>;
            lv.spectralRight = std::sin(angle) * std::numbers::sqrt2_v<float>;
        }
        lv.pwmPhase += static_cast<float>(lt.pwmHz) * v.pwmRate * tick;
        if (lv.pwmPhase >= 1.0F) lv.pwmPhase -= 1.0F;
        const auto moving = static_cast<float>((lt.pwmDepth * std::sin(kTwoPi * static_cast<double>(lv.pwmPhase))) + (0.04 * static_cast<double>(v.randomNow)));
        lv.width = std::clamp(static_cast<float>(lt.pulseWidth) + 0.05F * disp * v.widthOffset + moving * motion, 0.05F, 0.95F);
        // A pulse's power goes with width x (1 - width): narrowing, it fades
        // (at 90 % by 4 dB: measured, a held chord swung 4 dB). Kept even,
        // the width moves the tone alone.
        lv.pulseLevel = 0.5F / std::sqrt(lv.width * (1.0F - lv.width));
    }

    // The filter. (The breath on the note's own phase, started from the
    // synth's: every note together, and smooth at any speed.)
    const float swell = v.layer.at(0).amp.level;
    v.lfoPhase += static_cast<float>(t.breathHz) * tick;
    if (v.lfoPhase >= 1.0F) v.lfoPhase -= 1.0F;
    const double breath = std::sin(kTwoPi * v.lfoPhase) * m_d.breathOctaves * m_d.motion;
    double octaves = t.keyTrack * (v.note - 60) / 12.0 + breath + t.randomOctaves * m_d.motion * v.randomNow
                     - t.bloomOctaves * (1.0 - swell);
    // Its own shape over time: rising smoothly, then settling back.
    if (m_d.sweepOctaves > 0.0) {
        const double rise = std::max(t.sweepRise, 0.01);
        const double age = v.age;
        double shape = 0.0;
        if (age < rise) {
            const double x = age / rise;
            shape = x * x * (3.0 - 2.0 * x);
        } else {
            shape = t.sweepSustain + (1.0 - t.sweepSustain) * std::exp(-(age - rise) / std::max(t.sweepFall, 0.01));
        }
        octaves += m_d.sweepOctaves * shape;
    }
    // The pulse: each step opens the filter (and the level) and falls away
    // (4 ms in: never a click).
    v.stepGain = 1.0F;
    if (t.stepHz > 0.0) {
        v.stepPhase += static_cast<float>(t.stepHz) * tick;
        if (v.stepPhase >= 1.0F) v.stepPhase -= 1.0F;
        const double since = v.stepPhase / t.stepHz;
        const double step = std::min(since / 0.004, 1.0) * std::exp(-since / std::max(t.stepDecay, 0.01));
        octaves += t.stepOctaves * m_d.motion * step;
        v.stepGain = static_cast<float>(1.0 - t.stepDepth + t.stepDepth * step);
    }
    v.coeffs.set(m_d.cutoffHz * std::exp2(octaves) * (1.0 + 0.08 * disp * v.cutoffOffset), m_d.resonance, m_voiceRate);
    v.drive = m_d.drive * (1.0F + 0.1F * disp * v.driveOffset);

    // The texture: struck again now and then while the note is held, each strike its own size and place.
    if (t.texture != Texture::Air && m_d.textureGain > 0.0F && (v.keyDown || v.sustained) && --v.strikeCountdown <= 0) {
        const float size = 0.35F + 0.65F * m_random.unit();
        const float place = m_random.unit() * std::numbers::pi_v<float> * 0.5F;
        v.bellsL.strike(size * std::cos(place));
        v.bellsR.strike(size * std::sin(place));
        v.strikeCountdown = static_cast<int>((0.5F + m_random.unit()) / (static_cast<float>(t.strikeHz) * m_d.strikeScale * tick));
    }
    v.bellsL.settle();
    v.bellsR.settle();
}

EngineContext Synth::engineContext(const Voice& v) const noexcept
{
    EngineContext c;
    c.rate = m_voiceRate;
    c.hz = sawstack::midiHz(v.note);
    c.motion = m_d.motion;
    c.brightness = m_d.brightness;
    c.character = m_d.character;
    c.held = v.keyDown || v.sustained;
    const auto index = static_cast<std::size_t>(std::max(m_type, 0));
    c.waves = index < m_wavesFor.size() ? m_wavesFor.at(index).get() : nullptr;
    c.spectral = index < m_engineSpectralFor.size() ? m_engineSpectralFor.at(index) : nullptr;
    c.samples = index < m_samplesFor.size() ? m_samplesFor.at(index).get() : nullptr;
    return c;
}

Engine Synth::engineFor(std::size_t index) const noexcept
{
    const PadType& t = kTypes.at(index);
    const bool missing = t.engine == Engine::Sampled && (index >= m_samplesFor.size() || m_samplesFor.at(index) == nullptr);
    return missing ? t.patch.sampled.fallback : t.engine;
}

// An engine's note: the engine makes it, the swell and tail shape it.
void Synth::renderEngineVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept
{
    const PadType& t = *m_d.type;
    const std::span<float> l = std::span<float>(m_engineLeft).first(left.size());
    const std::span<float> r = std::span<float>(m_engineRight).first(right.size());
    v.engine.render(m_d.engine, t.patch, engineContext(v), m_random, l, r);
    const float gain = m_d.layer.at(0).gain * v.velocity;
    auto fromL = l.begin();
    auto fromR = r.begin();
    auto toR = right.begin();
    for (float& outL : left) {
        const float env = v.layer.at(0).amp.tick();
        (void)v.layer.at(1).amp.tick(); // (the unused layer's envelope runs out with the note)
        outL += *fromL++ * env * gain;
        *toR++ += *fromR++ * env * gain;
    }
    if (!v.active()) {
        v.note = -1;
        v.stolen = false;
    }
}

void Synth::renderVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept
{
    const PadType& t = *m_d.type;
    if (m_d.engine != Engine::Classic) {
        renderEngineVoice(v, left, right);
        return;
    }
    const bool ladder = t.filter == Filter::Ladder;
    const bool organ = std::ranges::any_of(m_d.drawbarGain, [](float g) { return g > 0.0F; });
    auto outRight = right.begin();
    for (float& outLeft : left) {
        if (--v.controlCountdown <= 0) {
            v.controlCountdown = kControl;
            control(v);
        }
        float l = 0.0F;
        float r = 0.0F;
        for (std::size_t k = 0; k < kLayers; ++k) {
            LayerVoice& lv = v.layer.at(k);
            const LayerDerived& ld = m_d.layer.at(k);
            const float env = lv.amp.tick();
            if (!ld.on || env <= 0.0F) continue;
            float ll = 0.0F;
            float rr = 0.0F;
            const float width = lv.width;
            const float pulse = ld.pulse * lv.pulseLevel;
            if (const SpectralTables* tables = ld.tables) {
                // The spectral body: each ear its own place in the loop.
                const std::vector<float>& loop = tables->loops.at(static_cast<std::size_t>(lv.table));
                // (The right ear: the left's, with as much of its own as the layer's stereo.)
                const float own = spectralRead(loop, lv.readLeft);
                const float other = spectralRead(loop, lv.readRight);
                ll += own * lv.spectralLeft;
                rr += (own + ((other - own) * ld.stereo)) * ld.stereoNorm * lv.spectralRight;
                lv.readLeft += lv.tableStep;
                lv.readRight += lv.tableStep;
                if (lv.readLeft >= SpectralTables::kLength) lv.readLeft -= SpectralTables::kLength;
                if (lv.readRight >= SpectralTables::kLength) lv.readRight -= SpectralTables::kLength;
            }
            for (int i = 0; i < (ld.tables == nullptr ? ld.unison : 0); ++i) {
                Osc& o = lv.osc.at(static_cast<std::size_t>(i));
                const float p = o.phase;
                const float dt = o.step;
                const float saw = 2.0F * p - 1.0F - sawstack::polyBlep(p, dt);
                float x = saw * ld.saw;
                if (pulse > 0.0F) {
                    // A pulse: the saw minus the same saw shifted by the width (both corrected).
                    float q = p + width;
                    if (q >= 1.0F) q -= 1.0F;
                    x += (saw - (2.0F * q - 1.0F - sawstack::polyBlep(q, dt))) * 0.5F * pulse;
                }
                if (ld.triangle > 0.0F) x += (4.0F * std::abs(p - 0.5F) - 1.0F) * ld.triangle;
                float next = p + dt;
                if (next >= 1.0F) next -= 1.0F;
                o.phase = next;
                ll += x * o.left;
                rr += x * o.right;
            }
            if (ld.sub > 0.0F) {
                const float s = std::sin(kTwoPi * lv.subPhase) * ld.sub;
                lv.subPhase += lv.subStep;
                if (lv.subPhase >= 1.0F) lv.subPhase -= 1.0F;
                ll += s;
                rr += s;
            }
            if (ld.noise > 0.0F) {
                ll += m_random.bipolar() * ld.noise * kNoiseAtVoiceRate;
                rr += m_random.bipolar() * ld.noise * kNoiseAtVoiceRate;
            }
            const float g = env * ld.gain;
            l += ll * g;
            r += rr * g;
        }
        const float swell = v.layer.at(0).amp.level;
        if (organ) {
            const float step = v.organStep;
            float centre = 0.0F;
            for (std::size_t b = 0; b < kDrawbars; ++b) {
                if (m_d.drawbarGain.at(b) <= 0.0F) continue;
                float& phase = v.organPhase.at(b);
                centre += std::sin(kTwoPi * phase) * m_d.drawbarGain.at(b);
                phase += step * static_cast<float>(kDrawbarRatios.at(b));
                if (phase >= 1.0F) phase -= 1.0F;
            }
            l += centre * swell;
            r += centre * swell;
        }

        // The filter, driven.
        if (ladder) {
            l = v.ladderL.tick(l, v.coeffs, v.drive);
            r = v.ladderR.tick(r, v.coeffs, v.drive);
        } else {
            l = v.softL.tick(l, v.coeffs, v.drive);
            r = v.softR.tick(r, v.coeffs, v.drive);
        }
        if (t.choir) {
            // The vowel: three resonances in parallel (a band-pass is what the low- and high-passes leave).
            float vl = 0.0F;
            float vr = 0.0F;
            for (std::size_t f = 0; f < 3; ++f) {
                float low = 0.0F;
                float high = 0.0F;
                v.vowelL.at(f).tick(l, low, high);
                vl += (l - low - high) / v.vowelL.at(f).k * kVowelGain.at(f);
                v.vowelR.at(f).tick(r, low, high);
                vr += (r - low - high) / v.vowelR.at(f).k * kVowelGain.at(f);
            }
            // (With some of the voice between its resonances, as a sung "aah"
            // has: resonances alone made whichever two notes' harmonics beat
            // near one the whole sound, measured 4 dB on a held fifth.)
            l = kVowelBody * l + vl * kVowelLevel;
            r = kVowelBody * r + vr * kVowelLevel;
        }

        // The texture over it (not through the filter: it stays bright), swelling with the pad.
        if (m_d.textureGain > 0.0F) {
            float tl = 0.0F;
            float tr = 0.0F;
            if (t.texture == Texture::Air) {
                float low = 0.0F;
                float high = 0.0F;
                const float nl = m_random.bipolar() * kNoiseAtVoiceRate;
                v.airL.tick(nl, low, high);
                tl = (nl - low - high) * kAirLevel;
                const float nr = m_random.bipolar() * kNoiseAtVoiceRate;
                v.airR.tick(nr, low, high);
                tr = (nr - low - high) * kAirLevel;
            } else {
                tl = v.bellsL.tick() * kBellLevel;
                tr = v.bellsR.tick() * kBellLevel;
            }
            l += tl * m_d.textureGain * swell;
            r += tr * m_d.textureGain * swell;
        }
        const float level = v.gain * v.velocity * v.stepGain;
        outLeft += l * level;
        *outRight++ += r * level;
        if (!v.active()) break;
    }
    if (!v.active()) {
        v.note = -1;
        v.stolen = false;
    }
}

void Synth::render(std::span<float> left, std::span<float> right) noexcept
{
    if (m_d.type == nullptr) return;
    // In pieces as long as the voices' room holds.
    for (std::size_t done = 0; done < left.size();) {
        const std::size_t n = std::min(left.size() - done, static_cast<std::size_t>(m_maxBlock));
        renderChunk(left.subspan(done, n), right.subspan(done, n));
        done += n;
    }
}

void Synth::renderChunk(std::span<float> left, std::span<float> right) noexcept
{
    // The voices at four times the rate, the tape over the whole chord there
    // too, then back down to the rate (what is above our hearing taken away).
    const std::size_t voiceFrames = left.size() * kOversample;
    const std::span<float> voiceLeft = std::span<float>(m_voiceLeft).first(voiceFrames);
    const std::span<float> voiceRight = std::span<float>(m_voiceRight).first(voiceFrames);
    std::ranges::fill(voiceLeft, 0.0F);
    std::ranges::fill(voiceRight, 0.0F);
    for (Voice& v : m_voices) {
        if (v.active()) renderVoice(v, voiceLeft, voiceRight);
    }
    auto tapeRight = voiceRight.begin();
    for (float& sample : voiceLeft) m_tape.tick(sample, *tapeRight++, m_random);
    constexpr auto kFour = static_cast<std::size_t>(kOversample);
    std::size_t from = 0;
    auto downRight = right.begin();
    for (float& downLeft : left) {
        downLeft = m_downLeft.tick(voiceLeft.subspan(from, kFour).data());
        *downRight++ = m_downRight.tick(voiceRight.subspan(from, kFour).data());
        from += kFour;
    }
    m_lfoPhase += static_cast<double>(left.size()) * m_d.type->breathHz / m_rate;
    if (m_lfoPhase >= 1.0) m_lfoPhase -= std::floor(m_lfoPhase);
    m_stepPhase += static_cast<double>(left.size()) * m_d.type->stepHz / m_rate;
    if (m_stepPhase >= 1.0) m_stepPhase -= std::floor(m_stepPhase);
    // The whole synth's slow wander (every note's first copies follow it).
    m_driftCountdown -= static_cast<int>(left.size());
    if (m_driftCountdown <= 0) {
        m_driftTarget = m_random.bipolar() * static_cast<float>(m_d.type->driftCents);
        m_driftCountdown = static_cast<int>((0.5F + m_random.unit()) * static_cast<float>(m_rate));
    }
    m_drift += (m_driftTarget - m_drift) * (1.0F - std::exp(-static_cast<float>(left.size()) / (0.35F * static_cast<float>(m_rate))));
    const float target = m_d.masterGain;
    const float smooth = 1.0F - std::exp(-1.0F / (0.02F * static_cast<float>(m_rate)));
    auto outRight = right.begin();
    constexpr double kMoveHz = 0.15; // Movement's sweep: down and up every seven seconds or so
    for (float& outLeft : left) {
        float l = outLeft;
        float r = *outRight;
        if (m_toneHz > 0.0) {
            if (m_toneCountdown-- <= 0) {
                m_toneCountdown = 31;
                const double sweep = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * m_movePhase);
                const double hz = m_toneHz * std::exp2(-m_moveOctaves * sweep);
                m_toneLowL.set(hz, 0.7071, m_rate);
                m_toneLowR.set(hz, 0.7071, m_rate);
            }
            l = m_toneLowL.lowPass(l);
            r = m_toneLowR.lowPass(r);
        }
        m_movePhase += kMoveHz / m_rate;
        if (m_movePhase >= 1.0) m_movePhase -= 1.0;
        if (m_shelfOn) {
            l = m_toneShelf.tick(l, 0);
            r = m_toneShelf.tick(r, 1);
        }
        if (m_timbreOn) {
            l = m_timbrePresence.tick(m_timbreLow.tick(l, 0), 0);
            r = m_timbrePresence.tick(m_timbreLow.tick(r, 1), 1);
            if (m_timbreDrive > 1.0F) {
                const float back = 1.0F / std::tanh(m_timbreDrive * 0.25F) * 0.25F; // (a quarter-scale signal comes back at its level)
                l = std::tanh(l * m_timbreDrive) * back;
                r = std::tanh(r * m_timbreDrive) * back;
            }
        }
        m_lofi.tick(l, r, m_random);
        l = m_lowCutL.highPass(l);
        r = m_lowCutR.highPass(r);
        m_chorus.tick(l, r, m_random);
        if (m_ensembleMix > 0.0F) {
            // A string machine's ensemble: three moving delays, a slow and a
            // fast sweep, 120° apart; added to one ear and taken from the
            // other, as the chorus is (one ear's dips the other's peaks: it
            // swirls, never pumps).
            m_ensemblePhase += 1.0 / m_rate;
            m_ensembleL.push(0.5F * (l + r));
            float swirl = 0.0F;
            for (int k = 0; k < 3; ++k) {
                const double third = k / 3.0;
                const double slow = std::sin(2.0 * std::numbers::pi * (0.6 * m_ensemblePhase + third));
                const double fast = std::sin(2.0 * std::numbers::pi * (6.0 * m_ensemblePhase + third));
                swirl += m_ensembleL.read(static_cast<float>((0.005 + 0.0015 * slow + 0.0003 * fast) * m_rate));
            }
            swirl *= m_ensembleMix * 0.7F / 3.0F;
            l += swirl;
            r -= swirl;
        }
        m_rotary.tick(l, r);
        m_phaser.tick(l, r);
        m_width.tick(l, r);
        m_echo.tick(l, r, 60.0);
        m_reverb.tick(l, r);
        // The lows in the middle (measured, the references' 0.8-1.0 alike
        // between the ears below 250 Hz): what differs between the ears
        // keeps only what is above 150 Hz.
        const float mid = 0.5F * (l + r);
        const float side = m_sideLow.highPass(0.5F * (l - r));
        l = mid + side;
        r = mid - side;
        l = m_air.tick(l, 0);
        r = m_air.tick(r, 1);
        // The top cut steeply.
        l = m_topL2.lowPass(m_topL1.lowPass(l));
        r = m_topR2.lowPass(m_topR1.lowPass(r));
        m_gainNow += (target - m_gainNow) * smooth;
        // Soft at the very top: never a hard clip, however many notes.
        outLeft = softTop(l * m_gainNow);
        *outRight++ = softTop(r * m_gainNow);
    }
    if (m_ensemblePhase > 1000.0) m_ensemblePhase -= 1000.0;
}

} // namespace gigchain::engine::pads
