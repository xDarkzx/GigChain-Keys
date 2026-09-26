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

// Everything needed to build one channel strip. Nodes must already be
// prepared for the graph's sample rate and block size.
struct StripSpec
{
    core::ChannelId id;
    RouteSettings route;
    std::shared_ptr<INode> instrument; // may be null: the strip is silent
    std::vector<std::shared_ptr<INode>> effects;
    double volumeDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

// One mixer strip inside a graph. Routing and nodes are fixed; volume, mute
// and solo are atomics the main thread may change while audio runs. Meters
// are written by the audio thread and read by the main thread.
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

    // Audio thread.
    void render(std::span<const MidiEvent> events, const AudioBlock& mix, bool anySolo) noexcept;

private:
    core::ChannelId m_id;
    RouteSettings m_route;
    std::shared_ptr<INode> m_instrument;
    std::vector<std::shared_ptr<INode>> m_effects;
    std::vector<float> m_left;
    std::vector<float> m_right;
    std::vector<MidiEvent> m_routed;
    std::atomic<float> m_gain{1.0F};
    std::atomic<float> m_pan{0.0F};
    std::atomic<bool> m_mute{false};
    std::atomic<bool> m_solo{false};
    std::atomic<float> m_peak{0.0F};
    std::atomic<float> m_rms{0.0F};

    static_assert(std::atomic<float>::is_always_lock_free);
};

// The processing graph for one patch. Built and destroyed on the main thread;
// render() runs on the audio thread and never allocates, locks or logs.
class RenderGraph
{
public:
    // `masterEffects` process the mix, in order, before the master gain.
    RenderGraph(std::vector<StripSpec> specs, double sampleRate, int maxBlock,
                std::vector<std::shared_ptr<INode>> masterEffects = {});

    // Audio thread. Overwrites `out`.
    void render(std::span<const MidiEvent> events, AudioBlock out, float masterGain) noexcept;

    // Main thread lookups for live mixer changes and meters. Non-owning.
    [[nodiscard]] ChannelStrip* strip(std::size_t index);
    [[nodiscard]] ChannelStrip* findStrip(const core::ChannelId& id);
    [[nodiscard]] std::size_t stripCount() const { return m_strips.size(); }
    // Blocks refused (rendered as silence) because they exceeded maxBlock,
    // since the last call. The audio thread cannot log; the main thread polls.
    uint64_t takeOversizedBlocks() { return m_oversizedBlocks.exchange(0, std::memory_order_relaxed); }
    [[nodiscard]] double sampleRate() const { return m_sampleRate; }
    [[nodiscard]] int maxBlock() const { return m_maxBlock; }

private:
    std::vector<std::unique_ptr<ChannelStrip>> m_strips;
    std::vector<std::shared_ptr<INode>> m_masterEffects;
    double m_sampleRate;
    int m_maxBlock;
    std::atomic<uint64_t> m_oversizedBlocks{0};
};

// Linear gain for a volume in dB; the floor (-96 dB) is silence.
float dbToGain(double volumeDb);

} // namespace gigchain::engine
