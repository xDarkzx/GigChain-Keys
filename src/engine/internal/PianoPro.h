#pragma once

#include "FxChain.h"
#include "INode.h"
#include "PianoSamples.h"
#include "PianoTypes.h"
#include "SawStackDsp.h"
#include "gigchain/engine/BuiltinReadings.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

// Piano Pro's sound: a piano (VCSL's recorded grand or upright, or an FM
// electric piano), the same piano an octave up, soft strings and a sub, then
// the same effects and sidechain as Saw Stack Pro, plus tape. Parameters are
// 0-1 (as plugins have) and may be set from any thread; the audio thread
// picks changes up at the next block. process() never allocates, locks or logs.
namespace gigchain::engine::piano {

inline constexpr int kMaxNotes = 24;  // notes sounding at once (a pedalled piano rings a lot); more fade out the oldest
inline constexpr int kVoicePool = 96; // three layers a note, and room for faded notes and release sounds

enum class LayerParam : uint8_t { On, Level, Count };
inline constexpr int kLayerParams = static_cast<int>(LayerParam::Count);

enum class GlobalParam : uint8_t { Level, BendRange, Count };
inline constexpr int kGlobalParams = static_cast<int>(GlobalParam::Count);

// What players see and can map to their keyboard's knobs.
enum class Macro : uint8_t
{
    Type,
    Tone,      // the main low-pass (the type sets where it starts)
    Resonance,
    Attack,    // seconds (Ranges::seconds): 0 is the hammer's own
    Release,   // how long a let-go key takes to stop
    Width,
    Reverb,
    Delay,
    Ott,
    Tape,      // wow, flutter, warmth and hiss
    Sidechain, // as Saw Stack Pro's: Mix, Rate, Slope, Curve, Trigger, On
    SidechainRate,
    SidechainSlope,
    SidechainCurve,
    SidechainTrigger,
    SidechainOn,
    Count,
};
inline constexpr int kMacros = static_cast<int>(Macro::Count);
inline constexpr int kParamCount = kLayers * kLayerParams + kGlobalParams + kMacros;

[[nodiscard]] constexpr uint32_t paramId(Layer layer, LayerParam p)
{
    return static_cast<uint32_t>(static_cast<int>(layer) * kLayerParams + static_cast<int>(p));
}
[[nodiscard]] constexpr uint32_t paramId(GlobalParam p)
{
    return static_cast<uint32_t>(kLayers * kLayerParams + static_cast<int>(p));
}
[[nodiscard]] constexpr uint32_t paramId(Macro p)
{
    return static_cast<uint32_t>(kLayers * kLayerParams + kGlobalParams + static_cast<int>(p));
}

// Read only (not settings): see readings::kPianoRecordings and kPianoSource.
inline constexpr uint32_t kRecordingsParameter = readings::kPianoRecordings;
inline constexpr uint32_t kSourceParameter = readings::kPianoSource;
static_assert(kRecordingsParameter >= static_cast<uint32_t>(kParamCount) && kSourceParameter >= static_cast<uint32_t>(kParamCount));

// A parameter's name ("Tone", "Octave Level"), and its key in saved settings (never changes once released).
[[nodiscard]] QString paramName(uint32_t id);
[[nodiscard]] QString paramKey(uint32_t id);
[[nodiscard]] QString layerName(Layer layer);
[[nodiscard]] double typeValue(int index);
[[nodiscard]] int typeIndex(double value);
// Every parameter as type `index` has it.
[[nodiscard]] std::array<float, kParamCount> typeParams(int index);

class Synth
{
public:
    Synth();

    // Main thread, before process().
    void prepare(double sampleRate, int maxBlock);
    // Main thread: where recorded piano `which` is (PianoLibrary::request);
    // until it holds a piano, that source plays the FM piano.
    void useRecordings(Source which, const std::atomic<const SampleSet*>* slot) noexcept;

    // Any thread.
    void setParam(uint32_t id, double value) noexcept;
    [[nodiscard]] double param(uint32_t id) const noexcept;
    [[nodiscard]] Source source() const noexcept;
    void requestReleaseAll() noexcept { m_releaseAll.store(true, std::memory_order_release); }

