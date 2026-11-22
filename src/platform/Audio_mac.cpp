#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "Core Audio");
}

// (Any app may ask for a realtime audio thread: Core Audio's own are.)
std::optional<int> realtimePriorityLimit()
{
    return std::nullopt;
}

} // namespace gigchain::platform
