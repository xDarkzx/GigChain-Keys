#pragma once

#include "MidiEvent.h"

#include "gigchain/core/Error.h"

#include <cstdint>
#include <span>

namespace gigchain::engine {

// The audio interface's inputs for one block: one pointer per input channel,
// each `frames` long. Empty when no inputs are open.
struct AudioInputs
{
    std::span<const float* const> channels;
    int frames = 0;
};

// Where the music is, for tempo-synced plugins (arpeggiators, delays, LFOs).
// A live rig's clock always runs: it starts with the audio and never stops.
struct TimeInfo
{
    double tempo = 120.0;        // beats (quarter notes) per minute
    double sampleRate = 48000.0;
    int64_t samplePosition = 0;  // samples since the clock started
    double ppqPosition = 0.0;    // quarter notes since the clock started
    double barStartPpq = 0.0;    // where the current bar started, in quarter notes
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    // Quarter notes per bar (4 in 4/4, 3 in 3/4, 3 in 6/8).
    [[nodiscard]] double quartersPerBar() const
    {
        return timeSigDenominator > 0 ? 4.0 * timeSigNumerator / timeSigDenominator : 4.0;
    }
};

// A stereo block of audio. `frames` never exceeds the maxBlock a node was
// prepared with.
struct AudioBlock
{
    float* left = nullptr;
    float* right = nullptr;
    int frames = 0;
};

// Something the render graph runs on the audio thread: an instrument (writes
// `io` from MIDI) or an effect (processes `io` in place).
//
// prepare() runs on the main thread before the node is published to the
// audio thread. process() runs on the audio thread and must not allocate,
// lock, log or throw.
class INode
{
public:
    virtual ~INode() = default;
    INode(const INode&) = delete;
    INode& operator=(const INode&) = delete;
    INode(INode&&) = delete;
    INode& operator=(INode&&) = delete;

    // Fails with the precise cause when the node cannot run at this setup.
    virtual core::Result<void> prepare(double sampleRate, int maxBlock) = 0;
    virtual void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) = 0;
    // Audio thread: sets one of the node's parameters (0-1) at a sample of
    // the next process() call. For knobs mapped to a parameter. Nodes without
    // parameters ignore it.
    virtual void queueParameter(uint32_t /*id*/, double /*value*/, int32_t /*sampleOffset*/) noexcept {}
    // Main thread: a parameter's value now (0-1) as the plugin shows it;
    // below 0 when it is not known (no such parameter, no parameters).
    [[nodiscard]] virtual double parameterValue(uint32_t /*id*/) const { return -1.0; }
    // Audio thread: true while the node holds notes (keys or sustain not yet released).
    [[nodiscard]] virtual bool holdsNotes() const noexcept { return false; }

protected:
    INode() = default;
};

} // namespace gigchain::engine
