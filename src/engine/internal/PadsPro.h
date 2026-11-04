#pragma once

#include "INode.h"
#include "SawStackDsp.h"
#include "SawStackFx.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

// Pads Pro: the app's own instrument of lush analog worship pads, in the
// spirit of the classic synths behind Analog Lab's pads (a Juno's pulse and
// chorus, a Jupiter-8's detuned saws, an OB-Xa's soft 2-pole filter, a CS-80's
// choir, a string machine's ensemble, a hymn organ). Slow blooms, long tails,
// every note drifting a little as old synths do, through a lush reverb with
// an octave-up shimmer in its feedback (as Eno's and Valhalla's). Played from
// the keyboard as any instrument, and held as a drone by Sustain Pads.
//
// Its sounds are ours, from generic analog-synth practice (the research in
// the project's notes); nothing is taken from another product's presets.
// Settings are parameters (0-1, as plugins have) that any thread may set; the
// audio thread picks them up at the next block. process() runs on the audio
// thread and never allocates, locks or logs.
namespace gigchain::engine::pads {

using sawstack::DelayLine;
using sawstack::Envelope;
using sawstack::Random;
using sawstack::Svf;

inline constexpr int kMaxNotes = 16;  // notes sounding at once (more steal the oldest)
inline constexpr int kVoicePool = 20; // room for stolen notes to fade out
inline constexpr int kSaws = 7;       // a voice's unison saws, at most
inline constexpr int kDrawbars = 4;   // the organ's: 16', 8', 4', 2 2/3'

// What players see and can learn to their keyboard's knobs.
enum class Param : uint8_t
{
    Type,     // which pad (kTypes)
    Tone,     // 0.5: as the pad was designed; brighter above, darker below (±2 octaves)
    Shimmer,  // the octave-up shimmer in the reverb (0: none; each pad sets where it starts)
    Motion,   // 0.5: as designed; the slow movement (filter, pulse width, drift) less or more
    Attack,   // seconds (Ranges::seconds); each pad sets where it starts
    Release,  // likewise
    Width,    // the widening (each pad sets where it starts)
    Reverb,   // the reverb's mix (each pad sets where it starts)
    Level,    // the whole (Ranges::levelDb), 0 dB at its middle
    Velocity, // how much playing harder is louder (0: not at all, as worship pads sit steady)
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

// One pad: what makes its sound. Levels in dB (at or below -60: off).
struct PadType
{
    const char* key = "";  // in saved settings (never changes once released)
    const char* name = ""; // as players see it
    const char* about = "";
    // The oscillators.
    double sawDb = -60.0;
    int saws = 1;           // unison saws
    double sawCents = 0.0;  // the unison's spread, lowest to highest
    double octaveSawDb = -60.0; // saws an octave up (a string machine's shine)
    double pulseDb = -60.0;
    double pulseWidth = 0.5;   // 0.5 is a square
    double pwmDepth = 0.0;     // ± around pulseWidth
    double pwmHz = 0.4;
    double subDb = -60.0;      // an octave down
    bool subSine = false;      // else a square
    double noiseDb = -60.0;    // breath
    std::array<double, kDrawbars> drawbarsDb{-60.0, -60.0, -60.0, -60.0}; // organ sines at 0.5, 1, 2, 3 times
    bool choir = false;        // through the "aah" vowel filter
    // The filter (12 dB/oct, as an OB's 2-pole: soft, keeps its air).
    double cutoffHz = 2000.0;
    double resonance = 0.7;     // Q
    double keyTrack = 0.3;      // how much higher notes open it (1: follows them fully)
    double filterLfoHz = 0.1;
    double filterLfoOctaves = 0.0;
    double highpassHz = 100.0;  // leaves the bass's room
    // Movement every note has.
    double driftCents = 3.0;    // slow random pitch (old analog oscillators)
    double vibratoHz = 5.0;
    double vibratoCents = 0.0;  // fades in after the attack
    // The envelope.
    double attack = 1.5, release = 5.0;
    // The effects.
    double chorus = 0.3;       // Vital's chorus mix
    double ensemble = 0.0;     // a string machine's fast ensemble (0-1)
    double width = 0.4;
    double reverb = 0.4;       // mix
    double reverbSeconds = 6.0;
    double reverbSize = 0.75;
    double reverbDampHz = 3000.0;
    double shimmer = 0.0;      // 0-1
    double levelDb = 0.0;      // the pad's own balance (all equally loud)
};

[[nodiscard]] std::span<const PadType> padTypes();
[[nodiscard]] const PadType& padType(int index);
[[nodiscard]] int padTypeCount();
[[nodiscard]] double typeValue(int index);
[[nodiscard]] int typeIndex(double value);
// A parameter's name ("Tone") and key in saved settings ("tone").
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

// The reverb with the shimmer inside it: what comes out of the reverb goes
// back in an octave higher (cleaned of its lows and softly limited, so it can
// never run away), so each time round the tail climbs: the shimmer of
// worship pads. Shimmer 0 is the plain reverb.
class ShimmerReverb
{
public:
    void prepare(double sampleRate)
    {
        m_reverb.prepare(sampleRate);
        m_reverb.setMix(1.0); // (wet only: the dry is mixed here)
        m_upL.prepare(sampleRate);
        m_upR.prepare(sampleRate);
        m_loopHpL.set(400.0, 0.7071, sampleRate);
        m_loopHpR.set(400.0, 0.7071, sampleRate);
    }
    void set(const sawstack::Reverb::Settings& settings, double mix, double shimmer)
    {
        m_reverb.set(settings);
        m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0));
        m_feedback = static_cast<float>(0.72 * std::clamp(shimmer, 0.0, 1.0));
        m_shimmerOut = static_cast<float>(0.5 * std::clamp(shimmer, 0.0, 1.0));
    }
    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        float inL = left + m_backL * m_feedback;
        float inR = right + m_backR * m_feedback;
        m_reverb.tick(inL, inR); // now the wet
        const float upL = m_upL.tick(m_loopHpL.highPass(inL));
        const float upR = m_upR.tick(m_loopHpR.highPass(inR));
        m_backL = std::tanh(upL);
        m_backR = std::tanh(upR);
        const float wetL = inL + upL * m_shimmerOut;
        const float wetR = inR + upR * m_shimmerOut;
        const float wet = std::sin(m_mix * std::numbers::pi_v<float> * 0.5F);
        const float dry = std::cos(m_mix * std::numbers::pi_v<float> * 0.5F);
        left = left * dry + wetL * wet;
        right = right * dry + wetR * wet;
    }

