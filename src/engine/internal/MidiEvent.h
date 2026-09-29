#pragma once

#include <cstdint>

namespace gigchain::engine {

// One raw MIDI channel message as it travels from the MIDI input to the
// audio thread. Trivially copyable so it can sit in lock-free queues.
struct MidiEvent
{
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    int32_t sampleOffset = 0; // position inside the audio block
};

// Most events a single audio block will carry; extra events are dropped.
inline constexpr int kMaxEventsPerBlock = 256;
// Most events one instrument takes in a block: a full block, and a chord
// handed over at a section switch on top (at most kMaxEventsPerBlock).
inline constexpr int kMaxStripEventsPerBlock = 2 * kMaxEventsPerBlock;

} // namespace gigchain::engine
