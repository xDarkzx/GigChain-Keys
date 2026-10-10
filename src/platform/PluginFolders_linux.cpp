#include "gigchain/platform/PluginFolders.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

QStringList standardVst3Folders()
{
    return {QDir::homePath() + u"/.vst3"_s, u"/usr/lib/vst3"_s, u"/usr/local/lib/vst3"_s};
}

QString vst3ModuleFile(const QString& bundle)
{
    return bundle + u"/Contents/x86_64-linux/"_s + QFileInfo(bundle).completeBaseName() + u".so"_s;
}

QStringList standardVst2Folders()
{
    // VST_PATH when set (as other Linux hosts read it), else the usual folders.
    if (const QString path = qEnvironmentVariable("VST_PATH"); !path.isEmpty()) return path.split(u':', Qt::SkipEmptyParts);
    return {QDir::homePath() + u"/.vst"_s, u"/usr/lib/vst"_s, u"/usr/local/lib/vst"_s, u"/usr/lib/lxvst"_s,
            u"/usr/local/lib/lxvst"_s};
}

bool isVst2PluginFile(const QString& path)
{
    return path.endsWith(u".so"_s);
}

QString vst2LibraryFile(const QString& plugin)
{
    return plugin;
}

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseSensitive;
}

} // namespace gigchain::platform
