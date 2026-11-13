#pragma once

#include "INode.h"
#include "PadsProEngines.h"
#include "PadsProFx.h"
#include "PadsProSpectral.h"
#include "SawStackDsp.h"
#include "SawStackFx.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

// Pads Pro: the app's own instrument of pads, built the way the pads players
// love are built (the research behind it: a slow swell and a long tail, two
// layers an octave apart, in tune with the movement in the tone not the
// pitch, a clean filter warmed by drive, a texture of bells or metal or air
// floating over it, and a big soft space). Every note a little different
// from the next, as an analog polysynth's voices are. Played from the
// keyboard as any instrument, and held as a drone by Sustain Pads.
//
// Its sounds are ours, from generic synthesis practice and published
// acoustics; nothing is taken from another product's presets, code or
// samples. Settings are parameters (0-1, as plugins have) that any thread may
// set; the audio thread picks them up at the next block. process() runs on
// the audio thread and never allocates, locks or logs.
namespace gigchain::engine::pads {

using sawstack::Envelope;

inline constexpr int kMaxNotes = 16;  // notes sounding at once (more steal the oldest)
inline constexpr int kVoicePool = 20; // room for stolen notes to fade out
inline constexpr int kLayers = 2;     // A and B
inline constexpr int kUnison = 4;     // a layer's copies of its oscillators, at most
inline constexpr int kDrawbars = 4;   // the organ's: 16', 8', 4', 2 2/3'

// What players see and can learn to their keyboard's knobs: the face of an
// analog collection's sounds. Four macros, each moving several things at
// once in the way its pad was designed (PadType::macros; at 0.5, the pad as
// designed), then the effects and the master. Ids and saved keys stay where
// 0.9 had them for the knobs that kept their meaning (a learned knob or a
// saved song keeps working); the others took the freed ids.
enum class Param : uint8_t
{
    Type,     // which pad (kTypes)
    Tone,     // "Brightness": a macro
    Chorus,   // the chorus's mix (each pad sets where it starts)
    Motion,   // "Movement": a macro
    Phaser,   // the phaser's mix (likewise)
    Delay,    // the echo's level (likewise)
    Timbre,   // a macro
    Reverb,   // the reverb's level (likewise)
    Level,    // "Master" (Ranges::levelDb), 0 dB at two thirds
    Velocity, // "Touch": how much playing harder is louder (0: not at all, as pads sit steady)
    Time,     // a macro
    Count,
};
inline constexpr int kParamCount = static_cast<int>(Param::Count);
[[nodiscard]] constexpr uint32_t paramId(Param p) { return static_cast<uint32_t>(p); }

// What a parameter's 0-1 value means, and back.
struct Ranges
{
    static double seconds(double v) { return 0.005 * std::pow(3000.0, std::clamp(v, 0.0, 1.0)); } // 5 ms .. 15 s
    static double fromSeconds(double s) { return std::clamp(std::log(s / 0.005) / std::log(3000.0), 0.0, 1.0); }
    static double levelDb(double v) { return -24.0 + 36.0 * std::clamp(v, 0.0, 1.0); } // -24 .. +12; 2/3 is 0 dB
    static double fromLevelDb(double db) { return std::clamp((db + 24.0) / 36.0, 0.0, 1.0); }
    static int choice(double v, int count) { return std::clamp(static_cast<int>(std::lround(v * (count - 1))), 0, count - 1); }
    static double fromChoice(int i, int count) { return count > 1 ? double(i) / (count - 1) : 0.0; }
};

// What a macro moves. Each adds its `amount` at the knob's full turn either
// way from the middle (half at a quarter turn), in the units named.
enum class Target : uint8_t
{
    None,
    Cutoff,    // octaves, both layers' filter
    Air,       // dB, the shelf above 8 kHz
    Resonance, // 0-1, the filter's
    LayerB,    // dB, the second layer's level
    Pulse,     // dB, the pulses and triangles in both layers
    Bed,       // dB, both layers (the pad under its texture)
    Texture,   // 0-1, the bells', metal's or air's level
    Strike,    // octaves, how often the texture is struck again
    Attack,    // octaves, the swell's time
    Release,   // octaves, the tail's
    Echo,      // 0-1, added to the echo's level
    Shimmer,   // 0-1, the octave climbing in the reverb
    Motion,    // the movement (pulse width, breath, wander, past 1 a gentle vibrato): 1 as designed, 0 still, 2 twice
    Detune,    // octaves, how far the copies stray from the note
    Drive,     // 0-1, how hard the filter is pushed
    LoFi,      // 0-1, the tape (and the pad's crusher)
    Phaser,    // 0-1, added to the phaser's mix
    Breath,    // octaves, added to how far the filter breathes (as an LFO's depth)
    Chorus,    // 0-1, added to the chorus's mix
    Sweep,     // octaves, added to how far each note's own filter shape opens (PadType::sweepOctaves)
    Count,
};
struct MacroTarget
{
    Target target = Target::None;
    double amount = 0.0;
};
using Macro = std::array<MacroTarget, 4>;
// Unless a pad says otherwise: Brightness opens the filter (and the air
// with it); Timbre brings the second layer and the pulses up; Time
// stretches the swell and the tail together (a key sound to a slow pad);
// Movement is how much it moves (the filter breathing deeper, as an analog
// collection's Movement deepens its LFOs), and how far its copies stray.
inline constexpr Macro kBrightnessMacro{{{Target::Cutoff, 1.5}, {Target::Air, 3.0}}};
inline constexpr Macro kTimbreMacro{{{Target::LayerB, 9.0}, {Target::Pulse, 9.0}}};
inline constexpr Macro kTimeMacro{{{Target::Attack, 2.0}, {Target::Release, 1.5}}};
inline constexpr Macro kMovementMacro{{{Target::Motion, 1.0}, {Target::Breath, 0.25}, {Target::Detune, 1.0}}};
struct Macros
{
    Macro brightness = kBrightnessMacro;
    Macro timbre = kTimbreMacro;
    Macro time = kTimeMacro;
    Macro movement = kMovementMacro;
};

// One of a pad's layers. Levels in dB (at or below -60: off).
struct LayerType
{
    double levelDb = -60.0; // the layer as a whole (at or below -60: no layer)
    int octave = 0;         // from the note played
    double pan = 0.0;       // -1 left .. 1 right
    // Its oscillators, within the layer.
    double sawDb = -60.0;
    double pulseDb = -60.0;
    double triangleDb = -60.0;
    double subDb = -60.0;   // a sine an octave below the layer
    double noiseDb = -60.0; // breath
    double pulseWidth = 0.5, pwmDepth = 0.0, pwmHz = 0.3; // 0.5: a square; the width moving ± pwmDepth
    int unison = 1;           // copies of each oscillator (1-4): the first on the note, the rest around it (wide: Synth::control)
    double detuneCents = 0.0; // how far the others stray from the note, at most (each its own amount, in pairs: in tune on average)
    double partnerDb = -12.0; // the others' level against the first (quieter: thicker without pulsing)
    double attackScale = 1.0; // its swell and tail, against the pad's (B can come in after A)
    double releaseScale = 1.0;
    // A spectral body instead of the oscillators (PadsProSpectral.h): a
    // choir, strings, air or glass of countless voices, each ear reading
    // the loop from its own place (wide). `bandwidthCents`: how wide each
    // harmonic's band (a choir 40-60, strings 20-30).
    bool spectral = false;
    Spectrum spectrum = Spectrum::Choir;
    double bandwidthCents = 45.0;
    // How unlike the two ears are (0: the same, both centred; 1: each its
    // own place in the loop, as wide as can be). The pads players love sit
    // mostly together (measured, the user's references: the middle 0.5-0.9
    // alike between the ears).
    double stereo = 1.0;
};

enum class Filter : uint8_t
{
    Soft,   // 2-pole, as an OB's or a SEM's: soft and airy
    Ladder, // 4-pole, as a Prophet's or a Jupiter's: warm and round
};

// One pad: what makes its sound.
struct PadType
{
    const char* key = "";    // in saved settings (never changes once released)
    const char* name = "";   // as players see it
    const char* family = ""; // its group on the panel
    const char* about = "";
    LayerType a{}, b{};
    std::array<double, kDrawbars> drawbarsDb{-60.0, -60.0, -60.0, -60.0}; // organ sines at 0.5, 1, 2, 3 times
    bool choir = false;       // through the "aah" vowel filter
    // The filter (resonance 0: clean, no peak).
    Filter filter = Filter::Soft;
    double cutoffHz = 2500.0;
    double resonance = 0.0;
    double keyTrack = 0.3;       // how much higher notes open it (1: follows them fully)
    double breathHz = 0.08;      // its slow breathing, every note together
    double breathOctaves = 0.12;
    double randomOctaves = 0.1;  // each note's own smooth, never-repeating movement
    double bloomOctaves = 0.0;   // how far below its cutoff it starts, opening over the swell
    // Each note's own shape over time (what makes the references unlike
    // each other: one opens over seconds and closes again, one strikes
    // bright on every new note and fades): the filter opens by sweepOctaves
    // over sweepRise seconds, then falls back over sweepFall to sweepSustain
    // of it.
    double sweepOctaves = 0.0, sweepRise = 1.0, sweepFall = 4.0, sweepSustain = 0.0;
    // A pulse in time: stepHz steps a second (every note on the same
    // clock), each opening the filter by stepOctaves and the level by
    // stepDepth, falling away over stepDecay seconds.
    double stepHz = 0.0, stepOctaves = 0.0, stepDecay = 0.2, stepDepth = 0.0;
    // Life.
    double driftCents = 1.2;     // each oscillator's own slow wander (small: in tune)
    double vibratoCents = 0.0;   // comes in after vibratoDelay, gently
    double vibratoHz = 5.0;
    double vibratoDelay = 1.5;
    // The texture over the pad.
    Texture texture = Texture::Air;
    double textureOctave = 1.0;  // the struck partials, octaves above the note
    double strikeHz = 0.7;       // how often it is struck again while held (each strike its own place in stereo)
    double textureDecay = 1.0;   // how long it rings, against its partials' own
    double crush = 0.0;          // how much of Lo-fi is the crusher (else the tape alone)
    // The effects.
    double chorus = 0.35;        // the BBD chorus's mix
    double ensemble = 0.0;       // a string machine's ensemble (0-1)
    double phaser = 0.0;
    double echo = 0.15;          // its mix, with Space at the pad's own
    double echoSeconds = 0.45;
    double echoFeedback = 0.35;
    double reverbSeconds = 6.0;
    double reverbSize = 0.85;
    double reverbDampHz = 7000.0;
    double lowCutHz = 130.0;     // keeps the pad out of the bass's way, never muddy
    double airDb = 2.0;          // a soft shelf above 8 kHz
    // The top, cut steeply (24 dB an octave) above this: the references
    // fall away fast above 2 kHz (measured, 17-25 dB under our pads at 4-8
    // kHz: what made ours fizzy and cheap).
    double topHz = 4000.0;
    // Its design (what the macros move from).
    double attack = 1.5, release = 5.0; // seconds
    double width = 0.4;
    double reverb = 0.4;         // also where the Reverb knob starts (as `chorus`, `phaser` and `echo` are the others')
    double shimmer = 0.0;
    double textureLevel = 0.0;   // 0-1
    double dispersion = 0.5;     // 0-1
    double drive = 0.3;          // 0-1
    double lofi = 0.0;           // 0-1
    double levelDb = 0.0;        // the pad's own balance (all equally loud)
    Macros macros{};
    bool chorusDeep = false;     // the chorus's mode II (else I)
    // The pad's own engine (PadsProEngines.h): how its sound is made from
    // the first oscillator, each pad its own way (Classic: the layers above).
    // Its settings are `patch`'s for that engine; the effects above still
    // finish it.
    Engine engine = Engine::Classic;
    EnginePatch patch{};
    double rotary = 0.0;         // a rotary speaker over it (0-1: its mix)
};

[[nodiscard]] std::span<const PadType> padTypes();
[[nodiscard]] const PadType& padType(int index);
[[nodiscard]] int padTypeCount();
[[nodiscard]] double typeValue(int index);
[[nodiscard]] int typeIndex(double value);
// A parameter's name ("Brightness") and key in saved settings ("tone").
[[nodiscard]] QString paramName(uint32_t id);
[[nodiscard]] QString paramKey(uint32_t id);
// Every parameter as pad `index` starts it (its knobs where it sets them).
[[nodiscard]] std::array<float, kParamCount> typeParams(int index);
[[nodiscard]] std::array<float, kParamCount> defaultParams();

// An octave up, as a shimmer's: two read points of a delay line racing
// towards the present (twice as fast as time passes: twice the pitch), each
// faded in and out with a Hann window half a window apart, so one is always
// whole while the other jumps back.
class OctaveUp
{
public:
    void prepare(double sampleRate)
    {
        m_window = static_cast<float>(0.06 * sampleRate);
        m_line.allocate(static_cast<std::size_t>(m_window) * 2 + 16);
    }
    float tick(float in)
    {
        m_line.push(in);
        m_phase += 1.0F / m_window; // the read point's delay falls by one sample a sample
        if (m_phase >= 1.0F) m_phase -= 1.0F;
        float out = 0.0F;
        for (const float offset : {0.0F, 0.5F}) {
            float p = m_phase + offset;
            if (p >= 1.0F) p -= 1.0F;
            const float delay = (1.0F - p) * m_window + 2.0F;
            const float fade = std::sin(std::numbers::pi_v<float> * p);
            out += m_line.read(delay) * fade * fade;
        }
        return out;
    }

private:
    DelayLine m_line;
    float m_window = 2880.0F;
    float m_phase = 0.0F;
};

// The hall with the shimmer inside it: what comes out of the hall goes
// back in an octave higher (cleaned of its lows and softly limited, so it
// can never run away), so each time round the tail climbs: the shimmer of
// shimmer pads. Shimmer 0 is the plain hall. `mix` is how much of it is
// added under the pad.
class ShimmerReverb
{
public:
    void prepare(double sampleRate)
    {
        m_hall.prepare(sampleRate);
        m_upL.prepare(sampleRate);
        m_upR.prepare(sampleRate);
        m_loopHpL.set(400.0, 0.7071, sampleRate);
        m_loopHpR.set(400.0, 0.7071, sampleRate);
    }
    void set(const Hall::Settings& settings, double mix, double shimmer)
    {
        m_hall.set(settings);
        m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0));
        m_feedback = static_cast<float>(1.2 * std::clamp(shimmer, 0.0, 1.0)); // (with the hall's loss and the limiting: below 1 round the loop)
        m_shimmerOut = static_cast<float>(2.0 * std::clamp(shimmer, 0.0, 1.0));
    }
    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        float inL = left + m_backL * m_feedback;
        float inR = right + m_backR * m_feedback;
        m_hall.tick(inL, inR); // now the wet
        const float upL = m_upL.tick(m_loopHpL.highPass(inL));
        const float upR = m_upR.tick(m_loopHpR.highPass(inR));
        m_backL = std::tanh(upL);
        m_backR = std::tanh(upR);
        const float wetL = (inL + upL * m_shimmerOut) * kHallGain;
        const float wetR = (inR + upR * m_shimmerOut) * kHallGain;
        // As a send: the pad stays whole, the space under it (a held chord
        // under a wet as loud as itself beats against it, and in mono the
        // two can cancel). Once let go, the tail is all there is.
        left += wetL * m_mix;
        right += wetR * m_mix;
    }

