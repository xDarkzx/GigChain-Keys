#pragma once

#include "INode.h"
#include "MidiRouter.h"

#include "gigchain/core/Ids.h"
#include "gigchain/engine/EngineTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gigchain::engine {

// A keyboard knob, fader or pedal (a MIDI controller) moving one parameter
// of one plugin on a strip, within a range (MainStage's screen controls).
struct ParameterMapping
{
    int midiChannel = 0; // 0 = any, 1..16
    int controller = 0;  // CC 0-127
    int target = -1;     // -1 = the instrument, else the effect's position
    uint32_t parameter = 0;
    double minimum = 0.0; // the parameter's value (0-1) at CC 0...
    double maximum = 1.0; // ... and at CC 127 (may be below minimum: reversed)
};

// Everything needed to build one channel strip. Nodes must already be
// prepared for the graph's sample rate and block size.
struct StripSpec
{
    core::ChannelId id;
    RouteSettings route;
    std::shared_ptr<INode> instrument; // may be null: the strip is silent (or plays an input)
    std::vector<std::shared_ptr<INode>> effects;
    std::vector<ParameterMapping> mappings;
    // An audio input (0-based input channels) instead of an instrument: a
    // microphone or guitar through the strip's effects. -1 = none; a mono
    // input has only `inputLeft`.
    int inputLeft = -1;
    int inputRight = -1;
    double volumeDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

// One mixer strip inside a graph. Routing and nodes are fixed; volume, mute
// and solo are atomics the main thread may change while audio runs. Meters
// are written by the audio thread and read by the main thread.
//
// After a patch change a strip that is not in the new patch can go on
// sounding as a "tail" of the new graph: held notes ring until their keys
// (and the sustain pedal) are released, and reverbs fade out.
class ChannelStrip
{
public:
    ChannelStrip(StripSpec spec, int maxBlock);

    [[nodiscard]] const core::ChannelId& id() const { return m_id; }

    // Main thread, while audio runs.
    void setVolumeDb(double volumeDb);
    // -1 (left) .. +1 (right), constant-power law (centre = unity on both sides).
    void setPan(double pan);
    void setMute(bool on) { m_mute.store(on, std::memory_order_relaxed); }
    void setSolo(bool on) { m_solo.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool solo() const { return m_solo.load(std::memory_order_relaxed); }
    // Peak since the last call (then reset), and the most recent block's RMS.
    LevelReading takeLevel();

    // Main thread: the nodes this strip plays (instrument first).
    [[nodiscard]] std::vector<const INode*> nodes() const;
    // The largest block the strip can render.
    [[nodiscard]] int maxBlock() const { return static_cast<int>(m_left.size()); }
    // True once a tail has gone quiet (set by the audio thread): it no longer
    // needs to run.
    [[nodiscard]] bool tailDone() const { return m_tailDone.load(std::memory_order_relaxed); }

    // Audio thread.
    void render(std::span<const MidiEvent> events, const AudioBlock& mix, bool anySolo, const TimeInfo& time,
                const AudioInputs& inputs) noexcept;
    // Audio thread: as a tail of a newer patch. Only note-offs and the
    // sustain pedal reach it; it stops (tailDone) after a second of silence
    // with nothing held.
    void renderTail(std::span<const MidiEvent> events, const AudioBlock& mix, const TimeInfo& time) noexcept;

private:
    // The strip's sound for this block into m_left/m_right, before its fader.
    void produce(std::span<const MidiEvent> routed, int frames, const TimeInfo& time, const AudioInputs& inputs) noexcept;
    // Adds the strip's sound to `mix` at `gain` with the pan law; returns the peak.
    float mixInto(const AudioBlock& mix, float gain) noexcept;

    core::ChannelId m_id;
    RouteSettings m_route;
    std::shared_ptr<INode> m_instrument;
    std::vector<std::shared_ptr<INode>> m_effects;
    std::vector<ParameterMapping> m_mappings;
    int m_inputLeft = -1;
    int m_inputRight = -1;
    std::vector<float> m_left;
    std::vector<float> m_right;
    std::vector<MidiEvent> m_routed;
    std::atomic<float> m_gain{1.0F};
    std::atomic<float> m_pan{0.0F};
    std::atomic<bool> m_mute{false};
    std::atomic<bool> m_solo{false};
    std::atomic<float> m_peak{0.0F};
    std::atomic<float> m_rms{0.0F};
    // Tail state (audio thread): how long it has been silent, and how long
    // since nothing was held; and whether it finished.
    int64_t m_quietSamples = 0;
    int64_t m_releasedSamples = 0;
    std::atomic<bool> m_tailDone{false};

    static_assert(std::atomic<float>::is_always_lock_free);
};

// The processing graph for one patch. Built and destroyed on the main thread;
// render() runs on the audio thread and never allocates, locks or logs.
class RenderGraph
{
public:
    // `masterEffects` process the mix, in order, before the master gain.
    // `tails`: strips of earlier patches still ringing out (see ChannelStrip).
    RenderGraph(std::vector<StripSpec> specs, double sampleRate, int maxBlock,
                std::vector<std::shared_ptr<INode>> masterEffects = {},
                std::vector<std::shared_ptr<ChannelStrip>> tails = {});

    // Audio thread. Overwrites `out`.
    void render(std::span<const MidiEvent> events, AudioBlock out, float masterGain, const TimeInfo& time = {},
                const AudioInputs& inputs = {}) noexcept;

    // Main thread lookups for live mixer changes and meters. Non-owning.
    [[nodiscard]] ChannelStrip* strip(std::size_t index);
    [[nodiscard]] ChannelStrip* findStrip(const core::ChannelId& id);
    [[nodiscard]] std::size_t stripCount() const { return m_strips.size(); }
    // Main thread: the strips, and the tails still sounding, for the next
    // graph to carry over.
    [[nodiscard]] const std::vector<std::shared_ptr<ChannelStrip>>& strips() const { return m_strips; }
    [[nodiscard]] const std::vector<std::shared_ptr<ChannelStrip>>& tails() const { return m_tails; }
    // Blocks refused (rendered as silence) because they exceeded maxBlock,
    // since the last call. The audio thread cannot log; the main thread polls.
    uint64_t takeOversizedBlocks() { return m_oversizedBlocks.exchange(0, std::memory_order_relaxed); }
    [[nodiscard]] double sampleRate() const { return m_sampleRate; }
    [[nodiscard]] int maxBlock() const { return m_maxBlock; }

private:
    std::vector<std::shared_ptr<ChannelStrip>> m_strips;
    std::vector<std::shared_ptr<ChannelStrip>> m_tails;
    std::vector<std::shared_ptr<INode>> m_masterEffects;
    double m_sampleRate;
    int m_maxBlock;
    std::atomic<uint64_t> m_oversizedBlocks{0};
};

// Linear gain for a volume in dB; the floor (-96 dB) is silence.
float dbToGain(double volumeDb);

} // namespace gigchain::engine
