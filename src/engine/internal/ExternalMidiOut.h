#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <map>
#include <memory>
#include <mutex>
#include <span>

namespace rt::midi {
class RtMidiOut;
} // namespace rt::midi

namespace gigchain::engine {

// Messages to hardware on MIDI outputs (a sound's Program Change to an
// external synth). Each output opens the first time something goes to it
// and stays open. Any thread (one at a time: a hardware synth's notes come
// from the engine's sender thread, its Program Changes from the main thread).
class ExternalMidiOut
{
public:
    ExternalMidiOut();
    ~ExternalMidiOut();
    ExternalMidiOut(const ExternalMidiOut&) = delete;
    ExternalMidiOut& operator=(const ExternalMidiOut&) = delete;
    ExternalMidiOut(ExternalMidiOut&&) = delete;
    ExternalMidiOut& operator=(ExternalMidiOut&&) = delete;

    // Sends `bytes` (one MIDI message) to the output named `port`. A port
    // that is not there or cannot open is an error (also logged); one that
    // fails is closed, so it opens again when it is back.
    core::Result<void> send(const QString& port, std::span<const unsigned char> bytes);

private:
    std::mutex m_mutex; // guards m_open
    std::map<QString, std::unique_ptr<rt::midi::RtMidiOut>> m_open;
};

} // namespace gigchain::engine
