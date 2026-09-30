#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "Core Audio");
}

} // namespace gigchain::platform