    // Audio thread.
    void process(std::span<const MidiEvent> events, const AudioBlock& io, const TimeInfo& time = {}) noexcept;
    [[nodiscard]] bool holdsNotes() const noexcept;
    [[nodiscard]] int soundingNotes() const noexcept; // for tests
    [[nodiscard]] int layersPlaying(int note) const noexcept; // for tests: piano, octave and strings voices of `note`

private:
    // One sounding thing: a struck note (recorded or FM), or a release sound.
    struct Voice
    {
        bool active() const { return amp.active(); }
        int note = -1;
        int layer = 0;          // which layer it plays for
        bool keyDown = false;
        bool sustained = false; // let go while the pedal is down
        bool releaseSound = false;
        bool stolen = false;    // fading out quickly to make room
        uint64_t order = 0;
        float velocity = 1.0F;  // 0-1, as struck
        float gain = 1.0F;
        float panL = 1.0F, panR = 1.0F;
        double heldSeconds = 0.0;
        // Recorded.
        const SampleZone* zone = nullptr;
        double position = 0.0, step = 0.0;
        // FM, strings and sub.
        double phase = 0.0, modPhase = 0.0, tinePhase = 0.0, tineModPhase = 0.0, freq = 0.0;
        float modIndex = 0.0F, tineIndex = 0.0F, tineLevel = 0.0F;
        float modDecay = 1.0F, tineDecay = 1.0F, tineLevelDecay = 1.0F;
        std::array<float, 5> unisonPhase{};
        sawstack::Svf lowL, lowR;
        sawstack::Envelope amp;
    };
    struct Derived
    {
        std::array<bool, kLayers> on{};
        std::array<float, kLayers> gain{};
        float masterGain = 1.0F;
        int bendRange = 2;
        double attack = 0.002, release = 0.4;
    };

    void loadType(int index) noexcept;
    void applyParams() noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void letGo(Voice& v) noexcept;
    void setSustain(bool down) noexcept;
    void releaseAll(bool fast) noexcept;
    void updateSub() noexcept;
    // An idle voice, or the quietest started before order `keepFrom` (a
    // note's own new layers never take each other's).
    Voice* freeVoice(uint64_t keepFrom) noexcept;
    // Too many notes: the oldest fades out quickly (a new one is coming).
    void makeRoomForANote() noexcept;
    void startRecorded(Voice& v, const SampleSet& set, int note, int playedVelocity, int pitchNote) noexcept;
    void startFm(Voice& v, int pitchNote, float velocity) noexcept;
    void render(std::span<float> left, std::span<float> right) noexcept;
    // Adds `frames` of `v` into its layer's buffers; `bend` is the pitch bend as a ratio.
    void renderVoice(Voice& v, std::size_t frames, double bend) noexcept;
    [[nodiscard]] const SampleSet* recordings() const noexcept;

    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<float, kParamCount> m_seen{};
    std::atomic<bool> m_releaseAll{false};
    std::atomic<int> m_type{-1};
    std::array<std::atomic<const std::atomic<const SampleSet*>*>, kSources> m_slots{}; // set on the main thread, read on the audio one
    Derived m_d;
    int m_fxType = -1;
    Source m_source = Source::Grand;
    double m_softness = 0.0;
    sawstack::Svf m_lowCutL, m_lowCutR;

    double m_rate = 48000.0;
    std::array<Voice, kVoicePool> m_voices{};
    std::array<int, 128> m_roundRobin{};
    uint64_t m_order = 0;
    uint64_t m_newestNote = 1; // the newest note's first voice (never taken for another)
    bool m_sustain = false;
    double m_bend = 0.0;
    sawstack::Random m_random;
    std::array<std::vector<float>, kLayers> m_layerL{}, m_layerR{}; // each layer's notes, added up
    std::array<float, kLayers> m_busGain{};                          // smoothed: level moves never click
    std::vector<float> m_subL{}, m_subR{};                           // the sub, kept out of the effects
    int m_subNote = -1;
    sawstack::FxChain m_fx;
    std::atomic<float> m_sidechainPosition{0.0F};
    double m_tempo = 120.0, m_ppq = 0.0, m_ppqStep = 0.0;
};

} // namespace gigchain::engine::piano
