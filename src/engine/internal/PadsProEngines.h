#pragma once

#include "PadsProFx.h"
#include "PadsProSpectral.h"

#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Pads Pro's engines: each pad's sound made its own way from the first
// oscillator, as the instruments players love are (a piano is struck, a
// guitar plucked or driven, a violin bowed, an organ blown or spun, a Moog
// filtered, a DX7 modulated). Each written here from the published method
// it names (no other product's code, presets or samples). The voice
// management, the swell and tail and the effects stay Synth's (PadsPro.h).
//
// Audio thread: start() and render() never allocate, lock or log;
// allocate() and buildWaves() (main thread, at prepare) do.
namespace gigchain::engine::pads {

enum class Engine : uint8_t
{
    Classic,       // the layers of PadType (oscillators or spectral bodies)
    Piano,         // a struck piano's tone (stretched partials, the hammer's notch) that blooms and sustains
    Tine,          // an electric piano's tine and pickup (FM, after Chowning; the pickup's asymmetry)
    Ebow,          // a guitar string driven for ever (a waveguide, after Karplus-Strong and Smith, fed by its own pickup)
    Strum,         // plucked guitar strings struck again and again (Karplus-Strong, with Jaffe-Smith's tuning and decay)
    Bowed,         // a section of bowed strings (Smith's bowed-string waveguide, as McIntyre-Schumacher-Woodhouse) in a violin's body
    StringMachine, // a string machine: divided-down saws, its octaves locked (the 70s ensembles)
    Tonewheel,     // tonewheels and drawbars (sines at the footages), with the key click and percussion
    PipeOrgan,     // flue pipes: ranks of principals, the chiff of air at each start
    Harmonium,     // free reeds under a bellows: two ranks beating, the pressure breathing
    Mellotron,     // tape: a choir loop played back with the tape's wow, flutter and dull top
    Moog,          // three oscillators into a driven 4-pole ladder (Huovilainen's)
    Juno,          // one oscillator (pulse moving, saw, a sub square), a 4-pole, the BBD chorus after
    TwinAnalog,    // two whole synths side by side, each with a high- and a low-pass (the CS-80's two channels)
    Vector,        // four waves crossfaded along a looping joystick path (the Prophet-VS's vector synthesis)
    Fm,            // six operators (Chowning's FM, as the DX7 has them): each its ratio, level and envelope
    Supersaw,      // seven saws spread as Szabo measured the JP-8000's
    Flute,         // a blown pipe (Cook's flute: a jet and a bore, breath noise)
    Modal,         // resonant modes rubbed and struck (a singing bowl: modal synthesis)
    Granular,      // a cloud of short grains on the note's partials (Roads' granular synthesis)
    // Atmospheres (textures of air, never sound effects), each tuned to the note played:
    Wind,          // gusts of wind, howling softly on the note's harmonics
    Ocean,         // the swell of the sea, singing the note under it
    Forest,        // wind in leaves over a soft bed
    Cave,          // a deep, dark drone and rumble
    Sampled,       // recordings of the real instrument (samples/pads/, free recordings: tools/make-pad-samples.py)
};

// A recorded note: its sound, the note it was played at, where it loops (none: it rings out).
struct SampleZone
{
    int root = 60;
    double rate = 44100.0;
    std::vector<float> left, right;
    std::size_t loopStart = 0, loopEnd = 0; // (loopEnd 0: no loop)
};
// A set of recorded notes across the keyboard ("strings", "piano").
struct SampleSet
{
    std::vector<SampleZone> zones;
    [[nodiscard]] std::size_t pick(int note) const; // the zone recorded nearest the note
};
// Where the recordings are: GIGCHAIN_PAD_SAMPLES if set, else the sound
// pack's samples/pads beside the app (in a Mac app, its Resources).
[[nodiscard]] QString padSampleFolder();
// A set loaded from padSampleFolder() (once, then shared); null when it is missing or broken (the reason logged).
[[nodiscard]] std::shared_ptr<const SampleSet> loadSampleSet(const QString& name);

// ---- Each engine's settings (in PadType::patch; only its engine's are read).

struct PianoSettings
{
    double hammerBright = 1.5;   // octaves the tone opens at each strike, closing as the note settles
    double hammerSeconds = 1.2;  // how fast it settles
    double settle = 0.45;        // the level it settles to (the struck note's decay, held: a piano held as a pad)
    double bloomSeconds = 0.0;   // a swell after the strike (0: none)
    double lpHz = 3500.0;
};

struct TineSettings
{
    double index = 1.6;          // the tine's brightness at the strike
    double indexSustain = 0.35;  // ... held
    double indexSeconds = 1.5;
    double bell = 0.25;          // the high tine "ping" (ratio 14)
    double bark = 0.3;           // the pickup's asymmetry (grit when played hard)
    double tremHz = 4.5, tremDepth = 0.35; // the stereo tremolo (a suitcase's)
};

struct EbowSettings
{
    double drive = 0.8;          // how hard the string is driven (sustain and harmonics)
    double pickup = 0.18;        // where along the string the pickup is (0-0.5: lower, brighter)
    double swellSeconds = 1.5;   // the driver coming in
    double vibratoHz = 4.8, vibratoCents = 7.0;
    double toneHz = 3000.0;      // the amp's top
};

struct StrumSettings
{
    double strumHz = 2.5;        // strikes a second while held
    double jitter = 0.35;        // how loosely in time
    double decay = 3.0;          // seconds each pluck rings
    double brightness = 0.6;     // the pluck's brightness (0-1)
    double octave = 0.5;         // the 12-string's octave string's level
    double toneHz = 4500.0;
};

struct BowedSettings
{
    int players = 3;             // strings each note (1-3), each its own tuning and vibrato
    double pressure = 0.55;      // the bow's pressure (0-1: airy to gritty)
    double position = 0.127;     // where the bow is, from the bridge
    double vibratoHz = 5.2, vibratoCents = 9.0;
    double detuneCents = 5.0;    // the players apart
    double bodyGain = 0.6;       // the violin's body resonances against the string
};

struct StringMachineSettings
{
    double eight = 1.0, four = 0.6, sixteen = 0.0; // the footages
    double lpHz = 4500.0;
    double body = 0.5;           // the formant-like body peaks (about 550 and 1900 Hz)
};

struct TonewheelSettings
{
    std::array<double, 9> drawbars{8, 8, 8, 0, 0, 0, 0, 0, 0}; // 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' (0-8)
    double percussion = 0.0;     // the 4' or 2 2/3' struck at the start
    double click = 0.15;         // the key click
};

struct PipeOrganSettings
{
    std::array<double, 5> ranks{1.0, 0.6, 0.0, 0.35, 0.0}; // 16' 8' 4' 2 2/3' 2' against the 8' (index 1)
    double chiff = 0.4;          // the air at each start
    double wind = 0.03;          // the wind's noise held
    double tremulant = 0.0;      // the wind's shake (0-1)
};

struct HarmoniumSettings
{
    double celeste = 4.0;        // cents between its two reeds
    double bellowsHz = 0.35, bellows = 0.25; // the pressure breathing
    double lpHz = 2800.0;
};

struct MellotronSettings
{
    double wow = 6.0, wowHz = 0.6; // cents
    double flutter = 2.0;          // cents, fast
    double toneHz = 3200.0;        // the tape's dull top
    double hiss = 0.01;
};

struct MoogSettings
{
    std::array<double, 3> octave{0, 0, -1};
    std::array<double, 3> detuneCents{0, 6, -4};
    std::array<double, 3> level{1.0, 0.8, 0.6};
    std::array<int, 3> wave{0, 0, 1}; // 0 saw, 1 triangle, 2 square
    double cutoffHz = 900.0, resonance = 0.25, drive = 0.6;
    double envOctaves = 1.6, envAttack = 1.5, envDecay = 3.0, envSustain = 0.4;
    double keyTrack = 0.5;
};

struct JunoSettings
{
    double pulse = 0.8, saw = 0.3, sub = 0.45, noise = 0.0;
    double pulseWidth = 0.5, pwmDepth = 0.35, lfoHz = 0.55;
    double hpHz = 110.0, cutoffHz = 1500.0, resonance = 0.18, keyTrack = 0.5;
    double envOctaves = 1.0, envAttack = 1.2, envDecay = 3.5, envSustain = 0.5;
};

struct AnalogChannel
{
    double saw = 0.7, square = 0.0, sine = 0.0;
    double pulseWidth = 0.5, pwmDepth = 0.0, pwmHz = 0.3;
    double octave = 0.0, detuneCents = 0.0, pan = 0.0;
    double hpHz = 60.0, lpHz = 2000.0, resonance = 0.1;
    double envOctaves = 0.8, envAttack = 1.0, envDecay = 3.0, envSustain = 0.5;
    double level = 1.0;
};
struct TwinAnalogSettings
{
    AnalogChannel one{};
    AnalogChannel two{.saw = 0.0, .square = 0.6};
    double vibratoHz = 5.5, vibratoCents = 0.0;
};

enum class VectorWave : uint8_t
{
    Sine,
    Vox,
    Bright,
    Hollow,
    Glass,
    Reed,
    Breath,
};
struct VectorSettings
{
    std::array<VectorWave, 4> waves{VectorWave::Sine, VectorWave::Vox, VectorWave::Glass, VectorWave::Hollow};
    std::array<double, 4> pathX{0.1, 0.9, 0.8, 0.2}; // the joystick's points (0-1), visited in turn and round again
    std::array<double, 4> pathY{0.1, 0.2, 0.9, 0.8};
    double segmentSeconds = 3.0;
    double detuneCents = 5.0;
    double cutoffHz = 4000.0, resonance = 0.1;
};

struct FmOperator
{
    double ratio = 1.0, detuneHz = 0.0;
    double level = 0.0;    // a carrier's loudness, or a modulator's depth (cycles of its carrier's phase)
    double attack = 0.01, decay = 1.0, sustain = 1.0, release = 2.0;
    double feedback = 0.0; // into itself (0-1)
    int into = 0;          // 0: heard; n (1-6): modulates operator n (a lower one)
    double pan = 0.0;
};
struct FmSettings
{
    std::array<FmOperator, 6> ops{};
    double lfoHz = 0.2, lfoDepth = 0.0; // the modulators' depth breathing
};

struct SupersawSettings
{
    double detune = 0.35, mix = 0.6;
    double cutoffHz = 3000.0, resonance = 0.1;
    double envOctaves = 0.8, envAttack = 1.5, envDecay = 4.0, envSustain = 0.5;
};

struct FluteSettings
{
    double breath = 0.08;        // the noise in the breath
    double jet = 0.32;           // the jet's length against the bore's (the tone's colour)
    double vibratoHz = 5.0, vibratoDepth = 0.04;
    double pressure = 0.9;
};

struct ModalSettings
{
    std::array<double, 8> ratios{1.0, 2.71, 5.15, 8.26, 12.0, 16.4, 21.5, 27.2};
    std::array<double, 8> gains{1.0, 0.7, 0.5, 0.35, 0.25, 0.18, 0.12, 0.08};
    std::array<double, 8> decays{8.0, 6.0, 4.5, 3.5, 2.5, 2.0, 1.5, 1.0};
    double rub = 0.6;            // the rubbing while held
    double strikeHz = 0.25;      // strikes a second while held
};

struct GranularSettings
{
    double density = 35.0;       // grains a second
    double grainMs = 220.0, spreadMs = 150.0;
    std::array<double, 6> partials{1, 2, 3, 4, 6, 8};
    std::array<double, 6> weights{1.0, 0.6, 0.45, 0.3, 0.2, 0.12};
    double detuneCents = 10.0;
    double octaveUp = 0.15;      // the chance a grain is an octave higher
    double cutoffHz = 7000.0;
};

// An atmosphere's settings (Wind, Ocean, Forest, Cave): how much of the
// air and how much of the note, and how fast it moves.
struct PlaceSettings
{
    double place = 1.0;     // the air's own texture (wind, swell, leaves, rumble)
    double tone = 0.5;      // the note under it (a soft bed, or the howl or the sea's singing)
    double busy = 1.0;      // how fast it moves (gusts, swells)
    double cutoffHz = 3000.0;
};

// A sampled pad's settings.
struct SampledSettings
{
    const char* set = "strings";          // samples/pads/<set>.txt
    Engine fallback = Engine::Bowed;      // played when the recordings are not there
    double toneHz = 9000.0;               // its top (Brightness and Timbre move it)
    double bloomSeconds = 0.0;            // a slow swell after the start (0: none)
    double tuneCents = 0.0;               // the recordings' own tuning put right (measured: tst_pads_pro, everyPadIsInTune)
};

// Every engine's settings (a pad fills in its own engine's).
struct EnginePatch
{
    SampledSettings sampled{};
    PlaceSettings place{};
    PianoSettings piano{};
    TineSettings tine{};
    EbowSettings ebow{};
    StrumSettings strum{};
    BowedSettings bowed{};
    StringMachineSettings strings{};
    TonewheelSettings tonewheel{};
    PipeOrganSettings pipe{};
    HarmoniumSettings harmonium{};
    MellotronSettings mellotron{};
    MoogSettings moog{};
    JunoSettings juno{};
    TwinAnalogSettings twin{};
    VectorSettings vector{};
    FmSettings fm{};
    SupersawSettings supersaw{};
    FluteSettings flute{};
    ModalSettings modal{};
    GranularSettings granular{};
};

// Single-cycle waves (kWaveLength samples each) an engine reads: the
// vector's four. Built at prepare.
inline constexpr int kWaveLength = 2048;
struct WaveSet
{
    std::vector<std::vector<float>> waves;
};
[[nodiscard]] std::shared_ptr<const WaveSet> buildWaves(Engine engine, const EnginePatch& patch);
// The spectrum an engine plays its tone from (built as a spectral loop at
// prepare), and how wide its bands are; false: it needs none.
[[nodiscard]] bool engineSpectrum(Engine engine, Spectrum& spectrum, double& bandwidthCents);

// What a voice's engine is told each block.
struct EngineContext
{
    double rate = 192000.0;       // the voice's rate
    double hz = 261.63;           // its note
    double motion = 1.0;          // the Movement macro (0 still .. 1 as designed .. 2 twice)
    double brightness = 0.0;      // the Brightness macro, octaves
    double character = 0.0;       // the Timbre macro, -1 .. 1 (each engine its own meaning)
    bool held = true;             // the key (or the pedal) down
    const WaveSet* waves = nullptr;
    const SpectralTables* spectral = nullptr;
    const SampleSet* samples = nullptr;
};

// One note's engine: every engine's state (only its pad's is used).
class EngineVoice
{
public:
    // Main thread: its delay lines, long enough for the lowest note at `rate`.
    void allocate(double rate);
    void start(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random) noexcept;
    // The same note played again while it still rings: its envelopes,
    // strikes and swells start over from where the sound is (no click).
    void retrigger() noexcept;
    // Writes (not adds) the note's sound into `left` and `right`.
    void render(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random, std::span<float> left,
                std::span<float> right) noexcept;

private:
    static constexpr int kControl = 32;
    static constexpr int kGrains = 32;

