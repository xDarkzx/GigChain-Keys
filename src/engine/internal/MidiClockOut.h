#pragma once

#include "gigchain/core/Error.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <thread>

class RtMidiOut;

namespace gigchain::engine {

// Sends MIDI clock (24 ticks per quarter note) at the engine's tempo to one
// MIDI output, so drum machines, arpeggiators and other gear follow the
// app. A Start goes out when it opens and a Stop when it closes. The ticks
// come from a thread of their own, timed against the system clock: an audio
// block (several milliseconds) is too coarse for them.
class MidiClockOut
{
public:
    MidiClockOut();
    ~MidiClockOut();
    MidiClockOut(const MidiClockOut&) = delete;
    MidiClockOut& operator=(const MidiClockOut&) = delete;
    MidiClockOut(MidiClockOut&&) = delete;
    MidiClockOut& operator=(MidiClockOut&&) = delete;

    static QStringList listPorts();

    // Main thread. Opens `port` and starts the clock; empty closes it. A
    // port that cannot open is an error (also logged).
    core::Result<void> open(const QString& port);
    void close();
    [[nodiscard]] QString portName() const { return m_port; }

    // Any thread.
    void setTempo(double bpm) { m_tempo.store(bpm, std::memory_order_relaxed); }
    // Ticks sent since it opened (for tests and diagnostics).
    [[nodiscard]] uint64_t ticksSent() const { return m_ticks.load(std::memory_order_relaxed); }
    // Main thread: true once after sending failed and the clock stopped (the
    // output was unplugged).
    bool takeFailed() { return m_failed.exchange(false, std::memory_order_relaxed); }

private:
    void run();

    std::unique_ptr<RtMidiOut> m_out;
    QString m_port;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<double> m_tempo{120.0};
    std::atomic<uint64_t> m_ticks{0};
    std::atomic<bool> m_failed{false};
};

} // namespace gigchain::engine