private:
    sawstack::Reverb m_reverb;
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

    // Audio thread.
    void process(std::span<const MidiEvent> events, const AudioBlock& io) noexcept;
    [[nodiscard]] bool holdsNotes() const noexcept;
    [[nodiscard]] int soundingNotes() const noexcept;

private:
    struct Voice
    {
        int note = -1;
        float velocity = 1.0F;
        bool keyDown = false;
        bool sustained = false;
        bool stolen = false;
        uint64_t order = 0;
        std::array<float, kSaws> sawPhase{};
        std::array<float, kSaws> octavePhase{};
        float pulsePhase = 0.0F, subPhase = 0.0F, pwmPhase = 0.0F;
        std::array<float, kDrawbars> organPhase{};
        float driftNow = 0.0F, driftTarget = 0.0F; // cents
        int driftCountdown = 0;
        float vibratoPhase = 0.0F;
        float age = 0.0F; // seconds since it started (the vibrato fades in)
        Svf lpL, lpR;
        std::array<Svf, 3> vowelL{}, vowelR{};
        Envelope amp;
        int filterCountdown = 0;
        [[nodiscard]] bool active() const { return amp.active(); }
    };
    struct Derived
    {
        const PadType* type = nullptr;
        float sawGain = 0.0F, octaveGain = 0.0F, pulseGain = 0.0F, subGain = 0.0F, noiseGain = 0.0F;
        std::array<float, kDrawbars> drawbarGain{};
        std::array<float, kSaws> sawOffset{}, sawLeft{}, sawRight{}; // cents, ears
        double cutoffHz = 2000.0;
        double motion = 1.0; // the Motion knob's scale (0 still .. 2 twice)
        double attack = 1.5, release = 5.0;
        float velocity = 0.0F;
        float masterGain = 1.0F;
    };

    void loadType(int index) noexcept;
    void applyParams() noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void setSustain(bool down) noexcept;
    void releaseAll(bool fast) noexcept;
    void render(std::span<float> left, std::span<float> right) noexcept;
    void renderVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept;

    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<float, kParamCount> m_seen{};
    std::atomic<bool> m_releaseAll{false};
    Derived m_d;
    double m_rate = 48000.0;
    std::array<Voice, kVoicePool> m_voices{};
    uint64_t m_order = 0;
    bool m_sustain = false;
    Random m_random;
    double m_lfoPhase = 0.0; // the slow filter movement, shared (the pad breathes as one)
    int m_type = -1;

    // The effects: low cut, chorus, ensemble, width, then the shimmer reverb.
    Svf m_lowCutL, m_lowCutR;
    sawstack::Chorus m_chorus;
    DelayLine m_ensembleL, m_ensembleR;
    double m_ensemblePhase = 0.0;
    float m_ensembleMix = 0.0F;
    sawstack::Dimension m_width;
    ShimmerReverb m_reverb;
    float m_gainNow = 0.0F; // smoothed master gain (knob moves never click)
};

} // namespace gigchain::engine::pads
