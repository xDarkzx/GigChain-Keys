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

QStringList standardVst2Folders()
{
    return {QDir::homePath() + u"/Library/Audio/Plug-Ins/VST"_s, u"/Library/Audio/Plug-Ins/VST"_s};
}

bool isVst2PluginFile(const QString& path)
{
    return path.endsWith(u".vst"_s, Qt::CaseInsensitive);
}

QString vst2LibraryFile(const QString& plugin)
{
    // A bundle: its binary is named in its Info.plist, and is in practice the
    // one file in Contents/MacOS.
    const QFileInfoList binaries = QDir(plugin + u"/Contents/MacOS"_s).entryInfoList(QDir::Files);
    return binaries.isEmpty() ? plugin : binaries.front().absoluteFilePath();
}

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseInsensitive; // the Mac's disks, by default
}

} // namespace gigchain::platform
