#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "Windows Audio (WASAPI)");
}

// (Any app may ask for a realtime audio thread: WASAPI's "Pro Audio" task.)
core::Result<std::optional<int>> realtimePriorityLimit()
{
    return std::optional<int>{};
}

} // namespace gigchain::platform
