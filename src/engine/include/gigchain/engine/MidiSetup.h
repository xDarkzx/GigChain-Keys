#pragma once

#include "gigchain/engine/EngineTypes.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <vector>

namespace gigchain::engine {

// The MIDI inputs the user chose in Settings (saved between runs).
struct MidiSetup
{
    QStringList enabled;               // inputs switched on, by name
    std::map<QString, int> channels;   // per input: 0 = all channels, 1-16 = only that one
    bool configured = false;           // false: never chosen, use the default
    // MIDI clock: the output it is sent to (empty = not sent), and whether
    // the tempo follows a clock coming in (a drum machine or DAW leads).
    QString clockOutput;
    bool followClock = false;

    bool operator==(const MidiSetup&) const = default;
};

// Port names as shown and saved, from RtMidi's (in Windows' order). RtMidi
// numbers every port by its place in the list ("Impact GXP61 0"), which
// changes when devices are plugged in in another order, so the number is
// left out; ports with the same name are told apart as "Name", "Name (2)"...
[[nodiscard]] QStringList portNames(const QStringList& numbered);
// A setup saved by a version before portNames(), its names with RtMidi's
// place number ("Impact GXP61 0"): the same setup with the numbers removed.
// Run once on such settings; a newer name may end in a number of its own.
[[nodiscard]] MidiSetup withoutPortPlaces(MidiSetup saved);

// Which inputs play, given what is plugged in (in Windows' order):
// - never chosen: only the first port. A keyboard often lists more than one
//   port (the Impact GXP61: keys on the first, DAW control on the second),
//   and two open ports would double notes.
// - chosen: exactly the inputs switched on; anything new stays off.
// - chosen, but none of them is plugged in (another keyboard at the gig):
//   the first port, so there is always sound.
[[nodiscard]] std::vector<MidiPort> resolveMidiInputs(const QStringList& present, const MidiSetup& setup);

// Whether a channel message with this status byte passes an input's channel
// filter (0 = all channels). Real-time safe.
[[nodiscard]] constexpr bool passesChannelFilter(uint8_t status, int channel) noexcept
{
    return channel <= 0 || (status & 0x0F) + 1 == channel;
}

} // namespace gigchain::engine
