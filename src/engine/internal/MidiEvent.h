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
    // From an input opened for its buttons and knobs only (a keyboard's DAW
    // port): learned controls and Learn see it, the instruments never do.
    bool controlsOnly = false;
    // Semitones the whole rig moves it (IEngine::setGlobalTranspose), on top
    // of each channel's own: stamped as it reaches the audio thread, a
    // note's release with the shift its press had (no hanging notes).
    int8_t transpose = 0;
    // The keyboard it came from (keyboardSource of its MIDI input's name);
    // 0: from the app itself (the on-screen keyboard, a song's notes), which
    // every channel hears.
    uint16_t source = 0;
};

// Most events a single audio block will carry; extra events are dropped.
inline constexpr int kMaxEventsPerBlock = 256;
// Most events one instrument takes in a block (room to spare over a full block).
inline constexpr int kMaxStripEventsPerBlock = 2 * kMaxEventsPerBlock;

} // namespace gigchain::engine
