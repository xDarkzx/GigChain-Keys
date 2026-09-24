#pragma once

#include "MidiEvent.h"
#include "MidiQueue.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class RtMidiIn;

namespace openstage::engine {

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

    // Main thread. Opens every port except those named in `switchedOff`; a port
    // that fails is logged and named in the returned notices while the others
    // still open. Not while the audio thread drains (pause the stream first).
    std::vector<QString> openAll(const QStringList& switchedOff = {});
    void close();
    [[nodiscard]] QStringList openPortNames() const;

    // Audio thread: moves pending events into `out`, returns how many.
    std::size_t drain(std::span<MidiEvent> out) noexcept;

    // Main thread: whether a note arrived since the last call.
    bool takeActivity() { return m_activity.exchange(false, std::memory_order_relaxed); }
    // Main thread: events dropped because a queue was full, since the last call.
    uint64_t takeDropped() { return m_dropped.exchange(0, std::memory_order_relaxed); }

private:
    struct Port
    {
        std::unique_ptr<RtMidiIn> in;
        MidiQueue queue;
        MidiInput* owner = nullptr;
        QString name;
    };

    static void callback(double timeStamp, std::vector<unsigned char>* message, void* user);

    std::vector<std::unique_ptr<Port>> m_ports;
    std::atomic<bool> m_activity{false};
    std::atomic<uint64_t> m_dropped{0};
};

} // namespace openstage::engine
