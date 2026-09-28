#pragma once

#include "MidiEvent.h"
#include "MidiQueue.h"

#include "gigchain/engine/EngineTypes.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rt::midi {
class RtMidiIn;
} // namespace rt::midi

namespace gigchain::engine {

// A channel voice message from raw MIDI bytes, or nullopt for anything else
// (system messages, truncated or malformed input). Real-time safe.
std::optional<MidiEvent> parseMidi(std::span<const unsigned char> bytes) noexcept;

// Every MIDI input port on the machine, wrapping RtMidi. The only unit that
// includes RtMidi. Each port has its own lock-free queue (RtMidi calls back
// on one thread per port); the audio thread drains them all.
class MidiInput
{
public:
    MidiInput();
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;
    MidiInput(MidiInput&&) = delete;
    MidiInput& operator=(MidiInput&&) = delete;

    static QStringList listPorts();

    // Main thread. Opens the enabled ones of `ports` (see resolveMidiInputs),
    // each with its channel filter; a port that fails is logged and named in
    // the returned notices while the others still open. Not while the audio
    // thread drains (pause the stream first).
    std::vector<QString> openAll(const std::vector<MidiPort>& ports);
    void close();
    [[nodiscard]] QStringList openPortNames() const;

    // Audio thread: moves pending events into `out`, returns how many.
    std::size_t drain(std::span<MidiEvent> out) noexcept;

    // Main thread: whether a note arrived since the last call.
    bool takeActivity() { return m_activity.exchange(false, std::memory_order_relaxed); }
    // Main thread: events dropped because a queue was full, since the last call.
    uint64_t takeDropped() { return m_dropped.exchange(0, std::memory_order_relaxed); }

    // Any thread: the tempo of the MIDI clock coming in (a drum machine, a
    // DAW), or 0 when no clock arrived in the last half second.
    [[nodiscard]] double clockTempo() const;
    // Any thread: true once after a MIDI Start (the clock's source started
    // its song at bar 1).
    bool takeClockStart() { return m_clockStart.exchange(false, std::memory_order_relaxed); }

private:
    struct Port
    {
        std::unique_ptr<rt::midi::RtMidiIn> in;
        MidiQueue queue;
        MidiInput* owner = nullptr;
        QString name;
        int channel = 0; // 0 = all channels
        // MIDI clock (RtMidi's thread for this port): the last tick, and the
        // smoothed time between ticks.
        int64_t lastTickNs = 0;
        double tickSeconds = 0.0;
    };

    static void callback(double timeStamp, std::vector<unsigned char>* message, void* user);
    void onClockTick(Port& port);

    std::vector<std::unique_ptr<Port>> m_ports;
    std::atomic<bool> m_activity{false};
    std::atomic<uint64_t> m_dropped{0};
    std::atomic<double> m_clockTempo{0.0};
    std::atomic<int64_t> m_lastClockNs{0};
    std::atomic<bool> m_clockStart{false};
};

// MIDI clock: 24 ticks per quarter note.
inline constexpr int kClockTicksPerQuarter = 24;

} // namespace gigchain::engine
