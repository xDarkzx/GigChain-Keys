#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

// (The same on every system: only Linux reads a limit of 0.)
QString realtimePriorityAdvice(std::optional<int> limit)
{
    if (limit != 0) return {};
    // A group's limits count from the next login (#71: a session opened
    // before the user joined the group had none, and nothing said so).
    // (A risk, not a measurement: PipeWire can still get realtime threads
    // through RTKit; plugins running through Wine inherit the limit.)
    return QCoreApplication::translate(
        "Audio", "This session's realtime priority limit is 0, so the audio, and plugins running through Wine "
                 "(yabridge) above all, can drop out more often. Add your user to the pipewire group "
                 "(sudo usermod -aG pipewire $USER), or, where there is no pipewire group, to the audio group "
                 "(sudo usermod -aG audio $USER), then log out and log in again");
}

} // namespace gigchain::platform
