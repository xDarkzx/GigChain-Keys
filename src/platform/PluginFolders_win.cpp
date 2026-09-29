#include "gigchain/platform/PluginFolders.h"

#include <QFileInfo>

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

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseInsensitive; // Windows paths: the same whatever the case
}

} // namespace gigchain::platform
