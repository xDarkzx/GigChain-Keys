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

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseSensitive;
}

} // namespace gigchain::platform
