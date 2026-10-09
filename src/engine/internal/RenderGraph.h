#pragma once

#include "INode.h"
#include "MidiEffects.h"
#include "MidiRouter.h"

#include "gigchain/core/Ids.h"
#include "gigchain/core/KnobPickup.h"
#include "gigchain/engine/EngineTypes.h"

#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gigchain::engine {

class LoopStation;

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
    int curve = 0;        // core::KnobCurve
    bool pickup = true;   // takes over only once it reaches the parameter (core::KnobPickup)
};

// Output pair `pair` (1 = outputs 3-4) of the device's outputs 3 and up, or
// `fallback` (the mix) when the device does not have it. Real-time safe.
[[nodiscard]] inline AudioBlock sendPair(std::span<float* const> sends, int pair, const AudioBlock& fallback) noexcept
{
    if (pair <= 0 || std::cmp_greater(2 * pair, sends.size())) return fallback;
    const std::span<float* const> two = sends.subspan(static_cast<std::size_t>((2 * pair) - 2), 2);
    return AudioBlock{.left = two.front(), .right = two.back(), .frames = fallback.frames};
}

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
    // Where it plays: 0 the mix (outputs 1-2, through the master); n the
    // device's outputs 2n+1 and 2n+2 directly (3-4, 5-6...: the in-ears, the
    // desk). A pair the device does not have plays in the mix.
    int outputPair = 0;
    MidiEffectSettings midiEffects; // its chord trigger and arpeggiator
};

// Which section of the song is in force during a block: `before` up to the
// sample `switchAt`, `after` from it on (the same when nothing changes).
// -1 = the song has no sections: every strip plays.
struct SectionGate
{
    int before = -1;
    int after = -1;
    int switchAt = 0;
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
    // The song sections this strip plays in (bit n = section n; all by
    // default). Outside them it takes no new notes, but its held notes,
    // pedals and knobs carry on, so it rings out.
    void setSections(uint64_t mask) { m_sections.store(mask, std::memory_order_relaxed); }
    [[nodiscard]] uint64_t sections() const { return m_sections.load(std::memory_order_relaxed); }
    // Whether it takes new notes outside any section (a song without
    // sections); false: a sound playing another of its channels. Held notes
    // ring out as above.
    void setUnsectioned(bool plays) { m_unsectioned.store(plays, std::memory_order_relaxed); }
    [[nodiscard]] bool unsectioned() const { return m_unsectioned.load(std::memory_order_relaxed); }
    // The loop-station slot this channel records into (-1: none). What it
    // records is its sound at its fader and pan: what is heard of it.
    void setLoopSlot(int slot) { m_loopSlot.store(slot, std::memory_order_relaxed); }
    [[nodiscard]] int loopSlot() const { return m_loopSlot.load(std::memory_order_relaxed); }
    [[nodiscard]] int outputPair() const noexcept { return m_outputPair; } // StripSpec::outputPair
    [[nodiscard]] bool hasMidiEffects() const noexcept { return !m_effected.empty(); }
    // Peak since the last call (then reset), and the most recent block's RMS.
    LevelReading takeLevel();

    // Main thread: the nodes this strip plays (instrument first).
    [[nodiscard]] std::vector<const INode*> nodes() const;
    // Main thread, often: where each mapped parameter is now, for its knob's
    // pickup (a knob takes over only once it reaches it).
    void refreshMappedValues();
    // The largest block the strip can render.
    [[nodiscard]] int maxBlock() const { return static_cast<int>(m_left.size()); }
    // Events left out since the last call (its block was full).
    uint64_t takeDroppedEvents() { return m_droppedEvents.exchange(0, std::memory_order_relaxed); }
    // True once a tail has gone quiet (set by the audio thread): it no longer
    // needs to run.
    [[nodiscard]] bool tailDone() const { return m_tailDone.load(std::memory_order_relaxed); }

    // Audio thread.
    void render(std::span<const MidiEvent> events, const AudioBlock& mix, bool anySolo, const TimeInfo& time,
                const AudioInputs& inputs, const SectionGate& gate = {}, LoopStation* loops = nullptr) noexcept;
    // Audio thread: as a tail of a newer patch. Only note-offs and the
    // sustain pedal reach it; it stops (tailDone) after a second of silence
    // with nothing held.
    void renderTail(std::span<const MidiEvent> events, const AudioBlock& mix, const TimeInfo& time,
                    LoopStation* loops = nullptr) noexcept;

private:
    // The node a mapping moves (nullptr: its effect is not there).
    [[nodiscard]] INode* mappingTarget(const ParameterMapping& m) const noexcept;
    // The strip's sound for this block into m_left/m_right, before its fader.
    void produce(std::span<const MidiEvent> routed, int frames, const TimeInfo& time, const AudioInputs& inputs) noexcept;
    // Puts the strip's fader and pan (at `gain`) on its sound, adds it to
    // `mix` and to its loop (if recording); returns the peak.
    float mixInto(const AudioBlock& mix, float gain, LoopStation* loops) noexcept;

    core::ChannelId m_id;
    RouteSettings m_route;
    std::shared_ptr<INode> m_instrument;
    std::vector<std::shared_ptr<INode>> m_effects;
    std::vector<ParameterMapping> m_mappings;
    // Per mapping: its parameter's value as the plugin last showed it (main
    // thread writes; below 0 = not known), and its knob's pickup (audio thread).
    std::vector<std::atomic<float>> m_mappedNow;
    std::vector<core::KnobPickup> m_pickups;
    int m_inputLeft = -1;
    int m_inputRight = -1;
    std::vector<float> m_left;
    std::vector<float> m_right;
    std::vector<MidiEvent> m_routed;
    MidiEffects m_midiEffects;
    std::vector<MidiEvent> m_effected; // m_routed through the MIDI effects (only when it has some)
    // m_routed's first `count`, through the MIDI effects when it has any: what the instrument hears.
    std::span<const MidiEvent> effected(std::size_t count, int frames, const TimeInfo& time) noexcept;
    std::atomic<uint64_t> m_droppedEvents{0};
    std::atomic<float> m_gain{1.0F};
    std::atomic<float> m_pan{0.0F};
    std::atomic<bool> m_mute{false};
    std::atomic<bool> m_solo{false};
    int m_outputPair = 0;
    std::atomic<uint64_t> m_sections{~uint64_t{0}};
    std::atomic<bool> m_unsectioned{true};
    std::atomic<int> m_loopSlot{-1};
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

    // Audio thread. Overwrites `out`. `gate`: the song section in force.
    // `loops`: the loop station (its block begun): strips record into it,
    // and its loops play into the mix before the master effects.
    // `sends`: the device's outputs 3 and up (added to, never cleared here).
    void render(std::span<const MidiEvent> events, AudioBlock out, float masterGain, const TimeInfo& time = {},
                const AudioInputs& inputs = {}, const SectionGate& gate = {}, LoopStation* loops = nullptr,
                std::span<float* const> sends = {}) noexcept;

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
    // MIDI events left out because a strip's block was full (more than
    // kMaxStripEventsPerBlock), over every strip and tail, since the last call.
    uint64_t takeDroppedEvents();
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
