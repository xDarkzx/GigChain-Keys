#pragma once

#include "INode.h"
#include "SawStackDsp.h"
#include "SawStackFx.h"
#include "SawStackTypes.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace gigchain::engine::sawstack {

// Saw Stack Pro's sound: eight layers that each have a job (a sine sub, a low
// saw, the main saw, a wide saw, an octave up, a square for body, a pluck for
// the hit, and noise for air), played together from one keyboard. Every
// layer but the noise is a full supersaw of up to 16 saws.
//
// Settings are parameters (0-1 each, as plugins have) the main thread or
// the audio thread may set at any time; the audio thread picks changes up at
// the next block. process() runs on the audio thread and never allocates,
// locks or logs.
inline constexpr int kLayers = 8;
inline constexpr int kNoiseLayer = 7;
inline constexpr int kMaxNotes = 16;      // notes sounding at once (more steal the oldest)
inline constexpr int kVoicePool = 24;     // room for stolen notes to fade out
inline constexpr int kFilterUpdate = 16;  // samples between filter cutoff updates

enum class LayerParam : uint8_t
{
    On,
    Level,
    Octave,
    Voices,
    Detune,
    Mix,
    Width,
    Wave, // the noise layer: its colour
    Cutoff,
    Resonance,
    FilterEnv,
    FilterDecay,
    Attack,
    Decay,
    Sustain,
    Release,
    Highpass,
    LowestOnly, // plays only the lowest held note (sub and bass layers: no muddy chords down low)
    Count,
};
inline constexpr int kLayerParams = static_cast<int>(LayerParam::Count);

enum class GlobalParam : uint8_t
{
    Level,
    BendRange,
    Velocity,
    Count,
};
inline constexpr int kGlobalParams = static_cast<int>(GlobalParam::Count);

// What players see and can map to their keyboard's knobs: the type, and
// five knobs that work on every layer at once. Everything else is the type's.
enum class Macro : uint8_t
{
    Type,
    Cutoff,    // 0.5: as the type was designed; brighter above, darker below
    Resonance, // 0: as designed
    Attack,    // seconds, as Ranges::seconds (each type sets where it starts)
    Release,   // likewise
    Spread,    // 0.5: as designed; wider and more detuned above, narrower below
    Count,
};
inline constexpr int kMacros = static_cast<int>(Macro::Count);
inline constexpr int kParamCount = kLayers * kLayerParams + kGlobalParams + kMacros;

[[nodiscard]] constexpr uint32_t paramId(int layer, LayerParam p)
{
    return static_cast<uint32_t>(layer * kLayerParams + static_cast<int>(p));
}
[[nodiscard]] constexpr uint32_t paramId(GlobalParam p)
{
    return static_cast<uint32_t>(kLayers * kLayerParams + static_cast<int>(p));
}
[[nodiscard]] constexpr uint32_t paramId(Macro p)
{
    return static_cast<uint32_t>(kLayers * kLayerParams + kGlobalParams + static_cast<int>(p));
}

enum class NoiseColour : uint8_t
{
    White,
    Pink,
    Crackle,
};

// What a parameter's 0-1 value means, and back.
struct Ranges
{
    static double levelDb(double v) { return -48.0 + 54.0 * v; } // 0 = off
    static double levelGain(double v) { return v <= 0.0 ? 0.0 : std::pow(10.0, levelDb(v) / 20.0); }
    static double fromLevelDb(double db) { return std::clamp((db + 48.0) / 54.0, 0.0, 1.0); }
    static double hz(double v) { return 20.0 * std::pow(1000.0, std::clamp(v, 0.0, 1.0)); } // 20 Hz .. 20 kHz
    static double fromHz(double frequency) { return std::clamp(std::log(frequency / 20.0) / std::log(1000.0), 0.0, 1.0); }
    static double seconds(double v) { return 0.0005 * std::pow(16000.0, std::clamp(v, 0.0, 1.0)); } // 0.5 ms .. 8 s
    static double fromSeconds(double s) { return std::clamp(std::log(s / 0.0005) / std::log(16000.0), 0.0, 1.0); }
    static int octave(double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 4.0)) - 2; } // -2..+2
    static double fromOctave(int o) { return (std::clamp(o, -2, 2) + 2) / 4.0; }
    static int voices(double v) { return 1 + static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * (kMaxUnison - 1))); }
    static double fromVoices(int n) { return (std::clamp(n, 1, kMaxUnison) - 1) / double(kMaxUnison - 1); }
    static int choice(double v, int count) { return std::clamp(static_cast<int>(std::lround(v * (count - 1))), 0, count - 1); }
    static double fromChoice(int i, int count) { return count > 1 ? double(i) / (count - 1) : 0.0; }
    static double q(double v) { return 0.5 + 7.5 * std::clamp(v, 0.0, 1.0); }
    static double filterOctaves(double v) { return 6.0 * std::clamp(v, 0.0, 1.0); }
    static int bendRange(double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 12.0)); }
};

