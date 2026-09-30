#include "gigchain/platform/PluginFolders.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

QStringList standardVst3Folders()
{
    return {QDir::homePath() + u"/Library/Audio/Plug-Ins/VST3"_s, u"/Library/Audio/Plug-Ins/VST3"_s};
}

QString vst3ModuleFile(const QString& bundle)
{
    return bundle + u"/Contents/MacOS/"_s + QFileInfo(bundle).completeBaseName();
}

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseInsensitive; // the Mac's disks, by default
}

} // namespace gigchain::platform
