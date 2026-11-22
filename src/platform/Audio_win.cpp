#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "Windows Audio (WASAPI)");
}

// (Any app may ask for a realtime audio thread: WASAPI's "Pro Audio" task.)
std::optional<int> realtimePriorityLimit()
{
    return std::nullopt;
}

} // namespace gigchain::platform