// The layers' names, as players see them.
[[nodiscard]] QString layerName(int layer);
// A parameter's name, e.g. "Main Saw Detune", "Cutoff".
[[nodiscard]] QString paramName(uint32_t id);
// A parameter's key in saved settings, e.g. "main.detune", "cutoff" (never changes once released).
[[nodiscard]] QString paramKey(uint32_t id);
// The type knob's value for type `index`, and back.
[[nodiscard]] double typeValue(int index);
[[nodiscard]] int typeIndex(double value);

// Every parameter as stack type `index` has it (its layers, and its knobs where it starts them).
[[nodiscard]] std::array<float, kParamCount> typeParams(int index);
// The sound a new Saw Stack Pro starts with (the default type).
[[nodiscard]] std::array<float, kParamCount> defaultParams();

class Synth
{
public:
    Synth();

    // Main thread, before process().
    void prepare(double sampleRate, int maxBlock);

    // Any thread.
    void setParam(uint32_t id, double value) noexcept;
    [[nodiscard]] double param(uint32_t id) const noexcept;
    // Main thread: every held note is let go at the next block.
    void requestReleaseAll() noexcept { m_releaseAll.store(true, std::memory_order_release); }

    // Audio thread.
    void process(std::span<const MidiEvent> events, const AudioBlock& io) noexcept;
    [[nodiscard]] bool holdsNotes() const noexcept;
    // Notes sounding (held, or ringing out), for tests.
    [[nodiscard]] int soundingNotes() const noexcept;

private:
    struct LayerVoice
    {
        std::array<float, kMaxUnison> phase{};
        float hpInL = 0.0F, hpInR = 0.0F, hpOutL = 0.0F, hpOutR = 0.0F; // removes the rumble under the note
        Svf lpL, lpR;
        Envelope amp, filter;
        int filterCountdown = 0;
        int note = -1;      // mono layers: the note playing
        float velocity = 1.0F;
    };
    struct Voice
    {
        int note = -1;
        float velocity = 1.0F;
        bool keyDown = false;
        bool sustained = false; // let go while the sustain pedal is down
        bool stolen = false;    // fading out quickly to make room
        uint64_t order = 0;     // when it started
        std::array<LayerVoice, kLayers> layers{};
        [[nodiscard]] bool active() const;
    };
    struct LayerSetup // what a layer's parameters mean, with the knobs applied
    {
        bool on = false;
        float gain = 0.0F;
        int octave = 0;
        Wave wave = Wave::Saw;
        UnisonSpread spread;
        double cutoff = 20000.0, q = 0.7071, filterOctaves = 0.0, highpass = 20.0;
        bool lowestOnly = false;
        bool ownEnvelope = false; // the pluck: keeps its own short shape whatever Attack and Release say
        double attack = 0.003, decay = 0.1, sustain = 1.0, release = 0.35, filterDecay = 0.2, filterRelease = 0.35;
    };
    struct LayerBus // where a layer's notes are added up
    {
        Svf lowCutL, lowCutR;
        float gain = 0.0F; // smoothed: knob moves never click
    };
    struct Derived // what the parameters mean, worked out once per change
    {
        std::array<LayerSetup, kLayers> layers{};
        NoiseColour noise = NoiseColour::White;
        double noiseWidth = 1.0;
        float masterGain = 0.0F;
        int bendRange = 2;
        float velocity = 0.3F;
        double attack = 0.003, release = 0.35; // the knobs' (seconds)
    };

    // Main thread or audio thread: stack type `index` replaces every layer and knob.
    void loadType(int index) noexcept;

    void applyParams() noexcept;
    void setEnvelopes(LayerVoice& lv, const LayerSetup& setup) const noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void setSustain(bool down) noexcept;
    void releaseAll(bool fast) noexcept;
    void updateMono() noexcept;
    void render(std::span<float> left, std::span<float> right) noexcept;
    void renderLayer(LayerVoice& lv, const LayerSetup& setup, int note, float velocity, std::size_t frames) noexcept;
    void renderNoise(std::size_t frames) noexcept;
    [[nodiscard]] double noteHz(int note, const LayerSetup& setup) const noexcept;

    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<float, kParamCount> m_seen{};
    std::atomic<bool> m_releaseAll{false};
    Derived m_d;

    double m_rate = 48000.0;
    std::array<Voice, kVoicePool> m_voices{};
    std::array<LayerVoice, kLayers> m_mono{}; // the lowest-note-only layers
    uint64_t m_order = 0;
    bool m_sustain = false;
    double m_bend = 0.0; // semitones
    Random m_random;

    // The noise layer: one for the whole chord (noise per note would pile up into hiss).
    Envelope m_noiseEnv;
    Svf m_noiseHpL, m_noiseHpR, m_noiseLpL, m_noiseLpR;
    std::array<float, 7> m_pinkL{}, m_pinkR{};

    std::array<LayerBus, kLayers> m_bus{};
    float m_masterGain = 0.0F;
    std::atomic<int> m_type{-1};

    // Behind the scenes, as the type sets them.
    Drive m_driveL, m_driveR;
    Glue m_glue;
    Chorus m_chorus;
    int m_fxType = -1;
    std::vector<float> m_layerL{}, m_layerR{};
};

} // namespace gigchain::engine::sawstack
