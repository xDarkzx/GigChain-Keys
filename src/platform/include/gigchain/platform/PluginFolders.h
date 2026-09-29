#pragma once

#include <QString>
#include <QStringList>
#include <Qt>

namespace gigchain::platform {

// The folders where this system keeps VST3 plugins, absolute, in the order
// they are searched (the VST3 standard for each system).
[[nodiscard]] QStringList standardVst3Folders();

// The file inside a .vst3 bundle folder holding this system's binary (what
// decides whether a plugin changed).
[[nodiscard]] QString vst3ModuleFile(const QString& bundle);

// Whether two paths that differ only in case are the same file here.
[[nodiscard]] Qt::CaseSensitivity fileNameCase();

} // namespace gigchain::platform