    // A fractional delay line (a string's or a bore's), read between samples.
    struct Line
    {
        std::vector<float> data;
        std::size_t pos = 0;
        void clear();
        void push(float x);
        [[nodiscard]] float read(double delay) const;
    };
    struct Grain
    {
        bool on = false;
        double phase = 0.0, step = 0.0;
        double at = 0.0, length = 1.0;
        double gain = 0.0, left = 0.0, right = 0.0;
        // Its tone and its window, each a sine turned a step every sample
        // (a rotation: a few multiplies, not a sine worked out each time).
        double toneSin = 0.0, toneCos = 1.0, toneStepSin = 0.0, toneStepCos = 1.0;
        double windowSin = 0.0, windowCos = 1.0, windowStepSin = 0.0, windowStepCos = 1.0;
    };
    struct Bow
    {
        Line neck, bridge;
        double loss = 0.0, vibrato = 0.0, vibratoHz = 5.0, detune = 0.0;
    };

    void control(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random) noexcept;

    double m_time = 0.0;
    double m_dt = 1.0 / 192000.0;
    int m_countdown = 0;
    std::array<double, 12> m_phase{};
    std::array<double, 12> m_step{};
    std::array<double, 6> m_opOut{}, m_opPrev{};
    std::array<sawstack::Envelope, 6> m_env{};
    std::array<FilterCoeffs, 3> m_coeffs{};
    std::array<LadderFilter, 3> m_ladder{};
    std::array<Svf, 8> m_svf{};
    std::array<double, 16> m_ctl{};   // control values, each engine its own
    std::array<double, 8> m_state{};  // small filter and loop states
    // Strings, bores, combs.
    std::array<Line, 4> m_lines{};
    std::array<Bow, 3> m_bows{};
    // A recording: which, and where in it.
    std::size_t m_zone = 0;
    double m_samplePos = 0.0, m_sampleStep = 1.0;
    // A spectral tone: where each ear reads it.
    int m_table = 0;
    double m_readL = 0.0, m_readR = 0.0, m_readStep = 0.0;
    // Modes (double: they ring for seconds).
    std::array<double, 8> m_modeC1{}, m_modeC2{}, m_modeY1{}, m_modeY2{}, m_modeGain{};
    // Grains.
    std::array<Grain, kGrains> m_grains{};
    double m_nextGrain = 0.0;
    double m_nextStrike = 0.0;
};

// A rotary speaker (after Smith, Serafin, Abel and Berners' Leslie model:
// a horn above a crossover and a drum below, each turning, their sound
// moving in pitch and level as they face you and away).
class Rotary
{
public:
    void prepare(double sampleRate);
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }
    void tick(float& left, float& right) noexcept;

private:
    double m_rate = 48000.0;
    float m_mix = 0.0F;
    double m_horn = 0.0, m_drum = 0.0;
    Svf m_cross;
    DelayLine m_hornLine, m_drumLine;
};

} // namespace gigchain::engine::pads