private:
    static constexpr float kHallGain = 1.0F;
    Hall m_hall;
    OctaveUp m_upL, m_upR;
    Svf m_loopHpL, m_loopHpR;
    float m_backL = 0.0F, m_backR = 0.0F;
    float m_mix = 0.0F, m_feedback = 0.0F, m_shimmerOut = 0.0F;
};

class Synth
{
public:
    Synth();

    // Main thread, before process().
    void prepare(double sampleRate, int maxBlock);

    // Any thread.
    void setParam(uint32_t id, double value) noexcept;
    [[nodiscard]] double param(uint32_t id) const noexcept;
    void requestReleaseAll() noexcept { m_releaseAll.store(true, std::memory_order_release); }
    // For Sustain Pads (not knobs): shimmer added to the pad's own (0-1),
    // and the pad starting at once (its own fade is the swell).
    void setExtraShimmer(double amount) noexcept { m_extraShimmer.store(static_cast<float>(std::clamp(amount, 0.0, 1.0)), std::memory_order_relaxed); }
    void setQuickAttack(bool quick) noexcept { m_quickAttack.store(quick, std::memory_order_relaxed); }

    // Audio thread.
    void process(std::span<const MidiEvent> events, const AudioBlock& io) noexcept;
    [[nodiscard]] bool holdsNotes() const noexcept;
    [[nodiscard]] int soundingNotes() const noexcept;

private:
    // The voices run this many times faster than the rate (their drive and
    // filters' overtones above our hearing are taken away, not folded back:
    // measured, the off-note grit 20 dB lower), then come back down.
    static constexpr int kOversample = 4;
    static constexpr int kControl = 32; // a voice's samples (at kOversample times the rate) between its pitch, filter and movement updates

