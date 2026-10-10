#include "gigchain/platform/PluginFolders.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

QStringList standardVst3Folders()
{
    return {u"C:/Program Files/Common Files/VST3"_s};
}

QString vst3ModuleFile(const QString& bundle)
{
    return bundle + u"/Contents/x86_64-win/"_s + QFileInfo(bundle).fileName();
}

QStringList standardVst2Folders()
{
    QStringList folders;
    // Where the user's VST2 plugins were put, as most installers record it.
    const QSettings registry(u"HKEY_LOCAL_MACHINE\\SOFTWARE\\VST"_s, QSettings::NativeFormat);
    if (const QString named = registry.value(u"VSTPluginsPath"_s).toString(); !named.isEmpty()) {
        folders << QDir::fromNativeSeparators(named);
    }
    for (const QString& usual : {u"C:/Program Files/VSTPlugins"_s, u"C:/Program Files/Steinberg/VSTPlugins"_s,
                                 u"C:/Program Files/Common Files/VST2"_s, u"C:/Program Files/Common Files/Steinberg/VST2"_s}) {
        if (!folders.contains(usual, Qt::CaseInsensitive)) folders << usual;
    }
    return folders;
}

bool isVst2PluginFile(const QString& path)
{
    return path.endsWith(u".dll"_s, Qt::CaseInsensitive);
}

QString vst2LibraryFile(const QString& plugin)
{
    return plugin;
}

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseInsensitive; // Windows paths: the same whatever the case
}

} // namespace gigchain::platform
