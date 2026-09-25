#pragma once

#include <QString>

#include <array>
#include <cstdint>

namespace gigchain::engine {

// What a pedal, pad or button on the keyboard can do (MainStage's "assign
// a controller"), learned in Settings.
enum class ControlAction : int { NextSong = 0, PreviousSong, NextPatch, PreviousPatch, Panic };
inline constexpr int kControlActionCount = 5;

// One MIDI control: a CC (a pedal or button), a note (a pad or key) or a
// program change, on one channel.
struct MidiTrigger
{
    enum Kind : uint8_t { None = 0, ControlChange = 0xB0, Note = 0x90, ProgramChange = 0xC0 };
    Kind kind = None;
    uint8_t channel = 0; // 0-15 (MIDI channel 1-16)
    uint8_t number = 0;  // CC number, note number or program number

    [[nodiscard]] constexpr bool isSet() const { return kind != None; }
    bool operator==(const MidiTrigger&) const = default;

    // "Pedal/CC 64 (channel 1)", "Note C3 (channel 10)", "Program 5 (channel 1)".
    [[nodiscard]] QString describe() const;

    // For atomics and settings: 0 = not set.
    [[nodiscard]] constexpr uint32_t pack() const
    {
        return kind == None ? 0U : (uint32_t{kind} << 16) | (uint32_t{channel} << 8) | number;
    }
    [[nodiscard]] static constexpr MidiTrigger unpack(uint32_t packed)
    {
        const auto kind = static_cast<uint8_t>(packed >> 16);
        if (kind != ControlChange && kind != Note && kind != ProgramChange) return {};
        return MidiTrigger{static_cast<Kind>(kind), static_cast<uint8_t>((packed >> 8) & 0x0F), static_cast<uint8_t>(packed & 0x7F)};
    }
};

using ControlTriggers = std::array<MidiTrigger, kControlActionCount>;

// Real-time safe. Whether a MIDI message belongs to `trigger` (every value of
// its CC, the note's on and off), and whether it is a press: CC value 64 or
// more, a note-on with velocity, any program change.
struct TriggerMatch
{
    bool belongs = false;
    bool pressed = false;
};
[[nodiscard]] constexpr TriggerMatch matchTrigger(const MidiTrigger& trigger, uint8_t status, uint8_t data1,
                                                  uint8_t data2) noexcept
{
    if (!trigger.isSet() || (status & 0x0F) != trigger.channel) return {};
    const uint8_t type = status & 0xF0;
    switch (trigger.kind) {
    case MidiTrigger::ControlChange:
        if (type == 0xB0 && data1 == trigger.number) return {true, data2 >= 64};
        return {};
    case MidiTrigger::Note:
        if ((type == 0x90 || type == 0x80) && data1 == trigger.number) return {true, type == 0x90 && data2 > 0};
        return {};
    case MidiTrigger::ProgramChange:
        if (type == 0xC0 && data1 == trigger.number) return {true, true};
        return {};
    case MidiTrigger::None:
        break;
    }
    return {};
}

// Real-time safe. The control a pressed message would be learned as, or an
// unset trigger (note-offs, pedal releases and other messages are not).
[[nodiscard]] constexpr MidiTrigger learnable(uint8_t status, uint8_t data1, uint8_t data2) noexcept
{
    const auto channel = static_cast<uint8_t>(status & 0x0F);
    switch (status & 0xF0) {
    case 0xB0: return data2 >= 64 ? MidiTrigger{MidiTrigger::ControlChange, channel, data1} : MidiTrigger{};
    case 0x90: return data2 > 0 ? MidiTrigger{MidiTrigger::Note, channel, data1} : MidiTrigger{};
    case 0xC0: return MidiTrigger{MidiTrigger::ProgramChange, channel, data1};
    default: return {};
    }
}

} // namespace gigchain::engine