    struct Osc
    {
        float phase = 0.0F;
        float step = 0.0F;     // phase per sample (worked out at the control rate)
        float detune = 0.0F;   // cents, fixed for the note
        float drift = 0.0F, driftTarget = 0.0F; // cents
        int driftCountdown = 0; // control ticks
        float place = 0.0F;    // the copies around the first: ±1 (a little of its own), the ear it is upside down in
        bool partner = false;  // one of the quieter copies around the first
        float left = 0.0F, right = 0.0F;
    };
    struct LayerVoice
    {
        std::array<Osc, kUnison> osc{};
        float subPhase = 0.0F, subStep = 0.0F;
        float pwmPhase = 0.0F;
        float width = 0.5F; // the pulse width now
        float pulseLevel = 1.0F; // its loudness kept as a square's, whatever the width
        // A spectral layer: its loop, where each ear reads it, how fast, and its place.
        int table = 0;
        double readLeft = 0.0, readRight = 0.0;
        double tableStep = 0.0;
        float spectralLeft = 0.0F, spectralRight = 0.0F;
        Envelope amp;
    };
    struct Voice
    {
        int note = -1;
        float velocity = 1.0F;
        bool keyDown = false;
        bool sustained = false;
        bool stolen = false;
        uint64_t order = 0;
        // The pad it was started as, and its engine: it plays on as that
        // pad until it ends (another pad chosen meanwhile lets it go).
        int type = 0;
        Engine kind = Engine::Classic;
        std::array<LayerVoice, kLayers> layer{};
        std::array<float, kDrawbars> organPhase{};
        float organStep = 0.0F; // the drawbars' fundamental, phase per sample
        // Its own small differences (voice dispersion), fixed for the note: -1 .. 1 each.
        float cutoffOffset = 0.0F, driveOffset = 0.0F, levelOffset = 0.0F, panOffset = 0.0F, widthOffset = 0.0F;
        float releaseOffset = 0.0F, pitchOffset = 0.0F, pwmRate = 1.0F;
        float gain = 1.0F;  // its level (from levelOffset)
        float pan = 0.0F;   // its place (from panOffset)
        // Its smooth random movement.
        float randomNow = 0.0F, randomTarget = 0.0F;
        int randomCountdown = 0;
        float age = 0.0F;   // seconds since it started (the vibrato fades in)
        float vibratoPhase = 0.0F;
        // The breath's and the pulse's phases (started from the synth's: every note on one clock), and the pulse's level now.
        float lfoPhase = 0.0F, stepPhase = 0.0F, stepGain = 1.0F;
        int controlCountdown = 0;
        float drive = 1.0F;
        FilterCoeffs coeffs;
        SoftFilter softL, softR;
        LadderFilter ladderL, ladderR;
        std::array<Svf, 3> vowelL{}, vowelR{};
        Resonators bellsL, bellsR;
        int strikeCountdown = 0; // control ticks
        Svf airL, airR;
        EngineVoice engine; // its pad's engine (PadType::engine), when it has one
        [[nodiscard]] bool active() const { return layer[0].amp.active() || layer[1].amp.active(); }
    };
    struct LayerDerived
    {
        bool on = false;
        float gain = 0.0F;
        float saw = 0.0F, pulse = 0.0F, triangle = 0.0F, sub = 0.0F, noise = 0.0F;
        int unison = 1;
        float partner = 1.0F; // the copies around the first, against it
        float norm = 1.0F;    // as loud with one copy as with four
        const SpectralTables* tables = nullptr; // a spectral layer's loops (else the oscillators)
        float stereo = 1.0F, stereoNorm = 1.0F; // LayerType::stereo, and the right ear kept as loud as the left
        double attack = 1.5, release = 5.0;
    };
    struct Derived
    {
        const PadType* type = nullptr;
        std::array<LayerDerived, kLayers> layer{};
        std::array<float, kDrawbars> drawbarGain{};
        double cutoffHz = 2500.0;
        double resonance = 0.0;
        double breathOctaves = 0.1; // how far the filter breathes (before the movement's scale)
        double motion = 1.0;     // the movement's scale (0 still .. 2 twice)
        double sweepOctaves = 0.0; // each note's filter shape (PadType::sweepOctaves, with the macros')
        Engine engine = Engine::Classic; // the pad's engine as it plays (a sampled pad without its recordings: its synth)
        double brightness = 0.0;   // an engine's: the Brightness macro, octaves
        double character = 0.0;    // an engine's: the Timbre macro, -1 .. 1
        double vibratoCents = 0.0;
        double dispersion = 1.0; // the voices' differences' scale (1: as the design's 0.5)
        double attack = 1.5, release = 5.0;
        double width = 1.0;      // the copies' spread, against the pad's own
        float detuneScale = 1.0F; // how far the copies stray, against the pad's own
        float strikeScale = 1.0F; // how often the texture is struck, against the pad's own
        float drive = 1.0F;
        float textureGain = 0.0F;
        float velocity = 0.0F;
        float masterGain = 1.0F;
    };

