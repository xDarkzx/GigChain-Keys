#pragma once

#include "MidiEvent.h"
#include "MidiQueue.h"

#include "gigchain/engine/EngineTypes.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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

// The sustain pedal's presses, for "a quick double press starts or stops the
// song": press() says whether this press came within kDoubleTapNs of the last
// (and then starts counting afresh: a third quick press is not another).
struct SustainTaps
{
    static constexpr int64_t kDoubleTapNs = 400'000'000; // 0.4 s
    bool down = false;
    int64_t lastPressNs = -1;

    // The pedal's value changed (CC 64) at `nowNs`: whether that was the second press of a double press.
    bool change(bool pressed, int64_t nowNs) noexcept
    {
        const bool newPress = pressed && !down;
        down = pressed;
        if (!newPress) return false;
        if (lastPressNs >= 0 && nowNs - lastPressNs <= kDoubleTapNs) {
            lastPressNs = -1;
            return true;
        }
        lastPressNs = nowNs;
        return false;
    }
};

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

    // Any thread: a key pressed (the keyboard's, or one played from the
    // screen), kept with when it came; the most recent 1024. Taken by the
    // main thread.
    void recordPress(int note, int velocity);
    std::vector<KeyPress> takePresses();

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
    // Any thread: what the transport buttons and the sustain pedal asked
    // (transport::k* flags) since the last call, when switched on.
    uint32_t takeTransportRequests() { return m_transportRequests.exchange(0, std::memory_order_acq_rel); }
    void setTransportControls(bool buttons, bool sustainDoubleTap)
    {
        m_transportButtons.store(buttons, std::memory_order_relaxed);
        m_sustainDoubleTap.store(sustainDoubleTap, std::memory_order_relaxed);
    }
    // Whether `message` (RtMidi's bytes) is a transport request: its
    // transport::k* flag, else 0. Sysex: MMC Play (deferred too), Stop,
    // Fast Forward and Rewind.
    [[nodiscard]] static uint32_t transportRequestOf(const std::vector<unsigned char>& message);
    // A Mackie Control button pressed (a note-on from a keyboard's DAW
    // port): its transport::k* flag, else 0. `shift`: Shift is held.
    [[nodiscard]] static uint32_t mackieRequestOf(uint8_t status, uint8_t note, uint8_t velocity, bool shift) noexcept;

private:
    struct Port
    {
        std::unique_ptr<rt::midi::RtMidiIn> in;
        MidiQueue queue;
        MidiInput* owner = nullptr;
        QString name;
        int channel = 0; // 0 = all channels
        // Not played: its buttons and knobs only (a keyboard's DAW port).
        bool controlsOnly = false;
        bool shift = false; // its Mackie Control Shift is held (this port's thread)
        // MIDI clock (RtMidi's thread for this port): the last tick, and the
        // smoothed time between ticks.
        int64_t lastTickNs = 0;
        double tickSeconds = 0.0;
        SustainTaps sustain; // (this port's thread)
    };

    static void callback(double timeStamp, std::vector<unsigned char>* message, void* user);
    void onClockTick(Port& port);

    std::vector<std::unique_ptr<Port>> m_ports;
    std::atomic<bool> m_activity{false};
    std::atomic<uint64_t> m_dropped{0};
    std::atomic<double> m_clockTempo{0.0};
    std::atomic<int64_t> m_lastClockNs{0};
    std::atomic<bool> m_clockStart{false};
    std::atomic<uint32_t> m_transportRequests{0};
    std::atomic<bool> m_transportButtons{true};
    std::atomic<bool> m_sustainDoubleTap{false};
    // Keys pressed, for timing (RtMidi's threads and the main thread add, the main thread takes).
    std::mutex m_pressesLock;
    std::vector<KeyPress> m_presses;
};

// MIDI clock: 24 ticks per quarter note.
inline constexpr int kClockTicksPerQuarter = 24;

} // namespace gigchain::engine
