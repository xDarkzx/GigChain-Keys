#pragma once

#include "MidiEvent.h"

#include "openstage/core/Error.h"

#include <span>

namespace openstage::engine {

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
    virtual void process(std::span<const MidiEvent> events, AudioBlock io) = 0;

protected:
    INode() = default;
};

} // namespace openstage::engine