    void applyParams() noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void startVoice(Voice& v, int note) noexcept;
    void setEnvelopes(Voice& v) noexcept;
    void noteOff(int note) noexcept;
    void setSustain(bool down) noexcept;
    void releaseAll(bool fast) noexcept;
    void render(std::span<float> left, std::span<float> right) noexcept;
    void renderChunk(std::span<float> left, std::span<float> right) noexcept;
    void control(Voice& v) noexcept;
    void renderVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept;
    void renderEngineVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept;
    [[nodiscard]] EngineContext engineContext(const Voice& v) const noexcept;
    [[nodiscard]] Engine engineFor(std::size_t index) const noexcept;

    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<float, kParamCount> m_seen{};
    std::atomic<float> m_extraShimmer{0.0F};
    std::atomic<bool> m_quickAttack{false};
    float m_extraShimmerSeen = -1.0F;
    bool m_quickAttackSeen = false;
    std::atomic<bool> m_releaseAll{false};
    Derived m_d;
    double m_rate = 48000.0;
    double m_voiceRate = 192000.0; // m_rate x kOversample
    int m_maxBlock = 512;
    std::vector<float> m_voiceLeft, m_voiceRight; // the voices, at m_voiceRate
    Decimator4 m_downLeft, m_downRight;
    LoFi m_tape; // (at m_voiceRate: it bends the whole chord)
    std::array<Voice, kVoicePool> m_voices{};
    uint64_t m_order = 0;
    bool m_sustain = false;
    Random m_random;
    double m_lfoPhase = 0.0; // the filter's slow breath, shared (the pad breathes as one)
    double m_stepPhase = 0.0; // the pulse's clock, shared likewise
    int m_type = -1;
    float m_driftGlide = 0.1F, m_randomGlide = 0.04F; // per control tick: how fast the wanders follow their targets
    // Every pad's spectral loops (built at prepare, shared between instances), by pad and layer.
    std::vector<std::shared_ptr<const SpectralTables>> m_spectralKeep;
    std::vector<std::array<const SpectralTables*, kLayers>> m_spectralFor;
    // Every pad's engine's waves and spectral tone (built at prepare), by pad; and the engines' room (at the voices' rate).
    std::vector<std::shared_ptr<const WaveSet>> m_wavesFor;
    std::vector<const SpectralTables*> m_engineSpectralFor;
    std::vector<std::shared_ptr<const SampleSet>> m_samplesFor;
    std::vector<float> m_engineLeft, m_engineRight;
    Rotary m_rotary;
    float m_drift = 0.0F, m_driftTarget = 0.0F; // cents: the whole synth's wander (every note's first copies)
    int m_driftCountdown = 0;                   // samples

