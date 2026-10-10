#pragma once

#include "HardwareOut.h"

#include <QString>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gigchain::engine {

class ExternalMidiOut;

// Plays the channels' hardware synths: a thread of its own takes what the
// audio thread put in each HardwareOut's queue and sends it to the synth's
// MIDI output (about a millisecond later; RtMidi may not be called from the
// audio thread). It runs only while some channel plays a synth.
class HardwareSender
{
public:
    explicit HardwareSender(ExternalMidiOut& out);
    ~HardwareSender();
    HardwareSender(const HardwareSender&) = delete;
    HardwareSender& operator=(const HardwareSender&) = delete;
    HardwareSender(HardwareSender&&) = delete;
    HardwareSender& operator=(HardwareSender&&) = delete;

    // Main thread: the synths to serve now (the patch's and its tails').
    void setOuts(std::vector<std::shared_ptr<HardwareOut>> outs);
    // Main thread: every note off on every synth served (panic).
    void allNotesOff();
    // Main thread, polled: what went wrong since the last call (each output
    // that failed is said once until it works again), and messages lost.
    std::vector<QString> takeProblems();
    uint64_t takeDropped();

private:
    void sendUntilStopped();
    void sendFrom(HardwareOut& out);

    ExternalMidiOut& m_out;
    std::mutex m_mutex; // guards m_outs and m_problems
    std::vector<std::shared_ptr<HardwareOut>> m_outs;
    std::vector<QString> m_problems;
    // Sender thread: outputs that failed, left alone until then (not one
    // error per note while a synth is unplugged).
    std::map<QString, std::chrono::steady_clock::time_point> m_restingUntil;
    std::atomic<uint64_t> m_dropped{0};
    // (std::thread and a flag, not std::jthread: Apple's library has no jthread.)
    std::atomic<bool> m_stop{false};
    std::thread m_thread; // stopped and joined by the destructor, before the rest goes
};

} // namespace gigchain::engine
