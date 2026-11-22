#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

// (The same on every system: only Linux reads a limit of 0.)
QString realtimePriorityAdvice(std::optional<int> limit)
{
    if (limit != 0) return {};
    // A group's limits count from the next login (#71: a session opened
    // before the user joined the group had none, and nothing said so).
    return QCoreApplication::translate(
        "Audio", "This session has no realtime priority, so the audio, and plugins running through Wine (yabridge), "
                 "drop out more often. Add your user to the pipewire group (sudo usermod -aG pipewire $USER), or the "
                 "audio group where there is no pipewire group, then log out and log in again");
}

} // namespace gigchain::platform