    // The effects: (the tape, above), a crusher, low cut, chorus, ensemble,
    // phaser, width, echo, the shimmer plate, the air shelf.
    LoFi m_lofi;
    Svf m_lowCutL, m_lowCutR;
    BbdChorus m_chorus;
    DelayLine m_ensembleL;
    double m_ensemblePhase = 0.0;
    float m_ensembleMix = 0.0F;
    Phaser m_phaser;
    sawstack::Dimension m_width;
    sawstack::PingPong m_echo;
    ShimmerReverb m_reverb;
    sawstack::EqBand m_air;
    Svf m_sideLow;                          // the lows kept in the middle (what differs between the ears, its lows taken away)
    Svf m_topL1, m_topL2, m_topR1, m_topR2; // the top cut: two 2-poles, 24 dB an octave (Butterworth)
    // Every macro heard on every pad (each passes the sound untouched with
    // its knob in the middle): an engine's pad darker or brighter
    // (Brightness) and warmer or more forward (Timbre) here; any pad's
    // Movement turned past its design sweeps a low-pass slowly down and up.
    Svf m_toneLowL, m_toneLowR;
    sawstack::EqBand m_toneShelf, m_timbreLow, m_timbrePresence;
    double m_toneHz = 0.0;       // the low-pass's top (0: none)
    double m_moveOctaves = 0.0;  // how far Movement sweeps it down
    double m_movePhase = 0.0;
    float m_timbreDrive = 1.0F;  // above 1: saturated, level kept
    bool m_shelfOn = false, m_timbreOn = false;
    int m_toneCountdown = 0;
    float m_gainNow = 0.0F; // smoothed master gain (knob moves never click)
};

} // namespace gigchain::engine::pads
